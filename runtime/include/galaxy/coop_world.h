// Online co-op's shared world (docs/COOP_PLAN.md "Instances" and "Shared world state"): the server decides
// everything but the visuals. Each instance (a stage and its scenario, sm64coopdx's level and act) has a record of
// the placed things gone from it (coins and star bits taken, items picked up, enemies defeated), and the session has
// one save (Power Stars, event flags, event values, achievements). A player reports what its own game did; the host settles it
// (first come wins, a thing gone stays gone) and tells the players it concerns: the instance's for what is gone, the
// room for the save. A player whose scene starts gets the instance's record first (sm64coopdx's
// packet_level_area_request / packet_level_area_inform); an instance nobody else is in starts fresh from the stage
// files, as sm64coopdx's level does once everyone left it.
//
// WorldAuthority is the host's bookkeeping alone (no network, for tests and a dedicated server later); World runs it
// over a Session: the host's own game talks to it directly, a joiner's through the session's messages.
#pragma once

#include "galaxy/net/transport.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::coop {

class Session;

// The session's message types the world uses (galaxy/coop_session.h: features from 32; snapshots take 32 and 33).
inline constexpr std::uint8_t kWorldEnterMessage = 34u;   // player to host: its scene in an instance is ready
inline constexpr std::uint8_t kWorldGoneMessage = 35u;    // player to host: things gone; host to players: the same
inline constexpr std::uint8_t kWorldReadyMessage = 36u;   // host to player: the instance's record has all come
inline constexpr std::uint8_t kSaveChangeMessage = 37u;   // player to host: a save change; host to all: accepted ones
inline constexpr std::uint8_t kSaveRequestMessage = 38u;  // joiner to host, once let in: the session's save changes
inline constexpr std::uint8_t kStarGotMessage = 39u;      // player to host: it took a star; host to its instance: the same

// Most things an instance's record keeps, and the most save changes a session keeps: a player can't grow the
// host's memory past them.
inline constexpr std::size_t kMaxGonePerInstance = 16384u;
inline constexpr std::size_t kMaxSaveChanges = 8192u;
// Longest names in a save change (the game's stage names and event names are shorter).
inline constexpr std::size_t kSaveNameLimit = 47u;

struct Instance {
    std::string stage;
    int scenario = 0;
    friend bool operator==(const Instance&, const Instance&) = default;
    friend auto operator<=>(const Instance&, const Instance&) = default;
};

// A placed thing gone from an instance: the game's key for it (its placement, the same in every player's game) and
// how (NEBULA_COOP_GONE_*: a coin taken counts for everyone in the instance).
struct Gone {
    std::uint32_t object = 0u;
    std::uint32_t how = 0u;
};

enum class SaveKind : std::uint8_t {
    Star = 1,   // a Power Star: name the galaxy, scenario the star
    Flag = 2,   // an event flag on
    Value = 3,  // an event value: name, value
    // An achievement unlocked in the save (galaxy::achievements' id as name; value when, in seconds since 1970 UTC):
    // every player's who plays it. The first unlock's time is the save's.
    Achievement = 4,
};

struct SaveChange {
    SaveKind kind = SaveKind::Star;
    std::string name;
    int scenario = 0;
    std::uint32_t value = 0u;
    friend bool operator==(const SaveChange&, const SaveChange&) = default;
};

