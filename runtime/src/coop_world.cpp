#include "galaxy/coop_world.h"

#include "galaxy/coop_session.h"
#include "galaxy/net/bytes.h"

#include <algorithm>
#include <utility>

namespace galaxy::coop {

namespace {

using net::ByteReader;
using net::ByteWriter;
using net::Channel;
using net::PeerId;

// The longest list one message carries (a record or a save is cut into several).
constexpr std::size_t kGonePerMessage = 4096u;
constexpr std::size_t kSavePerMessage = 1024u;
// What the game hasn't taken yet is kept up to this many.
constexpr std::size_t kMaxPending = 32768u;

// A name the game can look up: no terminator inside, not empty, short.
bool plain_name(const std::string& name) {
    return !name.empty() && name.size() <= kSaveNameLimit && name.find('\0') == std::string::npos;
}

// A stage's name as the game names them (letters, digits and underscores).
bool plain_stage(const std::string& stage) {
    if (stage.size() > kSaveNameLimit) return false;
    return std::all_of(stage.begin(), stage.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool valid_change(const SaveChange& change) {
    switch (change.kind) {
    case SaveKind::Star: return plain_stage(change.name) && !change.name.empty() && change.scenario > 0 && change.scenario < 256;
    case SaveKind::Flag: return plain_name(change.name);
    case SaveKind::Value: return plain_name(change.name) && change.value <= 0xffffu;
    case SaveKind::Achievement: return plain_stage(change.name) && !change.name.empty();
    }
    return false;
}

// The save's entry `change` updates rather than adds: a value's latest and an achievement's first unlock are kept by
// name alone.
bool same_entry(const SaveChange& had, const SaveChange& change) {
    if (change.kind == SaveKind::Value || change.kind == SaveKind::Achievement) {
        return had.kind == change.kind && had.name == change.name;
    }
    return had == change;
}

void write_instance(ByteWriter& out, const Instance& instance) {
    out.str(instance.stage).u8(static_cast<std::uint8_t>(std::clamp(instance.scenario, 0, 255)));
}

std::optional<Instance> read_instance(ByteReader& in) {
    Instance instance;
    instance.stage = in.str(256u);
    instance.scenario = in.u8();
    if (!in.ok() || !plain_stage(instance.stage)) return std::nullopt;
    return instance;
}

void write_gone(ByteWriter& out, std::span<const Gone> gone) {
    out.u16(static_cast<std::uint16_t>(gone.size()));
    for (const Gone& thing : gone) out.u32(thing.object).u8(static_cast<std::uint8_t>(thing.how));
}

std::optional<std::vector<Gone>> read_gone(ByteReader& in) {
    const std::size_t count = in.u16();
    std::vector<Gone> gone;
    gone.reserve(std::min(count, kGonePerMessage));
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        Gone thing;
        thing.object = in.u32();
        thing.how = in.u8();
        gone.push_back(thing);
    }
    if (!in.ok()) return std::nullopt;
    return gone;
}

void write_changes(ByteWriter& out, std::span<const SaveChange> changes) {
    out.u16(static_cast<std::uint16_t>(changes.size()));
    for (const SaveChange& change : changes) {
        out.u8(static_cast<std::uint8_t>(change.kind)).str(change.name);
        if (change.kind == SaveKind::Star) out.u8(static_cast<std::uint8_t>(change.scenario));
        if (change.kind == SaveKind::Value) out.u16(static_cast<std::uint16_t>(change.value));
        if (change.kind == SaveKind::Achievement) out.u32(change.value);
    }
}

std::optional<std::vector<SaveChange>> read_changes(ByteReader& in) {
    const std::size_t count = in.u16();
    std::vector<SaveChange> changes;
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        SaveChange change;
        const unsigned kind = in.u8();
        change.name = in.str(256u);
        if (kind == static_cast<unsigned>(SaveKind::Star)) change.scenario = in.u8();
        if (kind == static_cast<unsigned>(SaveKind::Value)) change.value = in.u16();
        if (kind == static_cast<unsigned>(SaveKind::Achievement)) change.value = in.u32();
        if (kind < 1u || kind > 4u) return std::nullopt;
        change.kind = static_cast<SaveKind>(kind);
        if (in.ok() && valid_change(change)) changes.push_back(std::move(change));
    }
    if (!in.ok()) return std::nullopt;
    return changes;
}

// A star got: its number (1 to 255) and whether it is a Grand Star.
void write_star(ByteWriter& out, const StarGot& star) {
    out.u8(static_cast<std::uint8_t>(std::clamp(star.star, 0, 255))).u8(star.grand ? 1u : 0u);
}

std::optional<StarGot> read_star(ByteReader& in) {
    StarGot star;
    star.star = in.u8();
    star.grand = in.u8() != 0u;
    if (!in.ok() || star.star < 1) return std::nullopt;
    return star;
}

// Something to send once the world's lock is let go (the session's own lock comes first on its thread).
struct Outgoing {
    std::uint8_t type = 0u;
    PeerId to = net::kEveryone;
    std::vector<std::uint8_t> bytes;
};

void flush(Session* session, std::vector<Outgoing>& out) {
    if (session == nullptr) return;
    for (const Outgoing& message : out) {
        (void)session->send(message.type, Channel::Ordered, message.bytes, message.to);
    }
    out.clear();
}

// A host's record of what is gone, for one of its players: in messages of kGonePerMessage, each with the player's
// scene (its epoch), then the end.
void queue_record(std::vector<Outgoing>& out, PeerId to, std::uint16_t epoch, std::span<const Gone> gone) {
    for (std::size_t at = 0; at < gone.size(); at += kGonePerMessage) {
        ByteWriter body;
        body.u16(epoch);
        write_gone(body, gone.subspan(at, std::min(kGonePerMessage, gone.size() - at)));
        out.push_back({kWorldGoneMessage, to, body.take()});
    }
    ByteWriter ready;
    ready.u16(epoch);
    out.push_back({kWorldReadyMessage, to, ready.take()});
}

void queue_changes(std::vector<Outgoing>& out, PeerId to, std::span<const SaveChange> changes) {
    for (std::size_t at = 0; at < changes.size(); at += kSavePerMessage) {
        ByteWriter body;
        write_changes(body, changes.subspan(at, std::min(kSavePerMessage, changes.size() - at)));
        out.push_back({kSaveChangeMessage, to, body.take()});
    }
}

}  // namespace

std::vector<std::uint8_t> encode_instance(const Instance& instance) {
    ByteWriter out;
    write_instance(out, instance);
    return out.take();
}

std::optional<Instance> decode_instance(std::span<const std::uint8_t> data) {
    ByteReader in(data);
    return read_instance(in);
}

std::vector<std::uint8_t> encode_gone(std::span<const Gone> gone) {
    ByteWriter out;
    write_gone(out, gone.first(std::min(gone.size(), kGonePerMessage)));
    return out.take();
}

std::optional<std::vector<Gone>> decode_gone(std::span<const std::uint8_t> data) {
    ByteReader in(data);
    return read_gone(in);
}

std::vector<std::uint8_t> encode_save_changes(std::span<const SaveChange> changes) {
    ByteWriter out;
    write_changes(out, changes.first(std::min(changes.size(), kSavePerMessage)));
    return out.take();
}

std::optional<std::vector<SaveChange>> decode_save_changes(std::span<const std::uint8_t> data) {
    ByteReader in(data);
    return read_changes(in);
}

// --- WorldAuthority ---

void WorldAuthority::drop_player(Player player) {
    const auto it = where_.find(player);
    if (it == where_.end()) return;
    const auto record = instances_.find(it->second);
    if (record != instances_.end()) {
        record->second.players.erase(player);
        if (record->second.players.empty()) instances_.erase(record);
    }
    where_.erase(it);
}

std::vector<Gone> WorldAuthority::enter(Player player, const Instance& instance) {
    drop_player(player);
    Record& record = instances_[instance];
    if (record.players.empty()) record.gone.clear();
    record.players.insert(player);
    where_[player] = instance;
    std::vector<Gone> gone;
    gone.reserve(record.gone.size());
    for (const auto& [object, how] : record.gone) gone.push_back({object, how});
    return gone;
}

void WorldAuthority::leave(Player player) { drop_player(player); }

WorldAuthority::Settled WorldAuthority::gone(Player player, std::span<const Gone> gone) {
    Settled settled;
    const auto it = where_.find(player);
    if (it == where_.end()) return settled;
    Record& record = instances_[it->second];
    for (const Gone& thing : gone) {
        if (record.gone.size() >= kMaxGonePerInstance) break;
        if (record.gone.emplace(thing.object, thing.how).second) settled.news.push_back(thing);
    }
    if (settled.news.empty()) return settled;
    for (const Player other : record.players) {
        if (other != player) settled.tell.push_back(other);
    }
    return settled;
}

std::vector<WorldAuthority::Player> WorldAuthority::star_got(Player player) const {
    const auto it = where_.find(player);
    if (it == where_.end()) return {};
    std::vector<Player> tell = players_in(it->second);
    tell.erase(std::remove(tell.begin(), tell.end(), player), tell.end());
    return tell;
}

bool WorldAuthority::save(const SaveChange& change, bool from_host) {
    if (!valid_change(change)) return false;
    if (change.kind == SaveKind::Value && !from_host) return false;
    const auto it = std::find_if(save_.begin(), save_.end(),
                                 [&change](const SaveChange& had) { return same_entry(had, change); });
    if (it != save_.end()) {
        if (change.kind != SaveKind::Value || it->value == change.value) return false;
        // The latest value only, kept in its place: the record doesn't grow as a value changes.
        it->value = change.value;
        return true;
    }
    if (save_.size() >= kMaxSaveChanges) return false;
    save_.push_back(change);
    return true;
}

std::optional<Instance> WorldAuthority::instance_of(Player player) const {
    const auto it = where_.find(player);
    if (it == where_.end()) return std::nullopt;
    return it->second;
}

std::vector<WorldAuthority::Player> WorldAuthority::players_in(const Instance& instance) const {
    const auto it = instances_.find(instance);
    if (it == instances_.end()) return {};
    return {it->second.players.begin(), it->second.players.end()};
}

void WorldAuthority::clear() {
    instances_.clear();
    where_.clear();
    save_.clear();
}

// --- World ---


void World::attach(Session& session) {
    session_ = &session;
    // A joiner's scene is ready (host): its record back.
    session.on_message(kWorldEnterMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::optional<Instance> instance = read_instance(in);
        if (!instance) return;
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_ || !host_) return;
            const std::vector<Gone> gone = authority_.enter(from.id, *instance);
            epochs_[from.id] = epoch;
            queue_record(out, from.id, epoch, gone);
        }
        flush(session_, out);
    });
    // Things gone: a joiner's claims (host), or the host's news (joiner).
    session.on_message(kWorldGoneMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::optional<std::vector<Gone>> gone = read_gone(in);
        if (!gone) return;
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_) return;
        if (host_) {
            const auto it = epochs_.find(from.id);
            if (it == epochs_.end() || it->second != epoch) return;
            host_gone(from.id, *gone);
            return;
        }
        if (!from.host || epoch != epoch_) return;
        for (const Gone& thing : *gone) push({WorldEvent::Kind::Gone, thing, {}});
    });
    session.on_message(kWorldReadyMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!in.ok() || !active_ || host_ || !from.host || epoch != epoch_) return;
        push({WorldEvent::Kind::Ready, {}, {}});
    });
    // Save changes: a player's (host), or the session's accepted ones (joiner).
    session.on_message(kSaveChangeMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        const std::optional<std::vector<SaveChange>> changes = decode_save_changes(data);
        if (!changes) return;
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_) return;
        if (host_) {
            for (const SaveChange& change : *changes) host_save(from.id, change);
            return;
        }
        if (!from.host) return;
        for (const SaveChange& change : *changes) push_save(change);
    });
    // A star got: a joiner's (host), or one taken in this player's instance (joiner).
    session.on_message(kStarGotMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::optional<StarGot> star = read_star(in);
        if (!star) return;
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_) return;
            if (!host_) {
                if (from.host && epoch == epoch_) push({WorldEvent::Kind::StarGot, {}, {}, *star});
                return;
            }
            const auto it = epochs_.find(from.id);
            if (it == epochs_.end() || it->second != epoch) return;
            for (auto& [to, bytes] : host_star(from.id, *star)) out.push_back({kStarGotMessage, to, std::move(bytes)});
        }
        flush(session_, out);
    });
    session.on_message(kSaveRequestMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t>) {
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_ || !host_) return;
            queue_changes(out, from.id, authority_.save_changes());
        }
        flush(session_, out);
    });
}

