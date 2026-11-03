#include "galaxy/coop_snapshot.h"

#include "galaxy/net/bytes.h"

#include <algorithm>
#include <cmath>

namespace galaxy::coop {

namespace {

// The snapshot's own version, first: one from another layout is dropped.
constexpr std::uint8_t kSnapshotVersion = 2u;
// Snapshots kept of each player: a second's worth.
constexpr std::size_t kKeep = 32u;

void put3(net::ByteWriter& out, const std::array<float, 3>& values) {
    for (const float value : values) out.f32(value);
}

std::array<float, 3> get3(net::ByteReader& in) {
    std::array<float, 3> values{};
    for (float& value : values) value = in.f32();
    return values;
}

bool finite(const std::array<float, 3>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

float lerp(float from, float to, float t) { return from + (to - from) * t; }

// A direction along the way between two: unit length again, or the later one where they cancel out.
std::array<float, 3> lerp_direction(const std::array<float, 3>& from, const std::array<float, 3>& to, float t) {
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3u; ++i) out[i] = lerp(from[i], to[i], t);
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (length <= 1e-4f) return to;
    for (float& value : out) value /= length;
    return out;
}

// The short way round between two angles in degrees.
float lerp_angle(float from, float to, float t) {
    float delta = std::fmod(to - from, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    return from + delta * t;
}

}  // namespace

std::vector<std::uint8_t> encode_snapshot(const PlayerSnapshot& snapshot) {
    net::ByteWriter out;
    out.u8(kSnapshotVersion).u32(snapshot.sequence);
    out.str(snapshot.stage.substr(0, kSnapshotStageLimit)).u32(static_cast<std::uint32_t>(snapshot.scenario));
    put3(out, snapshot.position);
    put3(out, snapshot.rotation);
    put3(out, snapshot.front);
    put3(out, snapshot.up);
    put3(out, snapshot.gravity);
    put3(out, snapshot.velocity);
    out.str(snapshot.group.substr(0, kSnapshotAnimationLimit));
    out.str(snapshot.animation.substr(0, kSnapshotAnimationLimit));
    out.f32(snapshot.frame).f32(snapshot.rate);
    for (const float weight : snapshot.weights) out.f32(weight);
    out.u32(snapshot.health).u32(snapshot.mode).u32(snapshot.flags).u32(snapshot.area);
    return out.take();
}

std::optional<PlayerSnapshot> decode_snapshot(std::span<const std::uint8_t> data) {
    net::ByteReader in(data);
    if (in.u8() != kSnapshotVersion) return std::nullopt;
    PlayerSnapshot snapshot;
    snapshot.sequence = in.u32();
    snapshot.stage = in.str(kSnapshotStageLimit);
    snapshot.scenario = static_cast<int>(in.u32());
    snapshot.position = get3(in);
    snapshot.rotation = get3(in);
    snapshot.front = get3(in);
    snapshot.up = get3(in);
    snapshot.gravity = get3(in);
    snapshot.velocity = get3(in);
    snapshot.group = in.str(kSnapshotAnimationLimit);
    snapshot.animation = in.str(kSnapshotAnimationLimit);
    snapshot.frame = in.f32();
    snapshot.rate = in.f32();
    for (float& weight : snapshot.weights) weight = in.f32();
    snapshot.health = in.u32();
    snapshot.mode = in.u32();
    snapshot.flags = in.u32();
    snapshot.area = in.u32();
    if (!in.ok()) return std::nullopt;
    // Nothing the game would choke on: no infinities, no names with a NUL inside.
    if (!finite(snapshot.position) || !finite(snapshot.rotation) || !finite(snapshot.front) || !finite(snapshot.up) ||
        !finite(snapshot.gravity) ||
        !finite(snapshot.velocity) || !std::isfinite(snapshot.frame) || !std::isfinite(snapshot.rate) ||
        !std::all_of(snapshot.weights.begin(), snapshot.weights.end(), [](float w) { return std::isfinite(w); })) {
        return std::nullopt;
    }
    for (const std::string* text : {&snapshot.stage, &snapshot.group, &snapshot.animation}) {
        if (text->find('\0') != std::string::npos) return std::nullopt;
    }
    return snapshot;
}

std::vector<std::uint8_t> encode_relayed_snapshot(net::PeerId from, std::span<const std::uint8_t> snapshot) {
    net::ByteWriter out;
    out.u64(from).raw(snapshot);
    return out.take();
}

std::optional<RelayedSnapshot> decode_relayed_snapshot(std::span<const std::uint8_t> data) {
    net::ByteReader in(data);
    RelayedSnapshot relayed;
    relayed.from = in.u64();
    if (!in.ok()) return std::nullopt;
    relayed.snapshot = in.rest();
    return relayed;
}

std::vector<net::PeerId> snapshot_audience(std::span<const SessionPlayer> players, std::string_view stage,
                                           int scenario, net::PeerId sender) {
    std::vector<net::PeerId> audience;
    if (stage.empty()) return audience;
    for (const SessionPlayer& player : players) {
        if (player.local || player.id == sender || player.scenario != scenario || player.stage != stage) continue;
        audience.push_back(player.id);
    }
    return audience;
}

void listen_snapshots(Session& session, RemotePlayers& players) {
    players.clear();
    session.on_message(kSnapshotMessage, [&session, &players](const SessionPlayer& from,
                                                               std::span<const std::uint8_t> data) {
        players.receive(from.id, from.index, from.name, from.character, data);
        if (!session.is_host()) return;
        // A joiner's, for the others in its instance.
        const std::optional<PlayerSnapshot> snapshot = decode_snapshot(data);
        if (!snapshot) return;
        const std::vector<SessionPlayer> room = session.players();
        const std::vector<net::PeerId> audience = snapshot_audience(room, snapshot->stage, snapshot->scenario, from.id);
        if (audience.empty()) return;
        const std::vector<std::uint8_t> relayed = encode_relayed_snapshot(from.id, data);
        for (const net::PeerId to : audience) {
            (void)session.send(kSnapshotRelayMessage, net::Channel::Unreliable, relayed, to);
        }
    });
    session.on_message(kSnapshotRelayMessage, [&session, &players](const SessionPlayer& from,
                                                                    std::span<const std::uint8_t> data) {
        const std::optional<RelayedSnapshot> relayed = decode_relayed_snapshot(data);
        if (!from.host || !relayed) return;
        for (const SessionPlayer& player : session.players()) {
            if (player.id != relayed->from || player.local) continue;
            players.receive(player.id, player.index, player.name, player.character, relayed->snapshot);
            return;
        }
    });
}

bool send_snapshot(Session& session, const PlayerSnapshot& snapshot) {
    const std::vector<std::uint8_t> bytes = encode_snapshot(snapshot);
    const std::vector<SessionPlayer> room = session.players();
    if (!session.is_host()) {
        for (const SessionPlayer& player : room) {
            if (player.host) return session.send(kSnapshotMessage, net::Channel::Unreliable, bytes, player.id);
        }
        return false;
    }
    bool sent = true;
    for (const net::PeerId to : snapshot_audience(room, snapshot.stage, snapshot.scenario, net::kNoPeer)) {
        sent = session.send(kSnapshotMessage, net::Channel::Unreliable, bytes, to) && sent;
    }
    return sent;
}

PlayerSnapshot blend_snapshots(const PlayerSnapshot& from, const PlayerSnapshot& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    PlayerSnapshot out = to;
    for (std::size_t i = 0; i < 3u; ++i) {
        out.position[i] = lerp(from.position[i], to.position[i], t);
        out.rotation[i] = lerp_angle(from.rotation[i], to.rotation[i], t);
        out.velocity[i] = lerp(from.velocity[i], to.velocity[i], t);
    }
    out.front = lerp_direction(from.front, to.front, t);
    out.up = lerp_direction(from.up, to.up, t);
    out.gravity = lerp_direction(from.gravity, to.gravity, t);
    // The same animation in both, not looped round in between: its frame along the way, its tracks' weights too.
    if (from.group == to.group && from.animation == to.animation && from.frame <= to.frame) {
        out.frame = lerp(from.frame, to.frame, t);
        for (std::size_t i = 0; i < out.weights.size(); ++i) out.weights[i] = lerp(from.weights[i], to.weights[i], t);
    }
    return out;
}

void RemotePlayers::receive(net::PeerId id, unsigned index, const std::string& name, CoopCharacter character,
                            std::span<const std::uint8_t> data, Clock::time_point now) {
    std::optional<PlayerSnapshot> snapshot = decode_snapshot(data);
    if (!snapshot) return;
    const std::lock_guard<std::mutex> lock(lock_);
    Player& player = players_[id];
    player.index = index;
    player.name = name;
    player.character = character;
    // Late (no newer than the last): dropped. A player who joins again is another peer, counting afresh.
    if (!player.received.empty()) {
        const std::uint32_t ahead = snapshot->sequence - player.received.back().snapshot.sequence;
        if (ahead == 0u || ahead >= 0x80000000u) return;
    }
    player.received.push_back({now, std::move(*snapshot)});
    while (player.received.size() > kKeep) player.received.pop_front();
}

void RemotePlayers::remove(net::PeerId id) {
    const std::lock_guard<std::mutex> lock(lock_);
    players_.erase(id);
}

void RemotePlayers::clear() {
    const std::lock_guard<std::mutex> lock(lock_);
    players_.clear();
}

std::vector<RemotePlayer> RemotePlayers::sample(const std::string& stage, int scenario, Clock::time_point now) const {
    std::vector<RemotePlayer> out;
    const Clock::time_point at = now - kSnapshotDelay;
    const std::lock_guard<std::mutex> lock(lock_);
    for (const auto& [id, player] : players_) {
        if (player.received.empty()) continue;
        const Received& latest = player.received.back();
        if (now - latest.at > kSnapshotStale) continue;
        if (latest.snapshot.stage != stage || latest.snapshot.scenario != scenario) continue;
        // The two either side of `at`; before the first, the first; past the last, the last.
        PlayerSnapshot snapshot = player.received.front().snapshot;
        for (std::size_t i = 1; i < player.received.size(); ++i) {
            const Received& before = player.received[i - 1u];
            const Received& after = player.received[i];
            if (after.at < at) {
                snapshot = after.snapshot;
                continue;
            }
            if (before.at <= at) {
                const auto span = std::chrono::duration<float>(after.at - before.at).count();
                const auto into = std::chrono::duration<float>(at - before.at).count();
                // Only within one instance: a player moved to another stage jumps.
                snapshot = before.snapshot.stage == after.snapshot.stage &&
                                   before.snapshot.scenario == after.snapshot.scenario
                               ? blend_snapshots(before.snapshot, after.snapshot, span > 0.0f ? into / span : 1.0f)
                               : after.snapshot;
            }
            break;
        }
        // The instance as now: a player still drawn from where they were before moving here waits for the new one.
        if (snapshot.stage != stage || snapshot.scenario != scenario) continue;
        RemotePlayer remote;
        remote.id = id;
        remote.index = player.index;
        remote.name = player.name;
        remote.character = player.character;
        remote.snapshot = std::move(snapshot);
        out.push_back(std::move(remote));
    }
    std::sort(out.begin(), out.end(), [](const RemotePlayer& a, const RemotePlayer& b) { return a.index < b.index; });
    return out;
}

}  // namespace galaxy::coop
