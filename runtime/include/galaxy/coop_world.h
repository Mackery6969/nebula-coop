// Online co-op's shared world (docs/COOP_PLAN.md "Instances" and "Shared world state"): the server decides
// everything but the visuals. Each instance (a stage and its scenario, sm64coopdx's level and act) has a record of
// the placed things gone from it (coins and star bits taken, items picked up, enemies defeated), and the session has
// one save (Power Stars, event flags, event values, achievements). A player reports what its own game did; the host settles it
// (first come wins, a thing gone stays gone) and tells the players it concerns: the instance's for what is gone, the
// room for the save. A player whose scene starts gets the instance's record first (sm64coopdx's
// packet_level_area_request / packet_level_area_inform); an instance nobody else is in starts fresh from the stage
// files, as sm64coopdx's level does once everyone left it. A player whose Mario misses stops keeping its instance
// going there and then (sm64coopdx's death warp takes a player out of the level), so an instance starts fresh only
// once everyone in it missed or left, and a player whose scene starts over after a miss finds it as the others left it.
//
// WorldAuthority is the host's bookkeeping alone (no network, for tests and a dedicated server later); World runs it
// over a Session: the host's own game talks to it directly, a joiner's through the session's messages.
#pragma once

#include "galaxy/net/transport.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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
// Shared objects (docs/COOP_PLAN.md "Shared objects", sm64coopdx's sync_object.c and packet_object.c):
inline constexpr std::uint8_t kObjectStateMessage = 40u;  // unreliable. Player to host: the objects it moves; host to its
                                                          // instance: the ones it let that player move
inline constexpr std::uint8_t kObjectHitMessage = 41u;    // player to host: its player hit an object; host to its
                                                          // instance: the same, for each game to play it again
inline constexpr std::uint8_t kWorldSwitchMessage = 42u;  // player to host: switches turned; host to its instance (and
                                                          // the instance's record, at its enter): the same
inline constexpr std::uint8_t kWorldMissMessage = 43u;    // player to host: its player missed: the instance goes on
                                                          // without it until its scene starts over

// Most things an instance's record keeps, and the most save changes a session keeps: a player can't grow the
// host's memory past them.
inline constexpr std::size_t kMaxGonePerInstance = 16384u;
inline constexpr std::size_t kMaxSaveChanges = 8192u;
// Most switches an instance's record keeps.
inline constexpr std::size_t kMaxSwitchesPerInstance = 4096u;
// An object's mover that sent nothing for this long has let it go: the next player near it may move it.
inline constexpr std::uint32_t kObjectOwnerSilenceMs = 500u;
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

// An object one player's game moves (its owner, sm64coopdx's sync object owner: the player nearest it), as the others'
// games follow it: the object's key (the same placement key as Gone's), the state its game runs (the nerve's id and
// its step), where it is and how fast it goes, how it is turned (its model's, a quaternion x y z w), and how far its
// owner's player is from it. A player takes an object over by sending its state; the host lets the nearer player have
// it (see WorldAuthority::claim).
struct ObjectState {
    std::uint32_t object = 0u;
    std::uint32_t nerve = 0u;
    std::uint16_t step = 0u;
    std::uint8_t flags = 0u;  // kObject*
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
    unsigned player = 0u;  // to the game: whose (their number, 1 the host)
};

// The owner's player just hit it: it takes it over whatever the distance (sm64coopdx's reliable update from who
// interacted with it).
inline constexpr std::uint8_t kObjectHitClaim = 1u << 0;
// Its nerve began with that hit: the others get it by playing the hit again (ObjectHit), not from the state.
inline constexpr std::uint8_t kObjectHitNerve = 1u << 1;
// How many states one message carries at most (a datagram's worth).
inline constexpr std::size_t kObjectStatesPerMessage = 24u;

// A player's own hit on an object (a stomp, a spin, a ground pound...): the object's key, the game's message for it
// (ACTMES_*) and which of the object's sensors it reached. Each other game in the instance plays it again from that
// player's copy, so the object reacts (squashed, knocked away, pressed) in every game as in the hitter's.
struct ObjectHit {
    std::uint32_t object = 0u;
    std::uint16_t message = 0u;
    std::uint8_t sensor = 0u;
    unsigned player = 0u;  // to the game: who hit it
    friend bool operator==(const ObjectHit&, const ObjectHit&) = default;
};

// A stage switch turned (the game's zone and switch number in `id`): an instance's switches are its players' together.
struct SwitchChange {
    std::uint32_t id = 0u;
    bool on = false;
    friend bool operator==(const SwitchChange&, const SwitchChange&) = default;
};

