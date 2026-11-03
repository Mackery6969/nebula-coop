// Online co-op's player snapshots (galaxy/coop_snapshot.h): their bytes, blending two, and the other players as the
// game draws them, a little in the past.
#include "galaxy/coop_session.h"
#include "galaxy/coop_snapshot.h"

#include <chrono>
#include <cmath>
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
using galaxy::coop::PlayerSnapshot;
using galaxy::coop::RemotePlayers;
using Clock = RemotePlayers::Clock;

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

PlayerSnapshot at(std::uint32_t sequence, float x, const char* stage = "EggStarGalaxy", int scenario = 1) {
    PlayerSnapshot snapshot;
    snapshot.sequence = sequence;
    snapshot.stage = stage;
    snapshot.scenario = scenario;
    snapshot.position = {x, 0.0f, 0.0f};
    snapshot.gravity = {0.0f, -1.0f, 0.0f};
    snapshot.front = {0.0f, 0.0f, 1.0f};
    snapshot.up = {0.0f, 1.0f, 0.0f};
    snapshot.group = "run";
    snapshot.animation = "run";
    snapshot.frame = x;
    return snapshot;
}

void test_bytes() {
    PlayerSnapshot snapshot = at(7u, 12.5f);
    snapshot.rotation = {0.0f, 90.0f, 0.0f};
    snapshot.front = {1.0f, 0.0f, 0.0f};
    snapshot.velocity = {1.0f, 2.0f, 3.0f};
    snapshot.rate = 0.5f;
    snapshot.weights = {0.25f, 0.75f, 0.0f, 0.0f};
    snapshot.health = 3u;
    snapshot.mode = 2u;
    snapshot.flags = 1u;
    const std::vector<std::uint8_t> bytes = galaxy::coop::encode_snapshot(snapshot);
    const std::optional<PlayerSnapshot> back = galaxy::coop::decode_snapshot(bytes);
    expect(back.has_value(), "a snapshot reads back");
    expect(back && back->sequence == 7u && back->stage == "EggStarGalaxy" && back->scenario == 1 &&
               back->position[0] == 12.5f && back->rotation[1] == 90.0f && back->front[0] == 1.0f && back->up[1] == 1.0f && back->velocity[2] == 3.0f &&
               back->group == "run" && back->animation == "run" && back->rate == 0.5f && back->weights[1] == 0.75f &&
               back->health == 3u && back->mode == 2u && back->flags == 1u,
           "as it was sent");
    expect(bytes.size() < 200u, "in one small datagram");
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        if (galaxy::coop::decode_snapshot({bytes.data(), cut})) {
            expect(false, "a snapshot cut short is dropped");
            break;
        }
    }
    PlayerSnapshot broken = snapshot;
    broken.position[0] = std::nanf("");
    expect(!galaxy::coop::decode_snapshot(galaxy::coop::encode_snapshot(broken)), "one with a NaN is dropped");
    std::vector<std::uint8_t> other = bytes;
    other[0] = 99u;
    expect(!galaxy::coop::decode_snapshot(other), "one of another version is dropped");
}

void test_blend() {
    PlayerSnapshot from = at(1u, 0.0f);
    PlayerSnapshot to = at(2u, 10.0f);
    from.rotation = {0.0f, 350.0f, 0.0f};
    to.rotation = {0.0f, 10.0f, 0.0f};
    to.gravity = {1.0f, 0.0f, 0.0f};
    to.front = {1.0f, 0.0f, 0.0f};
    const PlayerSnapshot half = galaxy::coop::blend_snapshots(from, to, 0.5f);
    expect(near(half.position[0], 5.0f), "halfway between two");
    expect(near(std::fmod(half.rotation[1] + 360.0f, 360.0f), 0.0f), "turned the short way round");
    const float length = std::sqrt(half.gravity[0] * half.gravity[0] + half.gravity[1] * half.gravity[1]);
    expect(near(length, 1.0f) && near(half.gravity[0], half.gravity[1] * -1.0f), "gravity stays unit length");
    expect(near(half.front[0], half.front[2]) && near(half.front[0] * half.front[0] * 2.0f, 1.0f),
           "facing halfway round, unit length");
    expect(near(half.frame, 5.0f), "the same animation's frame along the way");
    to.group = "jump";
    to.frame = 2.0f;
    const PlayerSnapshot early = galaxy::coop::blend_snapshots(from, to, 0.25f, 4.0f);
    expect(early.group == "run" && near(early.frame, 1.0f),
           "another animation, nearer the earlier one: the earlier's, moved on at its speed");
    const PlayerSnapshot late = galaxy::coop::blend_snapshots(from, to, 0.75f, 4.0f);
    expect(late.group == "jump" && near(late.frame, 1.0f) && late.sequence == to.sequence,
           "another animation, nearer the later one: the later's, from a frame before its own");
    PlayerSnapshot looped = at(3u, 0.0f);
    looped.frame = 58.0f;
    PlayerSnapshot round = at(4u, 0.0f);
    round.frame = 1.0f;
    expect(near(galaxy::coop::blend_snapshots(looped, round, 0.25f, 4.0f).frame, 59.0f),
           "round a loop: moved on past its end, not back through it");
}

