#include "galaxy/coop_world.h"

#include "galaxy/coop_session.h"
#include "galaxy/net/bytes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    case SaveKind::Coins:
        return plain_stage(change.name) && !change.name.empty() && change.scenario > 0 && change.scenario < 256 &&
               change.value <= 0xffffu;
    case SaveKind::RaceTime: return plain_name(change.name) && change.value != 0u;
    case SaveKind::StarBits: return change.name.empty() && change.value <= 0xffffu;
    case SaveKind::StarBitsChange: return change.name.empty() && change.value != 0u;
    }
    return false;
}

// A high score better than the one had: more coins, a quicker race.
bool better_score(const SaveChange& had, const SaveChange& change) {
    if (change.kind == SaveKind::Coins) return change.value > had.value;
    if (change.kind == SaveKind::RaceTime) return change.value < had.value;
    return false;
}

// The save's entry `change` updates rather than adds: a value's latest and an achievement's first unlock are kept by
// name alone.
bool same_entry(const SaveChange& had, const SaveChange& change) {
    if (change.kind == SaveKind::Value || change.kind == SaveKind::Achievement || change.kind == SaveKind::RaceTime ||
        change.kind == SaveKind::StarBits) {
        return had.kind == change.kind && had.name == change.name;
    }
    if (change.kind == SaveKind::Coins) {
        return had.kind == change.kind && had.name == change.name && had.scenario == change.scenario;
    }
    return had == change;
}

// Whether the save's entry takes `change`'s value: a value's or the bank's latest, a high score's better one.
bool updates_entry(const SaveChange& had, const SaveChange& change) {
    if (change.kind == SaveKind::Value || change.kind == SaveKind::StarBits) return had.value != change.value;
    return better_score(had, change);
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
        if (change.kind == SaveKind::Star || change.kind == SaveKind::Coins) {
            out.u8(static_cast<std::uint8_t>(change.scenario));
        }
        if (change.kind == SaveKind::Value || change.kind == SaveKind::Coins || change.kind == SaveKind::StarBits) {
            out.u16(static_cast<std::uint16_t>(change.value));
        }
        if (change.kind == SaveKind::Achievement || change.kind == SaveKind::RaceTime ||
            change.kind == SaveKind::StarBitsChange) {
            out.u32(change.value);
        }
    }
}

std::optional<std::vector<SaveChange>> read_changes(ByteReader& in) {
    const std::size_t count = in.u16();
    std::vector<SaveChange> changes;
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        SaveChange change;
        const unsigned kind = in.u8();
        change.name = in.str(256u);
        if (kind == static_cast<unsigned>(SaveKind::Star) || kind == static_cast<unsigned>(SaveKind::Coins)) {
            change.scenario = in.u8();
        }
        if (kind == static_cast<unsigned>(SaveKind::Value) || kind == static_cast<unsigned>(SaveKind::Coins) ||
            kind == static_cast<unsigned>(SaveKind::StarBits)) {
            change.value = in.u16();
        }
        if (kind == static_cast<unsigned>(SaveKind::Achievement) || kind == static_cast<unsigned>(SaveKind::RaceTime) ||
            kind == static_cast<unsigned>(SaveKind::StarBitsChange)) {
            change.value = in.u32();
        }
        if (kind < 1u || kind > 8u) return std::nullopt;
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

// An object's state on the wire: its speed in 64ths of a unit a frame and its turn in 32767ths, its owner's distance in
// fours of units.
std::int16_t to_fixed(float value, float scale) {
    const float scaled = std::clamp(value * scale, -32767.0f, 32767.0f);
    return static_cast<std::int16_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

bool finite3(const std::array<float, 3>& vec) {
    return std::isfinite(vec[0]) && std::isfinite(vec[1]) && std::isfinite(vec[2]);
}

void write_states(ByteWriter& out, std::span<const ObjectState> states) {
    out.u8(static_cast<std::uint8_t>(states.size()));
    for (const ObjectState& state : states) {
        out.u32(state.object).u32(state.nerve).u16(state.step).u8(state.flags);
        for (const float value : state.position) out.f32(value);
        for (const float value : state.velocity) out.u16(static_cast<std::uint16_t>(to_fixed(value, 64.0f)));
        for (const float value : state.rotation) out.u16(static_cast<std::uint16_t>(to_fixed(value, 32767.0f)));
        out.u16(static_cast<std::uint16_t>(std::clamp(state.distance * 0.25f + 0.5f, 0.0f, 65535.0f)));
    }
}

std::optional<std::vector<ObjectState>> read_states(ByteReader& in) {
    const std::size_t count = in.u8();
    std::vector<ObjectState> states;
    states.reserve(std::min(count, kObjectStatesPerMessage));
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        ObjectState state;
        state.object = in.u32();
        state.nerve = in.u32();
        state.step = in.u16();
        state.flags = in.u8();
        for (float& value : state.position) value = in.f32();
        for (float& value : state.velocity) value = static_cast<std::int16_t>(in.u16()) / 64.0f;
        for (float& value : state.rotation) value = static_cast<std::int16_t>(in.u16()) / 32767.0f;
        state.distance = in.u16() * 4.0f;
        if (in.ok() && finite3(state.position)) states.push_back(state);
    }
    if (!in.ok()) return std::nullopt;
    return states;
}

void write_hit(ByteWriter& out, const ObjectHit& hit) {
    out.u32(hit.object).u16(hit.message).u8(hit.sensor);
}

std::optional<ObjectHit> read_hit(ByteReader& in) {
    ObjectHit hit;
    hit.object = in.u32();
    hit.message = in.u16();
    hit.sensor = in.u8();
    if (!in.ok()) return std::nullopt;
    return hit;
}

void write_switches(ByteWriter& out, std::span<const SwitchChange> switches) {
    out.u16(static_cast<std::uint16_t>(switches.size()));
    for (const SwitchChange& change : switches) out.u32(change.id).u8(change.on ? 1u : 0u);
}

std::optional<std::vector<SwitchChange>> read_switches(ByteReader& in) {
    const std::size_t count = in.u16();
    std::vector<SwitchChange> switches;
    switches.reserve(std::min(count, kMaxSwitchesPerInstance));
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        SwitchChange change;
        change.id = in.u32();
        change.on = in.u8() != 0u;
        switches.push_back(change);
    }
    if (!in.ok()) return std::nullopt;
    return switches;
}

