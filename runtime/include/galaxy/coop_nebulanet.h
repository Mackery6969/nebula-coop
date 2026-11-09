// NebulaNet's side of a room (docs/COOP_PLAN.md, "Hosting"; the server is the NebulaNet repository): a host keeps a
// WebSocket open to it, gets a room code and hears of each joiner's addresses; a joiner asks for a code and gets the
// host's. The session then punches through both routers with the direct transport. NebulaNet never carries game
// traffic.
//
// The web calls block, so each link runs on a thread of its own; the session takes what happened (take_events) on
// its own thread. stop() returns at once: a call still running finishes on its own and is thrown away.
#pragma once

#include "galaxy/net/udp.h"
#include "galaxy/net/web.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

// The NebulaNet games use unless the coop_nebulanet setting names another.
inline constexpr const char* kNebulaNetServer = "https://nebulanet.mackery.com";
// Room codes: 6 letters and digits, without 0, O, 1 or I.
inline constexpr std::string_view kRoomCodeLetters = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
inline constexpr std::size_t kRoomCodeLength = 6u;

// A code as a player typed it ("k7qx-2m"), upper case without spaces or dashes; nothing when it can't be one.
[[nodiscard]] std::optional<std::string> parse_room_code(std::string_view text);
// An address as NebulaNet passes it: "1.2.3.4:5678" or "[2001:db8::1]:5678".
[[nodiscard]] std::string candidate_text(const net::Address& address);
// The other way (numbers only, no names looked up).
[[nodiscard]] bool parse_candidate(std::string_view text, net::Address& address);

struct NebulaNetRoom {
    std::string name;     // the host's
    std::string version;  // the game's
    unsigned players = 1u;
    unsigned max = 8u;
    std::vector<std::string> candidates;  // the host's addresses
};

struct NebulaNetEvent {
    enum class Kind {
        RoomCode,  // a host's room is listed: text is its code (again after NebulaNet was lost and found)
        Joiner,    // someone is joining a host's room: candidates are their addresses
        Answer,    // a joiner's: the room it asked for (room)
        Failed,    // text: why, in the menu's words. A host's link tries again by itself; a joiner's has ended
    };
    Kind kind = Kind::Failed;
    std::string text;
    std::vector<std::string> candidates;
    NebulaNetRoom room;
};

class NebulaNetLink {
public:
    explicit NebulaNetLink(std::shared_ptr<net::Web> web);
    ~NebulaNetLink();
    NebulaNetLink(const NebulaNetLink&) = delete;
    NebulaNetLink& operator=(const NebulaNetLink&) = delete;

    // Lists a host's room on `server` ("https://host") and keeps it listed until stop(), getting it back (by the
    // same code) when the connection drops.
    void host(std::string server, NebulaNetRoom room);
    // A host's: the room's player count changed.
    void set_players(unsigned players);
    // Asks `server` for the room `code`, telling it this joiner's addresses.
    void join(std::string server, std::string code, std::vector<std::string> candidates);
    void stop();
    [[nodiscard]] std::vector<NebulaNetEvent> take_events();

    struct Shared;

private:
    std::shared_ptr<net::Web> web_;
    std::shared_ptr<Shared> shared_;
};

}  // namespace galaxy::coop
