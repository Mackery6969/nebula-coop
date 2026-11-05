// Online co-op's session (docs/COOP_PLAN.md, "Hosting"): the room as the game sees it, over a transport
// (galaxy/net/transport.h). Host opens it on a port; Join connects to address:port, asks the host to let this player
// in and gets the room (its settings, the host's save and who is playing); the pause menu's Quit leaves it, or closes
// it for everyone when the host leaves. As in sm64coopdx, the host is player 0 and the others get the lowest free
// number.
//
// A session runs on a thread of its own (the network keeps going while the game loads or a menu is open). Everything
// here may be called from any thread; what happened (a player joined, the join refused, the room closed) waits in
// take_events() for the game's thread. Later features (snapshots, chat) send their own messages with send() and
// receive them with on_message().
//
// The join is checked as sm64coopdx's is: the same session protocol and game version, the same disc
// (galaxy/coop_mods.h) and the room's mods. A joiner playing other mods isn't let in yet: the host sends the room's
// list, the joiner downloads the ones it lacks from the host (before it is a player, so nobody sees it), and the
// session ends with ModsReady for Nebula to start again with those mods and join once more.
//
// The host removes players as sm64coopdx's /kick, /ban and /permban do (galaxy/coop_commands.h), and makes moderators
// who may kick and ban too. Bans and moderators go by who a player is beyond the room (the transport's peer_identity:
// an IP address), so they hold when the player joins again.
#pragma once

#include "galaxy/coop_mods.h"
#include "galaxy/net/stun.h"
#include "galaxy/net/transport.h"
#include "galaxy/net/web.h"
#include "galaxy/runtime_settings.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::coop {

// The join request's own layout (its first fields: this, then the game's version). A change to the session's other
// messages needs nothing here: the game's version is the build's co-op fingerprint (cmake/nebula_fingerprint.cmake),
// so builds that differ refuse each other by it. Only a change to how a join request starts changes this.
inline constexpr std::uint16_t kSessionProtocol = 8u;
// The first message type a feature may use with send() and on_message(); below are the session's.
inline constexpr std::uint8_t kFirstFeatureMessage = 32u;
// The host's save, sent at a join: this many bytes at most.
inline constexpr std::size_t kMaxSaveBytes = 8u << 20;

struct SessionPlayer {
    net::PeerId id = net::kNoPeer;
    unsigned index = 0u;  // 0 the host, then 1, 2... as they joined
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    bool host = false;
    bool local = false;
    // Where they play (set_place): the stage's name and its scenario; empty until they reach one.
    std::string stage;
    int scenario = 0;
    // The Power Stars they got in this room, new to its save (credit_star): sm64coopdx's Personal Star Counter.
    unsigned stars = 0u;
    // The round trip to them (to the host, for a joiner's view of the others), 0 for this player or when unknown.
    unsigned ping_ms = 0u;
    // The host made them a moderator (make_moderator): they may kick and ban.
    bool moderator = false;
};

// How the host removes a player (remove_player), as sm64coopdx's /kick, /ban and /permban.
enum class Removal : std::uint8_t {
    Kick,          // out of the room; they may join again
    Ban,           // and refused while this room is open
    PermanentBan,  // and refused in every room this player hosts (kept: KeepBan)
};

