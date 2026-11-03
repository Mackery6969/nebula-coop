// Online co-op's shared world (galaxy/coop_world.h): the host's record of each instance and the session's save, and
// the world over a session (what each player's game is told).
#include "galaxy/coop_session.h"
#include "galaxy/coop_world.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

using namespace std::chrono_literals;
using namespace galaxy::coop;
using Clock = std::chrono::steady_clock;

const Instance kEgg1{"EggStarGalaxy", 1};
const Instance kEgg2{"EggStarGalaxy", 2};

bool has(const std::vector<Gone>& gone, std::uint32_t object) {
    for (const Gone& thing : gone) {
        if (thing.object == object) return true;
    }
    return false;
}

void test_bytes() {
    const std::vector<Gone> gone{{1u, 0u}, {0xdeadbeefu, 1u}};
    const std::optional<std::vector<Gone>> back = decode_gone(encode_gone(gone));
    expect(back && back->size() == 2u && (*back)[1].object == 0xdeadbeefu && (*back)[1].how == 1u, "gone round trip");
    const std::optional<Instance> instance = decode_instance(encode_instance(kEgg2));
    expect(instance && *instance == kEgg2, "an instance round trip");
    expect(!decode_instance(encode_instance({"Egg/../Galaxy", 1})), "a stage with odd characters is dropped");

    const std::vector<SaveChange> changes{{SaveKind::Star, "EggStarGalaxy", 3, 0u},
                                          {SaveKind::Flag, "\x83\x60\x83\x52", 0, 0u},
                                          {SaveKind::Value, "Comet1Status", 0, 7u},
                                          {SaveKind::Star, "", 1, 0u},
                                          {SaveKind::Achievement, "first_star", 0, 1793145600u},
                                          {SaveKind::Achievement, "../first_star", 0, 1u}};
    const std::optional<std::vector<SaveChange>> saved = decode_save_changes(encode_save_changes(changes));
    expect(saved && saved->size() == 4u, "save changes round trip, the bad ones dropped");
    expect(saved && (*saved)[0].scenario == 3 && (*saved)[1].name == "\x83\x60\x83\x52" && (*saved)[2].value == 7u,
           "with their stars, Shift JIS names and values");
    expect(saved && saved->size() == 4u && (*saved)[3].name == "first_star" && (*saved)[3].value == 1793145600u,
           "and an achievement with when it was unlocked");
    std::vector<std::uint8_t> cut = encode_gone(gone);
    cut.pop_back();
    expect(!decode_gone(cut), "a cut list is dropped");
}