void World::start() {
    const bool host = session_ != nullptr && session_->is_host();
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        active_ = true;
        host_ = host;
        authority_.clear();
        save_.clear();
        entered_.reset();
        events_.clear();
        achievements_.clear();
        epochs_.clear();
        members_.clear();
        epoch_ = 0u;
    }
    if (!host && session_ != nullptr) {
        (void)session_->send(kSaveRequestMessage, Channel::Ordered, {}, host_peer());
    }
}

void World::stop() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    active_ = false;
    authority_.clear();
    save_.clear();
    entered_.reset();
    events_.clear();
    achievements_.clear();
    epochs_.clear();
    members_.clear();
}

net::PeerId World::host_peer() const {
    if (session_ == nullptr) return net::kNoPeer;
    for (const SessionPlayer& player : session_->players()) {
        if (player.host) return player.id;
    }
    return net::kNoPeer;
}

net::PeerId World::self_peer() const {
    if (session_ == nullptr) return net::kNoPeer;
    for (const SessionPlayer& player : session_->players()) {
        if (player.local) return player.id;
    }
    return net::kNoPeer;
}

void World::update() {
    if (session_ == nullptr) return;
    std::vector<PeerId> now;
    for (const SessionPlayer& player : session_->players()) now.push_back(player.id);
    std::sort(now.begin(), now.end());
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!active_ || !host_ || now == members_) return;
    for (const PeerId gone : members_) {
        if (!std::binary_search(now.begin(), now.end(), gone)) {
            authority_.leave(gone);
            epochs_.erase(gone);
        }
    }
    members_ = std::move(now);
}