// A file of the save the room plays (its folder's GameData.bin...), by name.
struct SaveFile {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

// The host's save as a joiner plays it: the user file to play and the folder's files.
struct RoomSave {
    unsigned file = 1u;
    std::vector<SaveFile> files;
};

enum class SessionState : std::uint8_t {
    Idle,
    Hosting,
    Connecting,  // reaching the host's lobby, then waiting for it to let this player in
    Joined,
};

struct SessionEvent {
    enum class Kind : std::uint8_t {
        Hosting,       // the room is open (text: the port)
        Joined,        // the host let this player in: take_room_save() holds the save
        JoinFailed,    // the join didn't work (text: why)
        Closed,        // the room ended for this player (text: why; empty when the player left)
        PlayerJoined,  // name: who
        PlayerLeft,    // name: who; text: why when they stopped answering
        RoomChanged,   // the host changed the room's settings (room())
        DownloadingMods,  // a joiner gets the room's mods it lacks (text: their ids); download_progress() says how far
        ModsReady,     // a joiner has all the room's mods (take_room_mods()); the session ended, for Nebula to start
                       // again with them and join again
        KeepBan,       // the host: `name` is banned from every room it hosts; keep text (who they are) with the
                       // others (HostConfig::bans)
        KeepModerator, // the host: `name` is made a moderator of every room it hosts; keep text likewise
                       // (HostConfig::moderators)
        Moderator,     // this player: the host made it a moderator
        RoomCode,      // the host: NebulaNet listed the room; text: the code friends join with
        NebulaNetLost, // the host: NebulaNet can't list the room now (text: why); it tries again, and friends can
                       // still join by address
    };
    Kind kind = Kind::Hosting;
    std::string name;
    std::string text;
};

struct HostConfig {
    std::uint16_t port = kCoopDefaultPort;  // 0: any (tests)
    RuntimeCoopSettings room;               // the room's settings, the password and max players among them
    std::string game_version;               // joiners with another are refused
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    // The save to send each joiner, read as they join (on the session's thread).
    std::function<RoomSave()> save;
    DiscInfo disc;                 // joiners with another are refused
    std::vector<SharedMod> mods;   // the room's mods, with their files and folders: joiners get the ones they lack
    // Who is refused, and who is a moderator, in every room this player hosts (peer identities, kept with the
    // player's settings).
    std::vector<std::string> bans;
    std::vector<std::string> moderators;
    // NebulaNet (galaxy/coop_nebulanet.h) to list the room on, so friends join by its code with no port forwarded;
    // empty: by address only. The STUN server tells NebulaNet the room's public address.
    std::string nebulanet;
    std::string stun = net::stun::kDefaultServer;
    std::uint16_t stun_port = net::stun::kDefaultPort;
};

struct JoinConfig {
    std::string address;
    std::uint16_t port = kCoopDefaultPort;
    std::string password;
    std::string game_version;
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;
    DiscInfo disc;
    std::vector<SharedMod> mods;           // the mods this game plays now (their files aren't needed)
    std::filesystem::path mods_cache;      // where the room's mods download (ModCache), and are looked for first
    // A copy of a room's mod this player has (its own mods folder's), checked against the mod's files and hash.
    std::function<std::optional<std::filesystem::path>(const SharedMod&)> find_mod;
    // A room code instead of the address: NebulaNet (this server) gives the host's addresses and tells the host to
    // punch through to this player.
    std::string code;
    std::string nebulanet;
    std::string stun = net::stun::kDefaultServer;
    std::uint16_t stun_port = net::stun::kDefaultPort;
};

// How far a join is (Session::join_step), for the joining screen.
enum class JoinStep : std::uint8_t {
    None,             // not joining
    Reaching,         // reaching the host's lobby
    Asking,           // in its lobby, waiting for the host to let this player in
    DownloadingMods,  // getting the room's mods it lacks (download_progress())
    FindingRoom,      // asking NebulaNet where the room of its code is
};

// A joiner's download of the room's mods.
struct DownloadProgress {
    bool active = false;
    std::uint64_t done = 0u;   // bytes
    std::uint64_t total = 0u;
    std::string mod;           // the one downloading
};

class Session {
public:
    using Handler = std::function<void(const SessionPlayer& from, std::span<const std::uint8_t> data)>;