[[nodiscard]] std::vector<std::uint8_t> encode_instance(const Instance& instance);
[[nodiscard]] std::optional<Instance> decode_instance(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_gone(std::span<const Gone> gone);
[[nodiscard]] std::optional<std::vector<Gone>> decode_gone(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_save_changes(std::span<const SaveChange> changes);
[[nodiscard]] std::optional<std::vector<SaveChange>> decode_save_changes(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_object_states(std::span<const ObjectState> states);
[[nodiscard]] std::optional<std::vector<ObjectState>> decode_object_states(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode_switches(std::span<const SwitchChange> switches);
[[nodiscard]] std::optional<std::vector<SwitchChange>> decode_switches(std::span<const std::uint8_t> data);

// The host's record of the world, by player (their peer ids). Not thread-safe.
class WorldAuthority {
public:
    using Player = net::PeerId;

    // A player's scene in `instance` is ready: what is gone there, for that player to remove. An instance nobody
    // else is in, or only players whose miss goes on, starts fresh first (its record dropped).
    std::vector<Gone> enter(Player player, const Instance& instance);
    // A player left the room: an instance it leaves empty is forgotten.
    void leave(Player player);
    // A player's miss went on (no bubble saved it): until its scene starts over and enters again it doesn't keep its
    // instance going (an instance whose players all missed starts fresh at the next enter, as sm64coopdx's level once
    // everyone death-warped out of it), and nothing more it does there counts. What it moved is the others' to take.
    // It still hears of stars taken there (a Grand Star takes it along once its scene starts over).
    void miss(Player player);
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
    // The other players in `player`'s instance (a hit is played again in their games). Nobody for a player in no
    // instance.
    [[nodiscard]] std::vector<Player> others(Player player) const;
    // A player's objects' states (`now_ms` the host's clock): the ones it may move, and the others in its instance to
    // tell. Each object has one owner at a time, as sm64coopdx's: a player takes it over from its owner when the owner
    // fell silent for kObjectOwnerSilenceMs (it went away, or the object stopped for it), when it is a quarter nearer
    // to the object than the owner, or when it just hit it.
    struct Claimed {
        std::vector<ObjectState> states;
        std::vector<Player> tell;
    };
    Claimed claim(Player player, std::span<const ObjectState> states, std::uint64_t now_ms);
    // Who moves an object of `player`'s instance now, if anyone (tests).
    [[nodiscard]] std::optional<Player> owner(Player player, std::uint32_t object) const;
    // A player's switches turned: the ones that change the instance's record, and the others there to tell.
    struct Switched {
        std::vector<SwitchChange> news;
        std::vector<Player> tell;
    };
    Switched switches(Player player, std::span<const SwitchChange> switches);
    // An instance's switches as its record has them, for a player entering it.
    [[nodiscard]] std::vector<SwitchChange> switches_of(const Instance& instance) const;
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
    struct Owner {
        Player player = net::kNoPeer;
        std::uint64_t heard_ms = 0u;
        float distance = 0.0f;
    };
    struct Record {
        std::map<std::uint32_t, std::uint32_t> gone;  // object, how
        std::map<std::uint32_t, Owner> owners;        // object, who moves it
        std::map<std::uint32_t, bool> switches;       // switch, on
        std::set<Player> players;
        std::set<Player> missed;                      // those of players whose miss goes on
    };
    void drop_player(Player player);
    // The record of the instance `player` plays in: none for a player in no instance, or whose miss goes on.
    Record* playing(Player player);

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
    enum class Kind : std::uint8_t { Gone, Ready, Save, StarGot, Hit, Switch };
    Kind kind = Kind::Gone;
    Gone gone;
    SaveChange save;
    StarGot star;
    ObjectHit hit;
    SwitchChange change;
};

// The world over a session, for this player's game. The game's calls (enter, report_gone, report_save, take) come
// from its thread; the session's messages on the session's.
class World {
public:
    // A message the world sends once its lock is let go (the session's own lock comes first on its thread).
    struct Outgoing {
        std::uint8_t type = 0u;
        net::PeerId to = net::kEveryone;
        std::vector<std::uint8_t> bytes;
        bool reliable = true;
    };

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
    // The game's player missed (no bubble saved it): it doesn't keep the instance it entered going until its scene
    // starts over and enters again, as sm64coopdx's death warp takes a player out of the level (WorldAuthority::miss).
    void report_miss();
    // The objects the game's player moves (their states, at most each frame): the host lets it move them or not, and
    // tells the others in the instance of the ones it does.
    void report_states(std::span<const ObjectState> states);
    // The game's player hit an object: the others in the instance play it again.
    void report_hit(const ObjectHit& hit);
    // The game turned a switch: the instance's record keeps it and the others there are told.
    void report_switch(const SwitchChange& change);
    [[nodiscard]] std::vector<WorldEvent> take();
    // The others' objects' states as they came, the latest of each object (unreliable: some never come).
    [[nodiscard]] std::vector<ObjectState> take_states();
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
    // The world's lock held, the host's: a player's states, hit or switches settled, and who to tell (the host's own
    // game here, the rest into `out`).
    void host_states(net::PeerId from, unsigned number, std::span<const ObjectState> states, std::vector<Outgoing>& out);
    void host_hit(net::PeerId from, ObjectHit hit, std::vector<Outgoing>& out);
    void host_switches(net::PeerId from, std::span<const SwitchChange> switches, std::vector<Outgoing>& out);
    void push_state(const ObjectState& state);
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
    std::vector<ObjectState> states_;    // for the game, the latest of each object
    std::unordered_map<std::uint32_t, std::size_t> state_index_;  // an object's place in states_
    std::vector<SaveChange> achievements_;  // for the runtime (take_achievements)
    std::vector<net::PeerId> members_;   // the host's players at the last update
    // Each scene this player enters is counted (a joiner's), and a host keeps each player's: what comes for or from
    // an earlier scene is dropped.
    std::uint16_t epoch_ = 0u;
    std::map<net::PeerId, std::uint16_t> epochs_;
};

}  // namespace galaxy::coop