// Something to send once the world's lock is let go (the session's own lock comes first on its thread).
using Outgoing = World::Outgoing;

void flush(Session* session, std::vector<Outgoing>& out) {
    if (session == nullptr) return;
    for (const Outgoing& message : out) {
        (void)session->send(message.type, message.reliable ? Channel::Ordered : Channel::Unreliable, message.bytes,
                            message.to);
    }
    out.clear();
}

// Objects' states for a player's scene (its epoch), from their owner (its number), a datagram each.
void queue_states(std::vector<Outgoing>& out, PeerId to, std::uint16_t epoch, unsigned owner,
                  std::span<const ObjectState> states) {
    for (std::size_t at = 0; at < states.size(); at += kObjectStatesPerMessage) {
        ByteWriter body;
        body.u16(epoch).u8(static_cast<std::uint8_t>(owner));
        write_states(body, states.subspan(at, std::min(kObjectStatesPerMessage, states.size() - at)));
        out.push_back({kObjectStateMessage, to, body.take(), false});
    }
}

void queue_switches(std::vector<Outgoing>& out, PeerId to, std::uint16_t epoch, std::span<const SwitchChange> switches) {
    for (std::size_t at = 0; at < switches.size(); at += kMaxSwitchesPerInstance) {
        ByteWriter body;
        body.u16(epoch);
        write_switches(body, switches.subspan(at, std::min(kMaxSwitchesPerInstance, switches.size() - at)));
        out.push_back({kWorldSwitchMessage, to, body.take()});
    }
}