    // `transport`: none for the direct one (galaxy/net/direct_transport.h).
    explicit Session(std::unique_ptr<net::Transport> transport = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Opens the room. False when the port can't be opened (taken, no network).
    bool host(HostConfig config);
    // Starts joining; how it went comes as an event (Joined or JoinFailed). False when it can't start at all.
    bool join(JoinConfig config);
    // Leaves, or closes the room as its host. Nothing happens when there is no session.
    void leave();
    // As leave(), but only when no other thread is inside the session now: a crashing game's last word to the room
    // (the thread that crashed may hold the session), so the others leave at once instead of waiting for the room to
    // stop answering. False when it couldn't.
    bool leave_if_free();

    [[nodiscard]] SessionState state() const;
    [[nodiscard]] bool active() const { return state() != SessionState::Idle; }
    [[nodiscard]] bool is_host() const;
    // Everyone in the room, by number (this player marked local).
    [[nodiscard]] std::vector<SessionPlayer> players() const;
    // The room's settings: the host's (as a joiner got them).
    [[nodiscard]] RuntimeCoopSettings room() const;
    // The port a host listens on.
    [[nodiscard]] std::uint16_t port() const;
    // A host's room code on NebulaNet (HostConfig::nebulanet), once it has one; empty otherwise.
    [[nodiscard]] std::string room_code() const;
    // How NebulaNet is reached (tests give their own); the platform's (galaxy/net/web.h) when none is set.
    void set_web(std::shared_ptr<net::Web> web);
    // The host changes the room's settings for everyone.
    void set_room(const RuntimeCoopSettings& room);
    // Where this player plays (the player list's galaxy and star), told to everyone as it changes. Kept while there
    // is no room too: a room opened or joined later starts with it.
    void set_place(const std::string& stage, int scenario);
    // This player's name (Options' PLAYER page's, cleaned as at a join), told to everyone as it changes: their player
    // list, nametags and chat show it from then on, as sm64coopdx's player settings do. A name another player has (in
    // any case) gets a number, "MARIO (2)", as at a join (sm64ex-coop's); calling again with the same name keeps the
    // number. Nothing outside a room.
    void set_name(const std::string& name);
    // The character this player plays now (sm64coopdx's live character change), told to everyone as it changes:
    // their player list, nametags and chat colours follow. Nothing outside a room.
    void set_character(CoopCharacter character);
    // The host: `player` got a Power Star new to the room's save (the shared world settled it): their count goes up
    // and everyone is told. Nothing for a joiner or outside a room; each room starts from 0.
    void credit_star(net::PeerId player);
    // This player's Power Stars in this room (0 outside one).
    [[nodiscard]] unsigned stars() const;
    // Removes `player` from the room: the host does it, a moderator asks the host to (a Kick or a Ban, not a
    // PermanentBan). False when this player may not, or `player` is no one else in the room (or is the host).
    bool remove_player(net::PeerId player, Removal how);
    // The host makes `player` a moderator, of every room it hosts (KeepModerator). False when it can't.
    bool make_moderator(net::PeerId player);
    // This player is a moderator of the room.
    [[nodiscard]] bool is_moderator() const;

    [[nodiscard]] std::vector<SessionEvent> take_events();
    // Once, after Joined: the host's save.
    [[nodiscard]] std::optional<RoomSave> take_room_save();
    // While a joiner connects: what it waits for.
    [[nodiscard]] JoinStep join_step() const;
    // While a joiner downloads the room's mods: how far.
    [[nodiscard]] DownloadProgress download_progress() const;
    // Once, after ModsReady: the room's mods, in its order, each with its folder here.
    [[nodiscard]] std::optional<std::vector<SharedMod>> take_room_mods();

    // A feature's message (`type` from kFirstFeatureMessage), to a player or net::kEveryone. False when not in a room
    // or too long for the channel (its type byte counts).
    bool send(std::uint8_t type, net::Channel channel, std::span<const std::uint8_t> data,
              net::PeerId to = net::kEveryone);
    // What runs (on the session's thread) when a feature's message comes. Set before hosting or joining.
    void on_message(std::uint8_t type, Handler handler);

private:
    struct State;

    void run();
    void start_thread();
    void stop_thread();

    std::unique_ptr<State> state_;
    mutable std::recursive_mutex lock_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace galaxy::coop
