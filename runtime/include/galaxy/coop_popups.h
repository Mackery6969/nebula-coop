// Online co-op's pop-ups (docs/COOP_PLAN.md, "Pop-ups"), as sm64coopdx's: a small box in the screen's top-right corner
// for a few seconds when another player joins or leaves the room, goes to the Observatory or a galaxy, comes to (or
// leaves) this player's galaxy, dies, or gets a Power Star. Each kind can be turned off on Options' PLAYER page
// (RuntimeSettings::coop_popups); one turned off is never made.
//
// The host makes them by comparing the room's players (Session::players) from one call to the next, so nothing new
// travels: the session already carries who is in the room, where each plays and their stars. Deaths come from the
// snapshots of the players in this player's instance (their NEBULA_COOP_PLAYER_DEAD), as sm64coopdx tells of a death
// only in the same level. The game draws what take() gives it (NebulaCoopPopups). Thread-safe.
#pragma once

#include "galaxy/coop_session.h"
#include "galaxy/runtime_settings.h"

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

// A pop-up waits at most this long for the game to show it (outside a game scene, a star select's), then is dropped:
// news that late would only confuse.
inline constexpr std::uint64_t kPopupWaitMs = 6000u;
// The most that wait; past it the oldest go.
inline constexpr std::size_t kPopupWaitLimit = 16u;

struct Popup {
    enum class Kind : std::uint8_t {
        Joined,       // `name` joined the room
        Left,         // `name` left it
        Observatory,  // `name` went to the Comet Observatory
        Galaxy,       // `name` went to `stage`, its star `scenario`
        EnteredHere,  // `name` came to this player's galaxy and star
        LeftHere,     // `name` left them
        Died,         // `name`, in this player's instance, died
        Star,         // `name` got a Power Star new to the room's save, in `stage`
    };
    Kind kind = Kind::Joined;
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    std::string stage;
    int scenario = 0;
    std::uint64_t at_ms = 0u;
};

// The Comet Observatory's stages (the Observatory, the domes, the library): going between them is staying put.
[[nodiscard]] bool observatory_stage(std::string_view stage);

// Whether a pop-up of `kind` is made with `options` (the PLAYER page's).
[[nodiscard]] bool popup_wanted(Popup::Kind kind, const RuntimeCoopPopups& options);

// A player in this player's instance as their snapshot shows them now (svc_coop_remote's sample).
struct InstancePlayer {
    net::PeerId id = net::kNoPeer;
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    bool dead = false;
};

class Popups {
public:
    // The kinds to make (the PLAYER page's), from now on.
    void set_options(const RuntimeCoopPopups& options);

    // The room's players now (Session::players(), this player marked local), each frame: who joined, left, went
    // somewhere or got a star since the last call. The room this player just joined (or hosted) is taken as it is,
    // with no pop-ups for who was already there; no players (no room) forgets everyone without any.
    void update(const std::vector<SessionPlayer>& players, std::uint64_t now_ms);
    // The other players in this player's instance now: who died since the last call. One who wasn't in the instance
    // at the last call is taken as they are (they may come in dead).
    void update_instance(std::span<const InstancePlayer> here, std::uint64_t now_ms);

    // The oldest pop-up waiting no longer than kPopupWaitMs, for the game to show; none when none waits.
    [[nodiscard]] std::optional<Popup> take(std::uint64_t now_ms);
    // The room ended: no pop-ups, no players known.
    void clear();

private:
    struct Known {
        std::string name;
        CoopCharacter character = CoopCharacter::Mario;
        std::string stage;
        int scenario = 0;
        unsigned stars = 0u;
    };
    void add(Popup popup);

    std::mutex lock_;
    RuntimeCoopPopups options_{};
    bool in_room_ = false;
    std::map<net::PeerId, Known> known_;
    std::map<net::PeerId, bool> dead_;  // the instance's players at the last call, and whether they were dead
    std::deque<Popup> waiting_;
};

}  // namespace galaxy::coop