void World::enter(const Instance& instance) {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_) return;
        entered_ = instance;
        ++epoch_;
        // Nothing from an earlier scene is for this one; the save is all given again.
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                                     [](const WorldEvent& event) { return event.kind != WorldEvent::Kind::Save; }),
                      events_.end());
        for (const SaveChange& change : save_) {
            if (change.kind != SaveKind::Achievement) push({WorldEvent::Kind::Save, {}, change});
        }
        if (host_) {
            for (const Gone& thing : authority_.enter(self, instance)) push({WorldEvent::Kind::Gone, thing, {}});
            push({WorldEvent::Kind::Ready, {}, {}});
        } else {
            ByteWriter body;
            body.u16(epoch_);
            write_instance(body, instance);
            out.push_back({kWorldEnterMessage, host, body.take()});
        }
    }
    flush(session_, out);
}

void World::report_gone(const Gone& gone) {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        if (host_) {
            const WorldAuthority::Settled settled = authority_.gone(self, std::span<const Gone>(&gone, 1u));
            for (const PeerId to : settled.tell) {
                const auto it = epochs_.find(to);
                if (it == epochs_.end()) continue;
                ByteWriter body;
                body.u16(it->second);
                write_gone(body, settled.news);
                out.push_back({kWorldGoneMessage, to, body.take()});
            }
        } else {
            ByteWriter body;
            body.u16(epoch_);
            write_gone(body, std::span<const Gone>(&gone, 1u));
            out.push_back({kWorldGoneMessage, host, body.take()});
        }
    }
    flush(session_, out);
}

