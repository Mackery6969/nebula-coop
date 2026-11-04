// Online co-op's cheat commands (galaxy/coop_teleport.h): /tp's words as Minecraft reads them, its message, and /tp,
// /teleport and /pos between players over the loopback, only while the room's cheats are on.
#include "galaxy/coop_chat.h"
#include "galaxy/coop_commands.h"
#include "galaxy/coop_menu.h"
#include "galaxy/coop_teleport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

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

void type_line(Chat& chat, const std::string& text) {
    chat.open();
    for (const char c : text) chat.type(static_cast<char32_t>(c), chat_clock_ms());
    chat.type(kTextEnter, chat_clock_ms());
}

bool said(const Chat& chat, const std::string& text, const std::string& name = {}) {
    const std::vector<ChatLine> lines = chat.lines();
    return std::any_of(lines.begin(), lines.end(), [&](const ChatLine& line) {
        return line.kind == ChatLine::Kind::Notice && line.text == text && (name.empty() || line.name == name);
    });
}

bool wait_for(const std::function<bool()>& done) {
    for (int i = 0; i < 400; ++i) {
        if (done()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return done();
}

bool near(const Position& a, const Position& b) {
    for (std::size_t i = 0; i < 3u; ++i) {
        if (a[i] - b[i] > 0.01f || b[i] - a[i] > 0.01f) return false;
    }
    return true;
}

// A player's game: where it is, where it sees the others, and where it was moved to.
struct FakeGame {
    std::mutex lock;
    std::optional<Position> here;
    std::map<galaxy::net::PeerId, Position> others;
    std::vector<Position> moves;

    CheatHooks hooks() {
        CheatHooks out;
        out.here = [this] {
            const std::lock_guard<std::mutex> guard(lock);
            return here;
        };
        out.where = [this](galaxy::net::PeerId id) -> std::optional<Position> {
            const std::lock_guard<std::mutex> guard(lock);
            const auto it = others.find(id);
            if (it == others.end()) return std::nullopt;
            return it->second;
        };
        out.move = [this](const Position& to) {
            const std::lock_guard<std::mutex> guard(lock);
            moves.push_back(to);
            here = to;
        };
        return out;
    }
    std::optional<Position> last_move() {
        const std::lock_guard<std::mutex> guard(lock);
        if (moves.empty()) return std::nullopt;
        return moves.back();
    }
    std::size_t move_count() {
        const std::lock_guard<std::mutex> guard(lock);
        return moves.size();
    }
};

void test_words() {
    const std::optional<Coordinate> plain = parse_coordinate("12.5");
    const std::optional<Coordinate> tilde = parse_coordinate("~");
    const std::optional<Coordinate> up = parse_coordinate("~-30");
    expect(plain && !plain->relative && plain->value == 12.5f, "a number is a coordinate");
    expect(tilde && tilde->relative && tilde->value == 0.0f, "~ is where they are");
    expect(up && up->relative && up->value == -30.0f, "~ and a number is that far from there");
    expect(!parse_coordinate("") && !parse_coordinate("~~") && !parse_coordinate("abc") &&
               !parse_coordinate("1e30") && !parse_coordinate("nan") && !parse_coordinate("5x"),
           "anything else isn't");

    const std::optional<TeleportWords> to_player = parse_teleport("MARIO");
    expect(to_player && to_player->target.empty() && to_player->player == "MARIO", "/tp PLAYER");
    const std::optional<TeleportWords> them = parse_teleport("LUIGI  MARIO");
    expect(them && them->target == "LUIGI" && them->player == "MARIO", "/tp TARGET PLAYER");
    const std::optional<TeleportWords> place = parse_teleport("1 ~2 -3");
    expect(place && place->target.empty() && !place->player && place->place[1].relative && place->place[2].value == -3.0f,
           "/tp X Y Z");
    const std::optional<TeleportWords> sent = parse_teleport("@a ~ ~500 ~");
    expect(sent && sent->target == "@a" && !sent->player && sent->place[1].value == 500.0f, "/tp TARGET X Y Z");
    expect(!parse_teleport("") && !parse_teleport("1 2 three") && !parse_teleport("a 1 2 x") &&
               !parse_teleport("a b c d e"),
           "other words aren't a teleport");

    Place mixed{};
    mixed[0] = {5.0f, false};
    mixed[1] = {10.0f, true};
    mixed[2] = {0.0f, true};
    expect(near(resolve_place(mixed, {100.0f, 200.0f, 300.0f}), {5.0f, 210.0f, 300.0f}),
           "~ axes count from where they are");
    expect(position_text({1.4f, -2.6f, 4500.0f}) == "1 -3 4500", "a position in whole units");

    TeleportMessage message;
    message.by = 7u;
    message.target = 9u;
    message.to.player = 3u;
    message.to.place = mixed;
    const std::vector<std::uint8_t> bytes = encode_teleport(message);
    const std::optional<TeleportMessage> back = decode_teleport(bytes);
    expect(back && back->by == 7u && back->target == 9u && back->to.player == 3u && back->to.place[1].relative &&
               back->to.place[1].value == 10.0f,
           "a teleport message comes back as it went");
    expect(!decode_teleport(std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1u)), "a short one doesn't");
}

void test_room() {
    Chat host_chat, joiner_chat, third_chat;
    Session host_session, joiner, third;
    FakeGame host_game, joiner_game, third_game;
    for (auto [chat, session, game] : {std::tuple{&host_chat, &host_session, &host_game},
                                       std::tuple{&joiner_chat, &joiner, &joiner_game},
                                       std::tuple{&third_chat, &third, &third_game}}) {
        chat->attach(*session);
        add_room_commands(*chat, *session);
        add_cheat_commands(*chat, *session, game->hooks());
    }

    HostConfig host;
    host.port = 0u;
    host.game_version = "teleport test";
    host.name = "HOSTY";
    host.save = [] { return RoomSave{}; };
    expect(host_session.host(host), "a room opens");
    const auto join = [&](const char* name) {
        JoinConfig config;
        config.address = "127.0.0.1";
        config.port = host_session.port();
        config.game_version = "teleport test";
        config.name = name;
        return config;
    };
    expect(joiner.join(join("LUIGI FAN")) && wait_for([&] { return joiner.state() == SessionState::Joined; }) &&
               third.join(join("THIRD")),
           "two join");
    expect(wait_for([&] {
               return third.state() == SessionState::Joined && joiner.players().size() == 3u &&
                      third.players().size() == 3u;
           }),
           "and everyone is in");
    const auto id_of = [&](const char* name) {
        for (const SessionPlayer& player : host_session.players()) {
            if (player.name == name) return player.id;
        }
        return galaxy::net::kNoPeer;
    };
    const galaxy::net::PeerId host_id = id_of("HOSTY"), joiner_id = id_of("LUIGI FAN"), third_id = id_of("THIRD");
    // The joiners see each other by the ids the host gave (the session's own): each game sees the others here.
    const Position host_at{0.0f, 100.0f, 0.0f}, joiner_at{500.0f, 0.0f, 0.0f}, third_at{0.0f, 0.0f, 900.0f};
    host_game.here = host_at;
    host_game.others = {{joiner_id, joiner_at}, {third_id, third_at}};
    joiner_game.here = joiner_at;
    joiner_game.others = {{host_id, host_at}, {third_id, third_at}};
    third_game.here = third_at;
    third_game.others = {{host_id, host_at}, {joiner_id, joiner_at}};
    for (Session* session : {&host_session, &joiner, &third}) session->set_place("CosmosGardenGalaxy", 1);
    expect(wait_for([&] {
               const auto all_there = [](const Session& session) {
                   const std::vector<SessionPlayer> players = session.players();
                   return std::all_of(players.begin(), players.end(),
                                      [](const SessionPlayer& p) { return p.stage == "CosmosGardenGalaxy"; });
               };
               return all_there(host_session) && all_there(joiner) && all_there(third);
           }),
           "everyone is in the same galaxy");

    // Cheats off: the commands don't show, and say why.
    const auto shows = [](Chat& chat, const char* help) {
        const std::vector<std::string> list = chat.commands().help();
        return std::find(list.begin(), list.end(), help) != list.end();
    };
    expect(!shows(joiner_chat, "CoopCmdTpHelp") && !shows(joiner_chat, "CoopCmdPosHelp"), "cheats off: no /tp on /help");
    type_line(joiner_chat, "/tp HOSTY");
    expect(said(joiner_chat, "CoopCmdCheatsOff") && joiner_game.move_count() == 0u, "nor does it work");

    galaxy::RuntimeCoopSettings room = host_session.room();
    room.cheats = true;
    host_session.set_room(room);
    expect(wait_for([&] { return joiner.room().cheats && third.room().cheats; }), "the host turns cheats on");
    expect(shows(joiner_chat, "CoopCmdTpHelp") && shows(joiner_chat, "CoopCmdTeleportHelp") &&
               shows(joiner_chat, "CoopCmdPosHelp"),
           "and /tp, /teleport and /pos show");

    type_line(joiner_chat, "/pos");
    expect(said(joiner_chat, "CoopCmdPos", "500 0 0"), "/pos says where this player is");

    type_line(joiner_chat, "/tp HOSTY");
    expect(joiner_game.last_move() && near(*joiner_game.last_move(), host_at) && said(joiner_chat, "CoopCmdTpTo", "HOSTY"),
           "/tp PLAYER: to them");
    type_line(joiner_chat, "/teleport ~ ~50 ~-10");
    expect(joiner_game.last_move() && near(*joiner_game.last_move(), {0.0f, 150.0f, -10.0f}) &&
               said(joiner_chat, "CoopCmdTpTo", "0 150 -10"),
           "/teleport X Y Z: ~ from where they are");
    type_line(joiner_chat, "/tp 2 0");
    expect(said(joiner_chat, "CoopCmdNoPerms"), "a player may not move the others");
    type_line(joiner_chat, "/tp nobody");
    expect(said(joiner_chat, "CoopCmdNotFound"), "a name no one has");
    type_line(joiner_chat, "/tp 1 2 x");
    expect(said(joiner_chat, "CoopCmdTpHelp"), "other words: how it goes");

    // The host moves another: their own game resolves it.
    type_line(host_chat, "/tp THIRD ~ 7 ~");
    expect(wait_for([&] { return third_game.last_move().has_value(); }) &&
               near(*third_game.last_move(), {0.0f, 7.0f, 900.0f}) && said(host_chat, "CoopCmdTpOther", "THIRD"),
           "the host moves another player, ~ from where they are");
    expect(wait_for([&] { return said(third_chat, "CoopCmdTpBy", "HOSTY"); }), "who is told who did it");

    // A moderator may too, by the host.
    type_line(host_chat, "/moderator 1");
    type_line(host_chat, "/confirm");
    expect(wait_for([&] { return joiner.is_moderator(); }), "a moderator");
    type_line(joiner_chat, "/tp THIRD HOSTY");
    expect(wait_for([&] { return third_game.move_count() == 2u; }) && near(*third_game.last_move(), host_at),
           "a moderator moves another to a player");
    expect(wait_for([&] { return said(third_chat, "CoopCmdTpBy", "LUIGI FAN"); }), "and the host says who");

    // @a: everyone (but who is already there).
    const std::size_t host_moves = host_game.move_count();
    type_line(host_chat, "/tp @a @s");
    expect(wait_for([&] { return joiner_game.move_count() == 3u && third_game.move_count() == 3u; }) &&
               said(host_chat, "CoopCmdTpEveryone", "2") && host_game.move_count() == host_moves,
           "/tp @a @s: everyone to this player");

    // Another galaxy: nobody changes stage.
    third.set_place("EggStarGalaxy", 1);
    expect(wait_for([&] {
               const std::optional<SessionPlayer> seen = find_player(host_session.players(), "THIRD");
               return seen && seen->stage == "EggStarGalaxy";
           }),
           "a player goes elsewhere");
    type_line(host_chat, "/tp THIRD LUIGI FAN");
    expect(said(host_chat, "CoopCmdTpHelp"), "a name with a space is a number's job");
    type_line(host_chat, "/tp 2 1");
    expect(said(host_chat, "CoopCmdTpElsewhere", "LUIGI FAN"), "not to a player in another galaxy");
    {
        const std::lock_guard<std::mutex> guard(host_game.lock);
        host_game.others.erase(third_id);
    }
    type_line(host_chat, "/tp THIRD");
    expect(said(host_chat, "CoopCmdTpElsewhere", "THIRD"), "nor this player to one");

    // Cheats off again: a moderator's message is dropped too.
    room.cheats = false;
    host_session.set_room(room);
    expect(wait_for([&] { return !joiner.room().cheats; }), "cheats go off");
    type_line(joiner_chat, "/tp THIRD 0 0 0");
    expect(said(joiner_chat, "CoopCmdCheatsOff"), "and the commands stop");

    joiner.leave();
    third.leave();
    host_session.leave();
}

}  // namespace

int main() {
    test_words();
    test_room();
    if (s_failures != 0) {
        std::fprintf(stderr, "%d teleport test(s) failed\n", s_failures);
        return 1;
    }
    std::printf("teleport tests passed\n");
    return 0;
}