// A host's record of an instance, for one of its players: what is gone in messages of kGonePerMessage, then its
// switches, each with the player's scene (its epoch), then the end.
void queue_record(std::vector<Outgoing>& out, PeerId to, std::uint16_t epoch, std::span<const Gone> gone,
                  std::span<const SwitchChange> switches) {
    for (std::size_t at = 0; at < gone.size(); at += kGonePerMessage) {
        ByteWriter body;
        body.u16(epoch);
        write_gone(body, gone.subspan(at, std::min(kGonePerMessage, gone.size() - at)));
        out.push_back({kWorldGoneMessage, to, body.take()});
    }
    if (!switches.empty()) queue_switches(out, to, epoch, switches);
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

std::vector<std::uint8_t> encode_object_states(std::span<const ObjectState> states) {
    ByteWriter out;
    write_states(out, states.first(std::min(states.size(), kObjectStatesPerMessage)));
    return out.take();
}

std::optional<std::vector<ObjectState>> decode_object_states(std::span<const std::uint8_t> data) {
    ByteReader in(data);
    return read_states(in);
}

std::vector<std::uint8_t> encode_switches(std::span<const SwitchChange> switches) {
    ByteWriter out;
    write_switches(out, switches.first(std::min(switches.size(), kMaxSwitchesPerInstance)));
    return out.take();
}

std::optional<std::vector<SwitchChange>> decode_switches(std::span<const std::uint8_t> data) {
    ByteReader in(data);
    return read_switches(in);
}

// --- WorldAuthority ---

void WorldAuthority::drop_player(Player player) {
    const auto it = where_.find(player);
    if (it == where_.end()) return;
    const auto record = instances_.find(it->second);
    if (record != instances_.end()) {
        record->second.players.erase(player);
        record->second.missed.erase(player);
        // What it moved is the next player's to take.
        std::erase_if(record->second.owners, [player](const auto& owned) { return owned.second.player == player; });
        if (record->second.players.empty()) instances_.erase(record);
    }
    where_.erase(it);
}

std::vector<Gone> WorldAuthority::enter(Player player, const Instance& instance) {
    drop_player(player);
    Record& record = instances_[instance];
    if (record.players.size() == record.missed.size()) {
        record.gone.clear();
        record.owners.clear();
        record.switches.clear();
    }
    record.players.insert(player);
    where_[player] = instance;
    std::vector<Gone> gone;
    gone.reserve(record.gone.size());
    for (const auto& [object, how] : record.gone) gone.push_back({object, how});
    return gone;
}

void WorldAuthority::leave(Player player) { drop_player(player); }

void WorldAuthority::miss(Player player) {
    Record* record = playing(player);
    if (record == nullptr) return;
    record->missed.insert(player);
    std::erase_if(record->owners, [player](const auto& owned) { return owned.second.player == player; });
}

WorldAuthority::Record* WorldAuthority::playing(Player player) {
    const auto it = where_.find(player);
    if (it == where_.end()) return nullptr;
    const auto record = instances_.find(it->second);
    if (record == instances_.end() || record->second.missed.count(player) != 0u) return nullptr;
    return &record->second;
}

WorldAuthority::Settled WorldAuthority::gone(Player player, std::span<const Gone> gone) {
    Settled settled;
    Record* playing_in = playing(player);
    if (playing_in == nullptr) return settled;
    Record& record = *playing_in;
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

std::vector<WorldAuthority::Player> WorldAuthority::others(Player player) const { return star_got(player); }

WorldAuthority::Claimed WorldAuthority::claim(Player player, std::span<const ObjectState> states, std::uint64_t now_ms) {
    Claimed claimed;
    Record* playing_in = playing(player);
    if (playing_in == nullptr) return claimed;
    Record& record = *playing_in;
    for (const ObjectState& state : states) {
        // A thing gone from the instance moves for nobody.
        if (record.gone.count(state.object) != 0u) continue;
        auto owned = record.owners.find(state.object);
        if (owned == record.owners.end()) {
            if (record.owners.size() >= kMaxGonePerInstance) continue;
            owned = record.owners.emplace(state.object, Owner{player, now_ms, state.distance}).first;
        } else {
            Owner& owner = owned->second;
            const bool silent = now_ms - owner.heard_ms >= kObjectOwnerSilenceMs || now_ms < owner.heard_ms;
            const bool nearer = state.distance * 1.25f < owner.distance;
            const bool hit = (state.flags & kObjectHitClaim) != 0u;
            if (owner.player != player && !silent && !nearer && !hit) continue;
            owner = {player, now_ms, state.distance};
        }
        claimed.states.push_back(state);
    }
    if (!claimed.states.empty()) {
        for (const Player other : record.players) {
            if (other != player) claimed.tell.push_back(other);
        }
    }
    return claimed;
}

std::optional<WorldAuthority::Player> WorldAuthority::owner(Player player, std::uint32_t object) const {
    const auto it = where_.find(player);
    if (it == where_.end()) return std::nullopt;
    const auto record = instances_.find(it->second);
    if (record == instances_.end()) return std::nullopt;
    const auto owned = record->second.owners.find(object);
    if (owned == record->second.owners.end()) return std::nullopt;
    return owned->second.player;
}

WorldAuthority::Switched WorldAuthority::switches(Player player, std::span<const SwitchChange> switches) {
    Switched switched;
    Record* playing_in = playing(player);
    if (playing_in == nullptr) return switched;
    Record& record = *playing_in;
    for (const SwitchChange& change : switches) {
        const auto had = record.switches.find(change.id);
        if (had != record.switches.end()) {
            if (had->second == change.on) continue;
            had->second = change.on;
        } else {
            if (record.switches.size() >= kMaxSwitchesPerInstance) continue;
            record.switches.emplace(change.id, change.on);
        }
        switched.news.push_back(change);
    }
    if (!switched.news.empty()) {
        for (const Player other : record.players) {
            if (other != player) switched.tell.push_back(other);
        }
    }
    return switched;
}

std::vector<SwitchChange> WorldAuthority::switches_of(const Instance& instance) const {
    std::vector<SwitchChange> switches;
    const auto it = instances_.find(instance);
    if (it == instances_.end()) return switches;
    switches.reserve(it->second.switches.size());
    for (const auto& [id, on] : it->second.switches) switches.push_back({id, on});
    return switches;
}

bool WorldAuthority::save(const SaveChange& change, bool from_host) {
    if (!valid_change(change)) return false;
    // The bank's changes aren't kept (World::host_save gives a joiner's to the host's game).
    if (change.kind == SaveKind::StarBitsChange) return false;
    if ((change.kind == SaveKind::Value || change.kind == SaveKind::StarBits) && !from_host) return false;
    const auto it = std::find_if(save_.begin(), save_.end(),
                                 [&change](const SaveChange& had) { return same_entry(had, change); });
    if (it != save_.end()) {
        if (!updates_entry(*it, change)) return false;
        // The latest value or the best score only, kept in its place: the record doesn't grow as it changes.
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
            queue_record(out, from.id, epoch, gone, authority_.switches_of(*instance));
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
    // Objects' states: a joiner's (host), or the ones the host let their owner move (joiner).
    session.on_message(kObjectStateMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const unsigned owner = in.u8();
        const std::optional<std::vector<ObjectState>> states = read_states(in);
        if (!states) return;
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_) return;
            if (!host_) {
                if (!from.host || epoch != epoch_ || owner == 0u) return;
                for (ObjectState state : *states) {
                    state.player = owner;
                    push_state(state);
                }
                return;
            }
            const auto it = epochs_.find(from.id);
            if (it == epochs_.end() || it->second != epoch) return;
            host_states(from.id, from.index + 1u, *states, out);
        }
        flush(session_, out);
    });
    // A hit: a joiner's player's (host), or another player's in this player's instance (joiner).
    session.on_message(kObjectHitMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const unsigned player = in.u8();
        std::optional<ObjectHit> hit = read_hit(in);
        if (!hit) return;
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_) return;
            if (!host_) {
                if (!from.host || epoch != epoch_ || player == 0u) return;
                hit->player = player;
                push({WorldEvent::Kind::Hit, {}, {}, {}, *hit});
                return;
            }
            const auto it = epochs_.find(from.id);
            if (it == epochs_.end() || it->second != epoch) return;
            hit->player = from.index + 1u;
            host_hit(from.id, *hit, out);
        }
        flush(session_, out);
    });
    // Switches turned: a joiner's (host), or the instance's (joiner: its record at the enter, then news).
    session.on_message(kWorldSwitchMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::optional<std::vector<SwitchChange>> switches = read_switches(in);
        if (!switches) return;
        std::vector<Outgoing> out;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            if (!active_) return;
            if (!host_) {
                if (!from.host || epoch != epoch_) return;
                for (const SwitchChange& change : *switches) push({WorldEvent::Kind::Switch, {}, {}, {}, {}, change});
                return;
            }
            const auto it = epochs_.find(from.id);
            if (it == epochs_.end() || it->second != epoch) return;
            host_switches(from.id, *switches, out);
        }
        flush(session_, out);
    });
    // A joiner missed: it doesn't keep the instance its scene (the epoch) was in going, until it enters again.
    session.on_message(kWorldMissMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint16_t epoch = in.u16();
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!in.ok() || !active_ || !host_) return;
        const auto it = epochs_.find(from.id);
        if (it == epochs_.end() || it->second != epoch) return;
        authority_.miss(from.id);
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
        states_.clear();
        state_index_.clear();
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
    states_.clear();
    state_index_.clear();
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
        states_.clear();
        state_index_.clear();
        if (host_) {
            for (const Gone& thing : authority_.enter(self, instance)) push({WorldEvent::Kind::Gone, thing, {}});
            for (const SwitchChange& change : authority_.switches_of(instance)) {
                push({WorldEvent::Kind::Switch, {}, {}, {}, {}, change});
            }
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
        } else if (change.kind != SaveKind::Value && change.kind != SaveKind::StarBits) {
            // A joiner's event values and bank are its own (WorldAuthority::save): they aren't sent. Its changes to
            // the bank are, for the host's game.
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

void World::report_miss() {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        entered_.reset();
        states_.clear();
        state_index_.clear();
        if (host_) {
            authority_.miss(self);
        } else {
            ByteWriter body;
            body.u16(epoch_);
            out.push_back({kWorldMissMessage, host, body.take()});
        }
    }
    flush(session_, out);
}