void test_authority() {
    WorldAuthority world;
    expect(world.enter(1u, kEgg1).empty(), "a new instance has nothing gone");
    WorldAuthority::Settled settled = world.gone(1u, std::vector<Gone>{{10u, 1u}, {11u, 0u}});
    expect(settled.news.size() == 2u && settled.tell.empty(), "news, nobody else to tell");
    const std::vector<Gone> record = world.enter(2u, kEgg1);
    expect(record.size() == 2u && has(record, 10u) && has(record, 11u), "a player entering gets the record");
    settled = world.gone(2u, std::vector<Gone>{{10u, 1u}, {12u, 0u}});
    expect(settled.news.size() == 1u && settled.news[0].object == 12u, "first come wins: a coin can't go twice");
    expect(settled.tell.size() == 1u && settled.tell[0] == 1u, "the others in the instance are told");
    expect(world.enter(3u, kEgg2).empty(), "another mission is another instance");
    settled = world.gone(3u, std::vector<Gone>{{12u, 0u}});
    expect(settled.news.size() == 1u && settled.tell.empty(), "with a record of its own");
    expect(world.instance_count() == 2u, "two instances");
    const std::vector<WorldAuthority::Player> told = world.star_got(2u);
    expect(told.size() == 1u && told[0] == 1u, "a star got is told to the others in the instance alone");
    expect(world.star_got(3u).empty(), "alone in an instance, nobody to tell");
    expect(world.star_got(9u).empty(), "a player in no instance tells nobody");

    // Player 1's scene starts over: player 2 is still there, so the record stays.
    expect(world.enter(1u, kEgg1).size() == 3u, "an instance with someone in it keeps its record");
    world.leave(2u);
    // Now alone: a scene that starts over starts fresh, as the game alone does.
    expect(world.enter(1u, kEgg1).empty(), "an instance nobody else is in starts fresh");
    world.leave(1u);
    world.leave(3u);
    expect(world.instance_count() == 0u, "empty instances are forgotten");
    expect(world.gone(1u, std::vector<Gone>{{1u, 0u}}).news.empty(), "a player in no instance changes nothing");

    expect(world.save({SaveKind::Star, "EggStarGalaxy", 1, 0u}, false), "a joiner's star is the session's");
    expect(!world.save({SaveKind::Star, "EggStarGalaxy", 1, 0u}, true), "once");
    expect(world.save({SaveKind::Flag, "SpecialStarGrand1", 0, 0u}, false), "a flag");
    expect(!world.save({SaveKind::Value, "Comet1Status", 0, 3u}, false), "a joiner's event values stay its own");
    expect(world.save({SaveKind::Value, "Comet1Status", 0, 3u}, true), "the host's are the session's");
    expect(!world.save({SaveKind::Value, "Comet1Status", 0, 3u}, true), "unchanged is no news");
    expect(world.save({SaveKind::Value, "Comet1Status", 0, 4u}, true), "changed is");
    expect(world.save_changes().size() == 3u && world.save_changes()[2].value == 4u,
           "the record keeps a value's latest only");
    expect(!world.save({SaveKind::Star, "EggStarGalaxy", 0, 0u}, true), "no star 0");

    expect(world.save({SaveKind::Achievement, "first_star", 0, 100u}, false), "a joiner's achievement is the save's");
    expect(!world.save({SaveKind::Achievement, "first_star", 0, 200u}, true), "once, whoever unlocks it again");
    expect(world.save_changes().back().value == 100u, "on the day it was first unlocked");
    expect(!world.save({SaveKind::Achievement, "", 0, 1u}, true), "no achievement without an id");
}

bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit = 10000ms) {
    const Clock::time_point until = Clock::now() + limit;
    while (Clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

// What a player's game has been told: things gone, whether the record ended, the save; and the achievements its
// runtime was given.
struct Game {
    World world;
    std::vector<Gone> gone;
    bool ready = false;
    std::vector<SaveChange> save;
    std::vector<StarGot> stars;
    std::vector<SaveChange> achievements;

    void pump() {
        for (const WorldEvent& event : world.take()) {
            if (event.kind == WorldEvent::Kind::Gone) gone.push_back(event.gone);
            if (event.kind == WorldEvent::Kind::Ready) ready = true;
            if (event.kind == WorldEvent::Kind::Save) save.push_back(event.save);
            if (event.kind == WorldEvent::Kind::StarGot) stars.push_back(event.star);
        }
        for (const SaveChange& change : world.take_achievements()) achievements.push_back(change);
    }
    bool got(const std::string& id) const {
        for (const SaveChange& had : achievements) {
            if (had.name == id) return true;
        }
        return false;
    }
    void enter(const Instance& instance) {
        gone.clear();
        save.clear();
        stars.clear();
        ready = false;
        world.enter(instance);
    }
    bool saw(const SaveChange& change) const {
        for (const SaveChange& had : save) {
            if (had == change) return true;
        }
        return false;
    }
};

void test_session_world() {
    Session host_session, mario, luigi;
    Game at_host, at_mario, at_luigi;
    at_host.world.attach(host_session);
    at_mario.world.attach(mario);
    at_luigi.world.attach(luigi);
    HostConfig host;
    host.port = 0u;
    host.room.max_players = 3u;
    host.game_version = "test 1";
    host.name = "HOSTY";
    host.save = [] { return RoomSave{}; };
    expect(host_session.host(host), "a room opens");
    at_host.world.start();
    const SaveChange early{SaveKind::Star, "EggStarGalaxy", 1, 0u};
    at_host.world.report_save(early);
    expect(mario.join({"127.0.0.1", host_session.port(), "", "test 1", "MARIO", galaxy::CoopCharacter::Mario}) &&
               luigi.join({"127.0.0.1", host_session.port(), "", "test 1", "LUIGI", galaxy::CoopCharacter::Luigi}),
           "two players join");
    expect(wait_for([&] {
               return mario.state() == SessionState::Joined && luigi.state() == SessionState::Joined &&
                      host_session.players().size() == 3u;
           }),
           "and are in");
    at_mario.world.start();
    at_luigi.world.start();
    const auto pump_all = [&] {
        at_host.world.update();
        at_host.pump();
        at_mario.pump();
        at_luigi.pump();
    };
    expect(wait_for([&] {
               pump_all();
               return at_mario.saw(early) && at_luigi.saw(early);
           }),
           "joiners get the session's save");

    at_host.enter(kEgg1);
    at_mario.enter(kEgg1);
    expect(wait_for([&] {
               pump_all();
               return at_host.ready && at_mario.ready;
           }),
           "both scenes get their (empty) record");
    expect(at_host.saw(early) && at_mario.saw(early), "and the save again, at each scene");
    at_mario.world.report_gone({100u, 1u});
    expect(wait_for([&] {
               pump_all();
               return has(at_host.gone, 100u);
           }),
           "a joiner's coin is gone at the host");
    at_host.world.report_gone({101u, 0u});
    at_host.world.report_gone({100u, 1u});
    expect(wait_for([&] {
               pump_all();
               return has(at_mario.gone, 101u);
           }),
           "the host's enemy is gone for the joiner");
    std::this_thread::sleep_for(100ms);
    pump_all();
    expect(at_mario.gone.size() == 1u, "a coin already gone isn't news again");

    // Luigi enters the same instance late: he gets its record; then another mission: nothing from this one.
    at_luigi.enter(kEgg1);
    expect(wait_for([&] {
               pump_all();
               return at_luigi.ready;
           }),
           "a late player's record comes");
    expect(has(at_luigi.gone, 100u) && has(at_luigi.gone, 101u) && at_luigi.gone.size() == 2u,
           "with everything gone so far");
    at_luigi.enter(kEgg2);
    expect(wait_for([&] {
               pump_all();
               return at_luigi.ready;
           }),
           "another instance's record");
    at_mario.world.report_gone({102u, 0u});
    std::this_thread::sleep_for(150ms);
    pump_all();
    expect(at_luigi.gone.empty(), "nothing from an instance he left");
    expect(has(at_host.gone, 102u), "while the host, still there, hears it");

    // A star got: the others in the taker's instance hear it (a Grand Star takes them along), nobody elsewhere.
    at_mario.world.report_star({3, true});
    expect(wait_for([&] {
               pump_all();
               return !at_host.stars.empty();
           }),
           "a joiner's Grand Star reaches the host in its instance");
    expect(at_host.stars.size() == 1u && at_host.stars[0] == StarGot{3, true}, "with its number and kind");
    at_host.world.report_star({1, false});
    expect(wait_for([&] {
               pump_all();
               return !at_mario.stars.empty();
           }),
           "the host's star reaches the joiner in its instance");
    expect(at_mario.stars[0] == StarGot{1, false} && at_mario.stars.size() == 1u, "once, and not its own back");
    std::this_thread::sleep_for(100ms);
    pump_all();
    expect(at_luigi.stars.empty(), "nothing for a player in another instance");
    expect(at_host.stars.size() == 1u, "the host doesn't hear its own");

    // Save: a joiner's star is everyone's; a joiner's event value isn't sent; the host's is.
    const SaveChange star{SaveKind::Star, "EggStarGalaxy", 2, 0u};
    const SaveChange flag{SaveKind::Flag, "SpecialStarGrand1", 0, 0u};
    at_luigi.world.report_save(star);
    at_luigi.world.report_save({SaveKind::Value, "Comet1Status", 0, 5u});
    at_mario.world.report_save(flag);
    at_host.world.report_save({SaveKind::Value, "Comet2Status", 0, 9u});
    const SaveChange value{SaveKind::Value, "Comet2Status", 0, 9u};
    expect(wait_for([&] {
               pump_all();
               return at_host.saw(star) && at_mario.saw(star) && at_luigi.saw(star) && at_host.saw(flag) &&
                      at_luigi.saw(flag) && at_mario.saw(value) && at_luigi.saw(value);
           }),
           "stars and flags from anyone, values from the host, reach everyone");
    for (const Game* game : {&at_host, &at_mario, &at_luigi}) {
        for (const SaveChange& change : game->save) {
            expect(change.name != "Comet1Status", "a joiner's event value stays its own");
        }
    }

    // The Personal Star Counter: each star new to the save counts for who got it, as the host settles it.
    at_mario.world.report_save(star);
    const auto stars_of = [](const Session& session, const char* name) {
        for (const SessionPlayer& player : session.players()) {
            if (player.name == name) return static_cast<int>(player.stars);
        }
        return -1;
    };
    expect(wait_for([&] {
               pump_all();
               return luigi.stars() == 1u && stars_of(mario, "LUIGI") == 1 && stars_of(luigi, "HOSTY") == 1;
           }),
           "a star counts for who got it, and everyone hears");
    expect(host_session.stars() == 1u && mario.stars() == 0u && stars_of(host_session, "MARIO") == 0,
           "a star the room already had counts for nobody");

    // Achievements: a joiner's is the save's, for the host's runtime and every player's; the host's too. The game
    // isn't told them (its runtime is), and a scene start doesn't give them again.
    at_luigi.world.report_save({SaveKind::Achievement, "luigi_play", 0, 1000u});
    at_host.world.report_save({SaveKind::Achievement, "first_star", 0, 2000u});
    at_mario.world.report_save({SaveKind::Achievement, "luigi_play", 0, 3000u});
    expect(wait_for([&] {
               pump_all();
               return at_host.got("luigi_play") && at_mario.got("luigi_play") && at_luigi.got("luigi_play") &&
                      at_mario.got("first_star") && at_luigi.got("first_star");
           }),
           "an achievement unlocked by anyone reaches everyone");
    std::this_thread::sleep_for(100ms);
    pump_all();
    for (const Game* game : {&at_host, &at_mario, &at_luigi}) {
        int luigi_play = 0;
        for (const SaveChange& change : game->achievements) {
            if (change.name != "luigi_play") continue;
            ++luigi_play;
            expect(change.value == 1000u, "on the day the first player unlocked it");
        }
        expect(luigi_play == 1, "once, though a second player unlocked it too");
        for (const SaveChange& change : game->save) {
            expect(change.kind != SaveKind::Achievement, "the game isn't told achievements");
        }
    }
    at_mario.achievements.clear();
    at_mario.enter(kEgg1);
    expect(wait_for([&] {
               pump_all();
               return at_mario.ready;
           }),
           "a scene starts over");
    expect(at_mario.achievements.empty(), "and isn't given the achievements again");

    // Mario leaves the room: the host forgets him, and his instance keeps the host in it.
    mario.leave();
    expect(wait_for([&] {
               pump_all();
               return host_session.players().size() == 2u;
           }),
           "the host sees him go");
    pump_all();
    at_host.enter(kEgg1);
    expect(wait_for([&] {
               pump_all();
               return at_host.ready;
           }),
           "the host's scene starts over");
    expect(at_host.gone.empty(), "alone there, it starts fresh");
    expect(at_host.world.instance_count() == 2u, "two instances: the host's, Luigi's");

    luigi.leave();
    host_session.leave();
    at_host.world.stop();
}

}  // namespace

int main() {
    test_bytes();
    test_authority();
    test_session_world();
    if (s_failures == 0) std::printf("world tests passed\n");
    return s_failures == 0 ? 0 : 1;
}