void World::report_save(const SaveChange& change) {
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    bool star = false;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !valid_change(change)) return;
        if (host_) {
            if (authority_.save(change, true)) {
                remember_save(change);
                queue_changes(out, net::kEveryone, std::span<const SaveChange>(&change, 1u));
                star = change.kind == SaveKind::Star;
            }
        } else if (change.kind != SaveKind::Value) {
            // A joiner's event values are its own (WorldAuthority::save): they aren't sent.
            queue_changes(out, host, std::span<const SaveChange>(&change, 1u));
        }
    }
    flush(session_, out);
    // The Personal Star Counter: a star new to the session's save counts for who got it, the host here.
    if (star && session_ != nullptr) session_->credit_star(self_peer());
}

void World::report_star(const StarGot& star) {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    if (star.star < 1 || star.star > 255) return;
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        if (host_) {
            for (auto& [to, bytes] : host_star(self, star)) out.push_back({kStarGotMessage, to, std::move(bytes)});
        } else {
            ByteWriter body;
            body.u16(epoch_);
            write_star(body, star);
            out.push_back({kStarGotMessage, host, body.take()});
        }
    }
    flush(session_, out);
}

std::vector<WorldEvent> World::take() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return std::exchange(events_, {});
}

std::vector<SaveChange> World::take_achievements() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return std::exchange(achievements_, {});
}