void World::report_states(std::span<const ObjectState> states) {
    if (states.empty()) return;
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        if (host_) {
            host_states(self, 1u, states, out);
        } else {
            queue_states(out, host, epoch_, 0u, states);
        }
    }
    flush(session_, out);
}

void World::report_hit(const ObjectHit& hit) {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        if (host_) {
            ObjectHit own = hit;
            own.player = 1u;
            host_hit(self, own, out);
        } else {
            ByteWriter body;
            body.u16(epoch_).u8(0u);
            write_hit(body, hit);
            out.push_back({kObjectHitMessage, host, body.take()});
        }
    }
    flush(session_, out);
}

void World::report_switch(const SwitchChange& change) {
    const PeerId self = self_peer();
    const PeerId host = host_peer();
    std::vector<Outgoing> out;
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (!active_ || !entered_) return;
        if (host_) {
            host_switches(self, std::span<const SwitchChange>(&change, 1u), out);
        } else {
            queue_switches(out, host, epoch_, std::span<const SwitchChange>(&change, 1u));
        }
    }
    flush(session_, out);
}

std::vector<ObjectState> World::take_states() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    state_index_.clear();
    return std::exchange(states_, {});
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
        if (updates_entry(*it, change)) it->value = change.value;
        return;
    }
    if (change.kind == SaveKind::StarBitsChange) return;
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
    if (change.kind == SaveKind::StarBitsChange) {
        // The host's game keeps the bank: it applies a joiner's change, and its bank comes back to everyone.
        if (valid_change(change)) push({WorldEvent::Kind::Save, {}, change});
        return;
    }
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

