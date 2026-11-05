// Online co-op's cheat commands (docs/COOP_PLAN.md, "Chat commands"): chat commands that work only while the room's
// cheats are on (the host's Online tab), and show on /help and Tab only then.
//
// /tp and /teleport are Minecraft's teleport, in a galaxy's units:
//   /tp PLAYER              this player to PLAYER
//   /tp X Y Z               this player to a place
//   /tp TARGET PLAYER       TARGET to PLAYER
//   /tp TARGET X Y Z        TARGET to a place
// A player is a name or a number (/players), @s this player, and a TARGET may be @a, everyone. A coordinate is a
// number, ~ where the moved player is on that axis, or ~ and a number from there (~ ~500 ~ is 500 up the stage's Y).
// A player goes to another only in the same instance (the galaxy and its star): nobody changes stage. Anyone may move
// themselves; the host and moderators may move the others too. /pos says where this player is.
//
// Moving another player is a message: the host's straight to them, a moderator's to the host, which checks it and
// passes it on. The moved player's own game resolves where (its ~ and the player it goes to, as its game sees them)
// and is told who moved it.
#pragma once

#include "galaxy/coop_session.h"
#include "galaxy/net/transport.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

class Chat;

// The session message a teleport of another player goes as (a feature's, galaxy/coop_session.h).
inline constexpr std::uint8_t kTeleportMessage = kFirstFeatureMessage + 17u;

using Position = std::array<float, 3>;

// One axis of a place: a number, or (relative) that far from where the moved player is.
struct Coordinate {
    float value = 0.0f;
    bool relative = false;
};
using Place = std::array<Coordinate, 3>;

// /tp's words, before the players are looked up.
struct TeleportWords {
    std::string target;               // who goes: "" for this player
    std::optional<std::string> player;  // to this player, or
    Place place{};                    // here when there is no player
};

// A coordinate's word ("12.5", "~", "~-3"): nothing when it is none.
[[nodiscard]] std::optional<Coordinate> parse_coordinate(std::string_view word);
// /tp's words as Minecraft reads them (one to four of them): nothing when they aren't a teleport.
[[nodiscard]] std::optional<TeleportWords> parse_teleport(std::string_view words);
// Where `place` is from `from` (its ~ axes counted from there).
[[nodiscard]] Position resolve_place(const Place& place, const Position& from);
// A position as /pos and /tp say it: "12 -3 4500", whole units.
[[nodiscard]] std::string position_text(const Position& position);

// Where a teleport goes: to another player, or to a place.
struct TeleportTo {
    net::PeerId player = net::kNoPeer;  // kNoPeer: `place`
    Place place{};
};
// A kTeleportMessage: who moved the player (the host fills it in for a moderator's), who goes, and where.
struct TeleportMessage {
    net::PeerId by = net::kNoPeer;
    net::PeerId target = net::kNoPeer;
    TeleportTo to;
};
[[nodiscard]] std::vector<std::uint8_t> encode_teleport(const TeleportMessage& message);
[[nodiscard]] std::optional<TeleportMessage> decode_teleport(std::span<const std::uint8_t> data);

// What the cheat commands need of the game. Each may be called on the session's thread as on the game's.
struct CheatHooks {
    // Where this player is, nothing outside a stage.
    std::function<std::optional<Position>()> here;
    // Where another player is as this game sees them: nothing when they aren't in this player's instance.
    std::function<std::optional<Position>(net::PeerId)> where;
    // The game moves this player to `position`.
    std::function<void(const Position&)> move;
};

// The cheat commands (/tp, /teleport, /pos) on `chat`'s, over `session`, and the session's kTeleportMessage. Once,
// with the chat attached and before hosting or joining.
void add_cheat_commands(Chat& chat, Session& session, CheatHooks hooks);

}  // namespace galaxy::coop