void test_remote_players() {
    RemotePlayers players;
    const Clock::time_point start = Clock::now();
    const auto send = [&](std::uint32_t sequence, float x, Clock::duration when, const char* stage = "EggStarGalaxy") {
        players.receive(5u, 1u, "LUIGI", galaxy::CoopCharacter::Luigi,
                        galaxy::coop::encode_snapshot(at(sequence, x, stage)), start + when);
    };
    send(1u, 0.0f, 0ms);
    send(2u, 10.0f, 100ms);
    send(3u, 20.0f, 200ms);
    std::vector<galaxy::coop::RemotePlayer> seen = players.sample("EggStarGalaxy", 1, start + 250ms);
    expect(seen.size() == 1u && seen[0].name == "LUIGI" && seen[0].character == galaxy::CoopCharacter::Luigi &&
               seen[0].index == 1u,
           "a player in the same instance is drawn");
    expect(!seen.empty() && near(seen[0].snapshot.position[0], 15.0f), "100 ms in the past, between two snapshots");
    expect(players.sample("EggStarGalaxy", 2, start + 250ms).empty(), "not on another star");
    expect(players.sample("AstroGalaxy", 1, start + 250ms).empty(), "nor in another galaxy");
    send(2u, 99.0f, 260ms);
    seen = players.sample("EggStarGalaxy", 1, start + 400ms);
    expect(!seen.empty() && near(seen[0].snapshot.position[0], 20.0f), "a late snapshot is dropped; past the last, it");
    expect(players.sample("EggStarGalaxy", 1, start + 1300ms).empty(), "nothing heard for a second: not drawn");
    send(4u, 0.0f, 1400ms, "AstroGalaxy");
    expect(players.sample("EggStarGalaxy", 1, start + 1450ms).empty(), "gone to another galaxy");
    expect(players.sample("AstroGalaxy", 1, start + 1450ms).size() == 1u, "and drawn there");
    players.receive(9u, 0u, "MARIO", galaxy::CoopCharacter::Mario,
                    galaxy::coop::encode_snapshot(at(1u, 0.0f, "AstroGalaxy")), start + 1600ms);
    seen = players.sample("AstroGalaxy", 1, start + 1700ms);
    expect(seen.size() == 2u && seen[0].name == "MARIO", "by the session's number");
    players.remove(9u);
    expect(players.sample("AstroGalaxy", 1, start + 1700ms).size() == 1u, "a player who left is gone");
    players.clear();
    expect(players.sample("AstroGalaxy", 1, start + 1700ms).empty(), "and all of them with the room");
}

bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit = 10000ms) {
    const Clock::time_point until = Clock::now() + limit;
    while (Clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

// Over the loopback: a joiner's snapshots reach the host and, passed on by it, the other joiner, each with who sent
// them; the host's reach both. Only players in the sender's instance get them.
void test_session_snapshots() {
    using namespace galaxy::coop;
    Session host_session;
    HostConfig host;
    host.port = 0u;
    host.room.max_players = 3u;
    host.game_version = "test 1";
    host.name = "HOSTY";
    host.save = [] { return RoomSave{}; };
    RemotePlayers at_host, at_mario, at_luigi;
    listen_snapshots(host_session, at_host);
    expect(host_session.host(host), "a room opens");
    Session mario, luigi;
    listen_snapshots(mario, at_mario);
    listen_snapshots(luigi, at_luigi);
    expect(mario.join({"127.0.0.1", host_session.port(), "", "test 1", "MARIO", galaxy::CoopCharacter::Mario}) &&
               luigi.join({"127.0.0.1", host_session.port(), "", "test 1", "LUIGI", galaxy::CoopCharacter::Luigi}),
           "two players join");
    expect(wait_for([&] {
               return mario.state() == SessionState::Joined && luigi.state() == SessionState::Joined &&
                      host_session.players().size() == 3u && mario.players().size() == 3u &&
                      luigi.players().size() == 3u;
           }),
           "and are in");
    for (Session* session : {&host_session, &mario, &luigi}) session->set_place("EggStarGalaxy", 1);
    const auto placed = [](const Session& session, const char* stage) {
        for (const SessionPlayer& player : session.players()) {
            if (player.stage != stage) return false;
        }
        return true;
    };
    expect(wait_for([&] {
               return placed(host_session, "EggStarGalaxy") && placed(mario, "EggStarGalaxy") &&
                      placed(luigi, "EggStarGalaxy");
           }),
           "everyone knows where everyone plays");
    std::uint32_t sequence = 0u;
    const auto seen = [](const RemotePlayers& players, const char* name, const char* stage = "EggStarGalaxy") {
        for (const RemotePlayer& player : players.sample(stage, 1, Clock::now() + 200ms)) {
            if (player.name == name) return true;
        }
        return false;
    };
    expect(wait_for([&] {
               ++sequence;
               (void)send_snapshot(luigi, at(sequence, 1.0f));
               (void)send_snapshot(host_session, at(sequence, 2.0f));
               return seen(at_host, "LUIGI") && seen(at_mario, "LUIGI") && seen(at_mario, "HOSTY") &&
                      seen(at_luigi, "HOSTY");
           }),
           "snapshots reach everyone, each with its sender");
    expect(!seen(at_luigi, "LUIGI") && !seen(at_host, "HOSTY"), "and not their own sender");
    const std::vector<RemotePlayer> at_joiner = at_mario.sample("EggStarGalaxy", 1, Clock::now() + 200ms);
    expect(at_joiner.size() == 2u && at_joiner[0].name == "HOSTY" && at_joiner[0].index == 0u &&
               at_joiner[1].character == galaxy::CoopCharacter::Luigi,
           "by number, with their characters");

    // Mario goes to another galaxy: nobody sends him theirs, his reach the host only (it passes them to nobody).
    mario.set_place("HeavensDoorGalaxy", 1);
    expect(wait_for([&] { return !placed(host_session, "EggStarGalaxy") && !placed(luigi, "EggStarGalaxy"); }),
           "the room hears he moved");
    // Snapshots sent to him before he moved may still wait in his socket (a slow runner's session thread drains it
    // late): let them land before forgetting them.
    std::this_thread::sleep_for(200ms);
    at_mario.clear();
    at_luigi.clear();
    for (int i = 0; i < 30; ++i) {
        ++sequence;
        (void)send_snapshot(luigi, at(sequence, 1.0f));
        (void)send_snapshot(host_session, at(sequence, 2.0f));
        (void)send_snapshot(mario, at(sequence, 3.0f, "HeavensDoorGalaxy"));
        std::this_thread::sleep_for(5ms);
    }
    expect(wait_for([&] { return seen(at_host, "MARIO", "HeavensDoorGalaxy"); }), "the host still hears him");
    expect(!seen(at_mario, "HOSTY") && !seen(at_mario, "LUIGI"), "but he gets nobody's from another instance");
    expect(!seen(at_luigi, "MARIO", "HeavensDoorGalaxy"), "and nobody gets his");
    expect(seen(at_luigi, "HOSTY"), "while the others still see each other");
    luigi.leave();
    mario.leave();
    host_session.leave();
}

}  // namespace

int main() {
    test_bytes();
    test_blend();
    test_remote_players();
    test_session_snapshots();
    if (s_failures == 0) std::printf("snapshot tests passed\n");
    return s_failures == 0 ? 0 : 1;
}
