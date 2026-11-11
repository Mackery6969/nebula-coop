#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Discord Rich Presence over the Discord desktop app's local IPC (the named pipe discord-ipc-N): what the player is
// doing, in the game's own words (its galaxy and star names, in the played language), and in an online room its
// party (how many play, of how many) with a Join button, as sm64coopdx's Discord activity has. A worker thread does
// all of it; while Discord is not running it retries every 15 s and the game never waits on it.
namespace galaxy::discord {

// An online room, as Discord shows it: "(2 of 8)" beside the status, and a Join button when `join_secret` is set.
// Discord hands the secret to a friend who presses Join (or an invite's Join) and to no one else.
struct Party {
    std::string id;           // the same for everyone in the room (party_id); empty: none
    unsigned size = 0u;       // players in the room, this one included; 0: no count
    unsigned max = 0u;        // the room's most
    std::string join_secret;  // join_secret(); empty: no Join button

    bool operator==(const Party&) const = default;
};

struct Activity {
    std::string details;  // first line: the galaxy, or "Choosing a file"
    std::string state;    // second line: the star, or the Power Star count
    Party party;
};

// Once, at start. An empty client id (no Discord application) keeps presence off.
void start(std::string client_id);
// The GENERAL page's DISCORD STATUS, read by the worker each second (off clears the status and closes the pipe).
void set_enabled_provider(bool (*provider)());
// What the player is doing now (its details and state; the party stays); sent when it changes.
void set_activity(const Activity& activity);
// The online room's party, or none (a default Party); sent when it changes.
void set_party(const Party& party);
// How Discord starts Nebula when a friend presses Join with Nebula closed (Windows: registered under
// HKEY_CURRENT_USER\Software\Classes\discord-<client id>, as Discord's own libraries do), once Discord answers. Empty:
// nothing is registered.
void set_launch_command(std::filesystem::path::string_type command);
// A Join pressed in Discord: the room's secret, once (the game's thread asks each frame).
std::optional<std::string> take_join_secret();
// A Join waits to be taken (the title is skipped for it).
bool join_pending();

// A NebulaNet room as a Join button carries it ("nebulanet:<code>:<password>", as sm64coopdx's
// "coopnet:<lobby>:<password>"), and back; nullopt for anything else.
struct JoinRoom {
    std::string code;
    std::string password;
};
std::string join_secret(std::string_view code, std::string_view password);
std::optional<JoinRoom> parse_join_secret(std::string_view secret);
// The party's id for a room code: the same for everyone in the room, without giving the code away.
std::string party_id(std::string_view code);

// The protocol's pieces, for tests: a frame (opcode, length, both little-endian, then the JSON), the handshake, the
// SET_ACTIVITY command (an empty activity clears the status) and the SUBSCRIBE command.
std::string frame(std::uint32_t opcode, std::string_view json);
std::string handshake_json(std::string_view client_id);
// `platforms`: a Join secret goes with supported_platforms ["desktop"], which Discord needs to offer the invite.
std::string set_activity_json(std::uint32_t pid, const Activity& activity, std::int64_t start_seconds,
                              std::uint32_t nonce, bool clear, bool platforms = true);
std::string subscribe_json(std::string_view event, std::uint32_t nonce);
std::string json_string(std::string_view text);
// Takes the first whole frame off the front of `buffer` (what Discord sent); false while none has fully arrived.
bool take_frame(std::string& buffer, std::uint32_t* opcode, std::string* json);
// The string value of `key`'s first appearance in `json`, unescaped; nullopt when it has none.
std::optional<std::string> json_field(std::string_view json, std::string_view key);

}  // namespace galaxy::discord