[[nodiscard]] std::vector<std::uint8_t> encode_instance(const Instance& instance);
[[nodiscard]] std::optional<Instance> decode_instance(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_gone(std::span<const Gone> gone);
[[nodiscard]] std::optional<std::vector<Gone>> decode_gone(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_save_changes(std::span<const SaveChange> changes);
[[nodiscard]] std::optional<std::vector<SaveChange>> decode_save_changes(std::span<const std::uint8_t> data);

// The host's record of the world, by player (their peer ids). Not thread-safe.
class WorldAuthority {
public:
    using Player = net::PeerId;

    // A player's scene in `instance` is ready: what is gone there, for that player to remove. An instance nobody
    // else is in starts fresh first (its record dropped).
    std::vector<Gone> enter(Player player, const Instance& instance);
    // A player left the room: an instance it leaves empty is forgotten.
    void leave(Player player);
    // A player's things gone from its instance: the ones that are news (first come wins), and the other players in
    // that instance to tell. Nothing for a player in no instance.
    struct Settled {
        std::vector<Gone> news;
        std::vector<Player> tell;
    };
    Settled gone(Player player, std::span<const Gone> gone);
    // A player took a star (its get demo began): the other players in its instance, to tell (sm64coopdx's
    // collect_star reaching the level). Nobody for a player in no instance.
    [[nodiscard]] std::vector<Player> star_got(Player player) const;
    // A change to the session's save: true when it is news (a star, flag or achievement not had yet, a value
    // changed), now in save_changes() for everyone. Event values are the host's game's alone (`from_host`): each
    // player's game keeps its own count of misses, comets and the like, and the host's is the session's.
    bool save(const SaveChange& change, bool from_host);

    [[nodiscard]] const std::vector<SaveChange>& save_changes() const { return save_; }
    [[nodiscard]] std::optional<Instance> instance_of(Player player) const;
    [[nodiscard]] std::vector<Player> players_in(const Instance& instance) const;
    [[nodiscard]] std::size_t instance_count() const { return instances_.size(); }
    void clear();

private:
    struct Record {
        std::map<std::uint32_t, std::uint32_t> gone;  // object, how
        std::set<Player> players;
    };
    void drop_player(Player player);

    std::map<Instance, Record> instances_;
    std::map<Player, Instance> where_;
    std::vector<SaveChange> save_;
};

// A star another player in the instance took: the star's number in the stage (its scenario) and whether it is a
// Grand Star (docs/COOP_PLAN.md "Cutscenes and transitions": a Grand Star takes everyone in the instance along).
struct StarGot {
    int star = 0;
    bool grand = false;
    friend bool operator==(const StarGot&, const StarGot&) = default;
};

// What the game is told (World::take): things gone from its instance, the record's end, save changes, and stars
// others in the instance took.
struct WorldEvent {
    enum class Kind : std::uint8_t { Gone, Ready, Save, StarGot };
    Kind kind = Kind::Gone;
    Gone gone;
    SaveChange save;
    StarGot star;
};

// The world over a session, for this player's game. The game's calls (enter, report_gone, report_save, take) come
// from its thread; the session's messages on the session's.
class World {
public:
    // Listens for the world's messages on `session` (set before hosting or joining, as the session's handlers are).
    void attach(Session& session);
    // The room opened or this player was let in: a fresh record (a joiner then asks for the session's save).
    void start();
    // The room ended for this player.
    void stop();
    // Each game frame: players who left the room leave the record.
    void update();

    // The game's scene in an instance is ready: its record comes (then Ready), and the session's save again, for the
    // game to apply what it doesn't have (a scene started over may have dropped changes made since it began).
    void enter(const Instance& instance);
    void report_gone(const Gone& gone);
    void report_save(const SaveChange& change);
    // The game's player took a star in the instance it entered: the others there are told.
    void report_star(const StarGot& star);
    [[nodiscard]] std::vector<WorldEvent> take();
    // The session's achievements as they come (Achievement save changes, which the game isn't told through take):
    // for the host's runtime to keep in the save's achievements.ini, a joiner's in its copy. Each scene start doesn't
    // give them again; a joiner gets the session's once, as it is let in.
    [[nodiscard]] std::vector<SaveChange> take_achievements();

    [[nodiscard]] bool active() const;
    // The host's record (tests).
    [[nodiscard]] std::size_t instance_count() const;

private:
    [[nodiscard]] net::PeerId host_peer() const;
    [[nodiscard]] net::PeerId self_peer() const;
    void push(WorldEvent event);
    void remember_save(const SaveChange& change);
    void push_save(const SaveChange& change);
    void host_gone(net::PeerId from, std::span<const Gone> gone);
    void host_save(net::PeerId from, const SaveChange& change);
    // The others in `from`'s instance told of its star: the host's own game here, the rest as messages to send once
    // the lock is let go.
    [[nodiscard]] std::vector<std::pair<net::PeerId, std::vector<std::uint8_t>>> host_star(net::PeerId from,
                                                                                           const StarGot& star);

    Session* session_ = nullptr;
    mutable std::recursive_mutex lock_;
    bool active_ = false;
    bool host_ = false;
    WorldAuthority authority_;           // the host's
    std::vector<SaveChange> save_;       // the session's save as this player knows it
    std::optional<Instance> entered_;    // the instance the game last entered
    std::vector<WorldEvent> events_;     // for the game
    std::vector<SaveChange> achievements_;  // for the runtime (take_achievements)
    std::vector<net::PeerId> members_;   // the host's players at the last update
    // Each scene this player enters is counted (a joiner's), and a host keeps each player's: what comes for or from
    // an earlier scene is dropped.
    std::uint16_t epoch_ = 0u;
    std::map<net::PeerId, std::uint16_t> epochs_;
};

}  // namespace galaxy::coop