// The world's lock held, the host's: the states the host lets `from` move, to the others in its instance.
void World::host_states(PeerId from, unsigned number, std::span<const ObjectState> states, std::vector<Outgoing>& out) {
    const auto now_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::steady_clock::now().time_since_epoch())
                                                       .count());
    const WorldAuthority::Claimed claimed = authority_.claim(from, states, now_ms);
    if (claimed.states.empty()) return;
    const PeerId self = self_peer();
    for (const PeerId to : claimed.tell) {
        if (to == self) {
            for (ObjectState state : claimed.states) {
                state.player = number;
                push_state(state);
            }
            continue;
        }
        const auto it = epochs_.find(to);
        if (it == epochs_.end()) continue;
        queue_states(out, to, it->second, number, claimed.states);
    }
}

void World::host_hit(PeerId from, ObjectHit hit, std::vector<Outgoing>& out) {
    const PeerId self = self_peer();
    for (const PeerId to : authority_.others(from)) {
        if (to == self) {
            push({WorldEvent::Kind::Hit, {}, {}, {}, hit});
            continue;
        }
        const auto it = epochs_.find(to);
        if (it == epochs_.end()) continue;
        ByteWriter body;
        body.u16(it->second).u8(static_cast<std::uint8_t>(hit.player));
        write_hit(body, hit);
        out.push_back({kObjectHitMessage, to, body.take()});
    }
}

void World::host_switches(PeerId from, std::span<const SwitchChange> switches, std::vector<Outgoing>& out) {
    const WorldAuthority::Switched switched = authority_.switches(from, switches);
    if (switched.news.empty()) return;
    const PeerId self = self_peer();
    for (const PeerId to : switched.tell) {
        if (to == self) {
            for (const SwitchChange& change : switched.news) push({WorldEvent::Kind::Switch, {}, {}, {}, {}, change});
            continue;
        }
        const auto it = epochs_.find(to);
        if (it == epochs_.end()) continue;
        queue_switches(out, to, it->second, switched.news);
    }
}

// The latest state of each object waits for the game; older ones of it are dropped.
void World::push_state(const ObjectState& state) {
    const auto [at, added] = state_index_.emplace(state.object, states_.size());
    if (!added) {
        states_[at->second] = state;
        return;
    }
    if (states_.size() >= kMaxGonePerInstance) {
        state_index_.erase(at);
        return;
    }
    states_.push_back(state);
}

}  // namespace galaxy::coop
