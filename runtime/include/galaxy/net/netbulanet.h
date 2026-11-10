// NebulaNet: a signaling service so friends join a room by a code with no port forwarding.
// The room still runs on the host's PC and play goes straight between players; NebulaNet only introduces them.
//
// A host creates a room (create_room): it sends its addresses (candidates) and gets a 6-char room code back.
// A joiner joins a room (join_room): it sends the code and its own candidates, and gets the host's candidates back.
// Both then punch through their routers directly. The host keeps the room alive until it closes it.
//
// This is a thin HTTP client over the NebulaNet protocol (see nebulanet/README.md for the protocol details).

#pragma once

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::netbulanet {

// A candidate address for NAT traversal, as sent to NebulaNet.
struct Candidate {
    std::string type;    // "stun" or "host"
    std::string address; // "ip:port"
};

// What create_room returns: the room code and what to display to players.
struct RoomInfo {
    std::string code;  // 6 uppercase letters/digits
    std::string server;
};

// What join_room returns: the host's candidates so this player can punch.
struct JoinInfo {
    std::string name;
    std::string game;
    std::string version;
    unsigned players = 0u;
    unsigned max = 0u;
    std::vector<Candidate> candidates;
};

// Errors from NebulaNet calls.
enum class Error {
    None,
    Network,      // could not reach the server
    Server,       // server returned an error
    Taken,        // room code collision (only on create)
    NotFound,     // code not found (only on join)
    Version,      // game version mismatch (joiner vs host)
    Timeout,      // operation timed out
};

// Create a room on the NebulaNet server. Returns RoomInfo on success, or Error.
// `candidates` are this host's addresses (from stun or local interfaces).
// `on_progress` is called periodically with a status message (may be empty).
std::future<std::variant<RoomInfo, Error>> create_room(
    std::string_view server,
    std::string_view game,
    std::string_view version,
    std::string_view room_name,
    unsigned players,
    unsigned max_players,
    const std::vector<Candidate>& candidates,
    std::function<void(std::string)> on_progress = nullptr
);

// Close a room on the NebulaNet server (the host calls this when the room ends).
// Returns Error::None on success.
std::future<Error> close_room(std::string_view server, std::string_view code);

// A joiner looks up a room by code. Returns JoinInfo or Error.
std::future<std::variant<JoinInfo, Error>> join_room(
    std::string_view server,
    std::string_view code,
    const std::vector<Candidate>& candidates,
    std::function<void(std::string)> on_progress = nullptr
);

// Sanitize a NebulaNet server URL (ensure it has a scheme).
[[nodiscard]] std::string normalize_server_url(std::string_view server) noexcept;

}  // namespace galaxy::netbulanet