bool World::active() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return active_;
}

std::size_t World::instance_count() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return authority_.instance_count();
}

void World::push(WorldEvent event) {
    if (events_.size() >= kMaxPending) {
        // The game isn't taking them (no scene runs): what is gone is given again at its next enter; the save stays.
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                                     [](const WorldEvent& e) { return e.kind != WorldEvent::Kind::Save; }),
                      events_.end());
        if (events_.size() >= kMaxPending) return;
    }
    events_.push_back(std::move(event));
}

void World::remember_save(const SaveChange& change) {
    const auto it = std::find_if(save_.begin(), save_.end(),
                                 [&change](const SaveChange& had) { return same_entry(had, change); });
    if (it != save_.end()) {
        if (change.kind == SaveKind::Value) it->value = change.value;
        return;
    }
    if (save_.size() < kMaxSaveChanges) save_.push_back(change);
}

void World::push_save(const SaveChange& change) {
    remember_save(change);
    if (change.kind != SaveKind::Achievement) {
        push({WorldEvent::Kind::Save, {}, change});
    } else if (achievements_.size() < kMaxSaveChanges) {
        achievements_.push_back(change);
    }
}

// On the session's thread, the world's lock held: a joiner's claims settled, the others in its instance told.
void World::host_gone(PeerId from, std::span<const Gone> gone) {
    const WorldAuthority::Settled settled = authority_.gone(from, gone);
    if (settled.news.empty()) return;
    const PeerId self = self_peer();
    std::vector<Outgoing> out;
    for (const PeerId to : settled.tell) {
        if (to == self) {
            for (const Gone& thing : settled.news) push({WorldEvent::Kind::Gone, thing, {}});
            continue;
        }
        const auto it = epochs_.find(to);
        if (it == epochs_.end()) continue;
        ByteWriter body;
        body.u16(it->second);
        write_gone(body, settled.news);
        out.push_back({kWorldGoneMessage, to, body.take()});
    }
    flush(session_, out);
}

// The world's lock held: the others in the taker's instance told, each for its own scene.
std::vector<std::pair<PeerId, std::vector<std::uint8_t>>> World::host_star(PeerId from, const StarGot& star) {
    const PeerId self = self_peer();
    std::vector<std::pair<PeerId, std::vector<std::uint8_t>>> out;
    for (const PeerId to : authority_.star_got(from)) {
        if (to == self) {
            push({WorldEvent::Kind::StarGot, {}, {}, star});
            continue;
        }
        const auto it = epochs_.find(to);
        if (it == epochs_.end()) continue;
        ByteWriter body;
        body.u16(it->second);
        write_star(body, star);
        out.emplace_back(to, body.take());
    }
    return out;
}

void World::host_save(PeerId from, const SaveChange& change) {
    if (!authority_.save(change, false)) return;
    // The Personal Star Counter: a star new to the session's save counts for the player who got it (on the session's
    // thread, which holds the session already).
    if (change.kind == SaveKind::Star && session_ != nullptr) session_->credit_star(from);
    // The host's game and every player, the sender too: each one's copy of the save is the session's.
    push_save(change);
    std::vector<Outgoing> out;
    queue_changes(out, net::kEveryone, std::span<const SaveChange>(&change, 1u));
    flush(session_, out);
}

}  // namespace galaxy::coop
