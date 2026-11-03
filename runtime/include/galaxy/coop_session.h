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
#pragma once

#include "galaxy/coop_mods.h"
#include "galaxy/net/transport.h"
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

// The session's own protocol (its messages, below the transport's): a different one refuses the join.
inline constexpr std::uint16_t kSessionProtocol = 5u;
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
};

// How far a join is (Session::join_step), for the joining screen.
enum class JoinStep : std::uint8_t {
    None,             // not joining
    Reaching,         // reaching the host's lobby
    Asking,           // in its lobby, waiting for the host to let this player in
    DownloadingMods,  // getting the room's mods it lacks (download_progress())
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

    [[nodiscard]] SessionState state() const;
    [[nodiscard]] bool active() const { return state() != SessionState::Idle; }
    [[nodiscard]] bool is_host() const;
    // Everyone in the room, by number (this player marked local).
    [[nodiscard]] std::vector<SessionPlayer> players() const;
    // The room's settings: the host's (as a joiner got them).
    [[nodiscard]] RuntimeCoopSettings room() const;
    // The port a host listens on.
    [[nodiscard]] std::uint16_t port() const;
    // The host changes the room's settings for everyone.
    void set_room(const RuntimeCoopSettings& room);
    // Where this player plays (the player list's galaxy and star), told to everyone as it changes. Kept while there
    // is no room too: a room opened or joined later starts with it.
    void set_place(const std::string& stage, int scenario);
    // This player's name (Options' PLAYER page's, cleaned as at a join), told to everyone as it changes: their player
    // list, nametags and chat show it from then on, as sm64coopdx's player settings do. Nothing outside a room; the
    // character stays the one played since the join.
    void set_name(const std::string& name);
    // The host: `player` got a Power Star new to the room's save (the shared world settled it): their count goes up
    // and everyone is told. Nothing for a joiner or outside a room; each room starts from 0.
    void credit_star(net::PeerId player);
    // This player's Power Stars in this room (0 outside one).
    [[nodiscard]] unsigned stars() const;

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
