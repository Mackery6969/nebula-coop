// Online co-op's player snapshots (docs/COOP_PLAN.md "Remote players"): what each player sends of its Mario 30 times a
// second, unreliably, and how the others draw it. A snapshot is the instance the player is in (the stage and the
// scenario), where it is and how it is turned, and the animation playing. RemotePlayers keeps the latest few of each
// other player and gives them back a little in the past, interpolated between two, so a lost or late datagram doesn't
// show as a jump.
#pragma once

#include "galaxy/coop_session.h"
#include "galaxy/net/transport.h"
#include "galaxy/runtime_settings.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

// The session's message type the snapshots go as (galaxy/coop_session.h: the features' first).
inline constexpr std::uint8_t kSnapshotMessage = 32u;
// A joiner's snapshot as the host passes it on to the others in its instance (a joiner sends its own to the host
// alone, which passes it to who should see it): who it is from, then its bytes.
inline constexpr std::uint8_t kSnapshotRelayMessage = 33u;
// How often a player sends its own.
inline constexpr std::chrono::milliseconds kSnapshotInterval{33};
// How far behind the latest snapshot the others are drawn: a little over two snapshots, so one may be lost.
inline constexpr std::chrono::milliseconds kSnapshotDelay{100};
// A player whose last snapshot is older than this isn't drawn (they're loading, or their datagrams stopped).
inline constexpr std::chrono::milliseconds kSnapshotStale{1000};

struct PlayerSnapshot {
    std::uint32_t sequence = 0u;  // counted up by the sender: an older one than the last is dropped
    std::string stage;
    int scenario = 0;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};  // degrees
    std::array<float, 3> front{};     // the model's facing and its up (its base matrix's Z and Y), unit length
    std::array<float, 3> up{};
    std::array<float, 3> gravity{};
    std::array<float, 3> velocity{};
    std::string group;      // the animation (XanimePlayer::getCurrentAnimationName)
    std::string animation;  // the bck it plays (getCurrentBckName)
    float frame = 0.0f;
    float rate = 1.0f;
    std::array<float, 4> weights{};  // the animation's tracks' weights (XanimePlayer::mWeights)
    std::uint32_t health = 0u;
    std::uint32_t mode = 0u;
    std::uint32_t flags = 0u;  // NEBULA_COOP_PLAYER_*
    std::uint32_t area = 0u;   // the part of the stage they're in (NebulaCoopPlayer::area): bubbles stay within one
};

// Longest names kept from a snapshot.
inline constexpr std::size_t kSnapshotStageLimit = 47u;
inline constexpr std::size_t kSnapshotAnimationLimit = 47u;

[[nodiscard]] std::vector<std::uint8_t> encode_snapshot(const PlayerSnapshot& snapshot);
// Nothing when it's cut short or not a snapshot.
[[nodiscard]] std::optional<PlayerSnapshot> decode_snapshot(std::span<const std::uint8_t> data);

struct RelayedSnapshot {
    net::PeerId from = net::kNoPeer;
    std::span<const std::uint8_t> snapshot;
};
[[nodiscard]] std::vector<std::uint8_t> encode_relayed_snapshot(net::PeerId from, std::span<const std::uint8_t> snapshot);
[[nodiscard]] std::optional<RelayedSnapshot> decode_relayed_snapshot(std::span<const std::uint8_t> data);

// Who a snapshot from `sender` in an instance goes to (docs/COOP_PLAN.md "Instances"): the room's other players whose
// place is that instance, neither the sender nor this player. Players elsewhere get none, so a room's traffic grows
// with how many share each instance, not with the room's size.
[[nodiscard]] std::vector<net::PeerId> snapshot_audience(std::span<const SessionPlayer> players,
                                                         std::string_view stage, int scenario, net::PeerId sender);

class RemotePlayers;
// The snapshots over a session: `players` gets the others' as they come (a joiner's straight from the host, the other
// joiners' passed on by it), and a host passes each joiner's on to its audience. Set before hosting or joining.
void listen_snapshots(Session& session, RemotePlayers& players);
// This player's snapshot: a host's to each player in its instance, a joiner's to the host alone (it passes it on), so
// a joiner's upload stays one snapshot however many share its instance. False when not in a room.
bool send_snapshot(Session& session, const PlayerSnapshot& snapshot);

// Between two snapshots of the same player: positions and directions along the way, angles the short way round, the
// animation the later one's (its frame moved along when both play the same).
[[nodiscard]] PlayerSnapshot blend_snapshots(const PlayerSnapshot& from, const PlayerSnapshot& to, float t);

struct RemotePlayer {
    net::PeerId id = net::kNoPeer;
    unsigned index = 0u;  // the session's number: 0 the host
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    PlayerSnapshot snapshot;
};

// The other players' snapshots as they come (on the session's thread), and as the game draws them (on its own).
class RemotePlayers {
public:
    using Clock = std::chrono::steady_clock;

    // A snapshot from a player, received at `now`.
    void receive(net::PeerId id, unsigned index, const std::string& name, CoopCharacter character,
                 std::span<const std::uint8_t> data, Clock::time_point now = Clock::now());
    void remove(net::PeerId id);
    void clear();
    // Everyone with a fresh snapshot in the given instance, as at `now` less kSnapshotDelay, by the session's number.
    [[nodiscard]] std::vector<RemotePlayer> sample(const std::string& stage, int scenario,
                                                   Clock::time_point now = Clock::now()) const;

private:
    struct Received {
        Clock::time_point at;
        PlayerSnapshot snapshot;
    };
    struct Player {
        unsigned index = 0u;
        std::string name;
        CoopCharacter character = CoopCharacter::Mario;
        std::deque<Received> received;  // oldest first
    };

    mutable std::mutex lock_;
    std::map<net::PeerId, Player> players_;
};

}  // namespace galaxy::coop
