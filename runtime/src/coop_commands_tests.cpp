// Online co-op's chat commands (galaxy/coop_commands.h): adding and running them, /help, /confirm, Tab, and the
// room's /players, /kick, /ban, /permban and /moderator between players over the loopback.
#include "galaxy/coop_chat.h"
#include "galaxy/coop_commands.h"
#include "galaxy/coop_menu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
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

std::vector<std::string> notices(const Chat& chat) {
    std::vector<std::string> out;
    for (const ChatLine& line : chat.lines()) {
        if (line.kind == ChatLine::Kind::Notice) out.push_back(line.text);
    }
    return out;
}

bool said(const Chat& chat, const std::string& text, const std::string& name = {}) {
    const std::vector<ChatLine> lines = chat.lines();
    return std::any_of(lines.begin(), lines.end(), [&](const ChatLine& line) {
        return line.kind == ChatLine::Kind::Notice && line.local && line.text == text &&
               (name.empty() || line.name == name);
    });
}

void test_options() {
    const std::vector<std::string> psc = Commands::options_in("[On|Off] - Displays stars you've collected.");
    expect(psc == std::vector<std::string>({"On", "Off"}), "a description's [On|Off] are its options");
    expect(Commands::options_in("[set|add|query]").size() == 3u, "however many");
    expect(Commands::options_in("[NAME OR ID] - who").empty(), "words with spaces aren't options");
    expect(Commands::options_in("no brackets").empty() && Commands::options_in("[open").empty(), "nor is nothing");
}

void test_registry() {
    Commands commands;
    std::string got;
    int runs = 0;
    expect(commands.add("psc", "[On|Off] - Displays stars you've collected.", [&](std::string_view words) {
        ++runs;
        got = std::string(words);
        return words == "on" || words == "off";
    }),
           "a command is added");
    expect(!commands.add("PSC", "again", [](std::string_view) { return true; }), "a name is taken case aside");
    expect(!commands.add("help", "", [](std::string_view) { return true; }) &&
               !commands.add("confirm", "", [](std::string_view) { return true; }) &&
               !commands.add("two words", "", [](std::string_view) { return true; }) &&
               !commands.add("", "", [](std::string_view) { return true; }) &&
               !commands.add("nothing", "", {}),
           "the chat's own names, spaces, nothing and no handler are refused");

    expect(commands.run("/psc off") && got == "off" && runs == 1, "a command runs with its words");
    expect(commands.run("/PSC on") && got == "on", "its name case aside");
    expect(!commands.run("/psc maybe") && runs == 3, "a command may turn its words down");
    expect(commands.run("/psc  on") == false && got == " on", "only the first space goes");
    expect(!commands.run("/pscx on") && !commands.run("/nope") && !commands.run("psc on"), "others aren't commands");

    std::vector<std::string> help = commands.help();
    expect(help.size() == 1u && help[0] == "/psc [On|Off] - Displays stars you've collected.", "/help lists it");
    expect(commands.describe("psc", "[On|Off|Auto] - new") && commands.help()[0] == "/psc [On|Off|Auto] - new",
           "a description changes");
    expect(!commands.describe("other", "x"), "only a command's");
    expect(commands.complete("/psc a", {}) == std::vector<std::string>({"/psc Auto"}), "with its options");

    // Questions: /confirm answers, anything else drops them.
    int yes = 0;
    commands.ask([&] { ++yes; });
    expect(commands.asking() && commands.help().back() == "CoopCmdConfirmHelp", "a question shows /confirm on /help");
    expect(commands.run("/confirm") && yes == 1 && !commands.asking(), "/confirm answers it");
    expect(!commands.run("/confirm"), "once");
    commands.ask([&] { ++yes; });
    commands.run("/psc on");
    expect(!commands.asking() && !commands.run("/confirm") && yes == 1, "another command drops it");

    ChatCommand hidden;
    hidden.name = "secret";
    hidden.help = "CoopCmdSecretHelp";
    hidden.shown = [] { return false; };
    hidden.run = [](std::string_view) { return true; };
    expect(commands.add(hidden), "a command may come whole");
    expect(commands.help().size() == 1u && commands.complete("/s", {}).empty(), "hidden from /help and Tab");
    expect(commands.run("/secret"), "but it still runs (it says why it won't)");
    commands.remove("secret");
    expect(!commands.run("/secret"), "and goes");
}

void test_complete() {
    Commands commands;
    (void)commands.add("psc", "[On|Off] - stars", [](std::string_view) { return true; });
    (void)commands.add("pause", "", [](std::string_view) { return true; });
    ChatCommand kick;
    kick.name = "kick";
    kick.help = "CoopCmdKickHelp";
    kick.players = true;
    kick.run = [](std::string_view) { return true; };
    (void)commands.add(kick);
    const std::vector<std::string> players{"Mackery", "luigi fan", "Mario"};

    expect(commands.complete("/p", players) == std::vector<std::string>({"/pause", "/psc"}), "names in order");
    expect(commands.complete("/", players) == std::vector<std::string>({"/help", "/kick", "/pause", "/psc"}),
           "every one, /help too");
    expect(commands.complete("/psc ", players) == std::vector<std::string>({"/psc On", "/psc Off"}), "options");
    expect(commands.complete("/psc o", players).size() == 2u && commands.complete("/psc of", players).size() == 1u,
           "matched from their start, case aside");
    expect(commands.complete("/kick ma", players) == std::vector<std::string>({"/kick Mackery", "/kick Mario"}),
           "players for a command that takes them");
    expect(commands.complete("/kick Mario x", players).empty(), "one word only");
    expect(commands.complete("hi ma", players) == std::vector<std::string>({"hi Mackery", "hi Mario"}),
           "a message's last word");
    expect(commands.complete("hi ", players).empty(), "but not nothing");

    // In the chat: each Tab the next, then around; another key starts over.
    Chat chat;
    chat.commands().add("psc", "[On|Off] - stars", [](std::string_view) { return true; });
    chat.open();
    for (const char c : std::string("/psc ")) chat.type(static_cast<char32_t>(c), 0u);
    chat.type(kTextTab, 0u);
    expect(chat.view(0u).input == "/psc On" && chat.view(0u).cursor == 7u, "Tab completes the line");
    chat.type(kTextTab, 0u);
    expect(chat.view(0u).input == "/psc Off", "and again, the next");
    chat.type(kTextTab, 0u);
    expect(chat.view(0u).input == "/psc On", "then around");
    chat.type(kTextBackspace, 0u);
    chat.type('f', 0u);
    chat.type(kTextTab, 0u);
    expect(chat.view(0u).input == "/psc Off", "from the line as typed after another key");
}

void test_chat() {
    Chat chat;
    std::string got;
    chat.commands().add("psc", "[On|Off] - stars", [&](std::string_view words) {
        got = std::string(words);
        if (words != "on" && words != "off") return false;
        chat.say(std::string("Stars ") + std::string(words));
        return true;
    });
    type_line(chat, "/psc off");
    expect(!chat.typing() && got == "off", "Enter on a '/' line runs its command");
    expect(said(chat, "Stars off") && chat.lines().size() == 1u, "what it says shows, and nothing is sent");
    type_line(chat, "/psc what");
    expect(notices(chat).back() == "CoopCmdUnknown", "a command turning its words down says so");
    type_line(chat, "/dance");
    expect(notices(chat).back() == "CoopCmdUnknown", "as does none");
    for (const char* help : {"/help", "/?", "/"}) {
        const std::size_t before = chat.lines().size();
        type_line(chat, help);
        expect(chat.lines().size() == before + 1u && chat.lines().back().text == "/psc [On|Off] - stars", help);
    }
    // A command isn't a message, so a flood of them isn't cut.
    for (int i = 0; i < 10; ++i) type_line(chat, "/psc on");
    expect(notices(chat).back() == "Stars on" && !chat.typing(), "commands have no burst");
}

bool wait_for(const std::function<bool()>& done) {
    for (int i = 0; i < 1000; ++i) {
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

void test_room() {
    // Everyone plays on this machine, so everyone is 127.0.0.1: a ban bans every later joiner.
    Chat host_chat, joiner_chat, third_chat;
    Session host_session, joiner, third;
    host_chat.attach(host_session);
    joiner_chat.attach(joiner);
    third_chat.attach(third);
    add_room_commands(host_chat, host_session);
    add_room_commands(joiner_chat, joiner);
    add_room_commands(third_chat, third);

    HostConfig host;
    host.port = 0u;
    host.game_version = "commands test";
    host.name = "HOSTY";
    host.save = [] { return RoomSave{}; };
    expect(host_session.host(host), "a room opens");
    const auto join = [&](const char* name) {
        JoinConfig config;
        config.address = "127.0.0.1";
        config.port = host_session.port();
        config.game_version = "commands test";
        config.name = name;
        return config;
    };
    expect(joiner.join(join("LUIGI FAN")) && third.join(join("THIRD")), "two join");
    expect(wait_for([&] {
               return joiner.state() == SessionState::Joined && third.state() == SessionState::Joined &&
                      joiner.players().size() == 3u && third.players().size() == 3u;
           }),
           "and everyone is in");

    type_line(joiner_chat, "/players");
    expect(said(joiner_chat, "CoopCmdPlayers") && said(joiner_chat, "CoopCmdPlayerHost", "0 - HOSTY") &&
               said(joiner_chat, "1 - LUIGI FAN") && said(joiner_chat, "2 - THIRD"),
           "/players lists everyone by number");

    // Only the host or a moderator removes players, and asks first.
    type_line(joiner_chat, "/kick THIRD");
    expect(said(joiner_chat, "CoopCmdNoPerms") && !joiner_chat.commands().asking(), "a player may not kick");
    const std::vector<std::string> joiner_help = joiner_chat.commands().help();
    expect(joiner_help == std::vector<std::string>({"CoopCmdPlayersHelp"}), "nor sees how on /help");
    type_line(host_chat, "/kick nobody");
    expect(said(host_chat, "CoopCmdNotFound"), "a name no one has");
    type_line(host_chat, "/kick HOSTY");
    expect(said(host_chat, "CoopCmdSelfKick"), "the host can't kick itself");
    type_line(host_chat, "/moderator 1");
    expect(said(host_chat, "CoopCmdModeratorConfirm", "LUIGI FAN") && host_chat.commands().asking(),
           "a number names a player, and the host is asked first");
    type_line(host_chat, "/confirm");
    expect(said(host_chat, "CoopCmdAddModerator", "LUIGI FAN"), "then makes them a moderator");
    expect(wait_for([&] { return joiner.is_moderator(); }), "who hears of it");
    bool told = false, kept = false;
    for (const SessionEvent& event : joiner.take_events()) told = told || event.kind == SessionEvent::Kind::Moderator;
    for (const SessionEvent& event : host_session.take_events()) {
        kept = kept || (event.kind == SessionEvent::Kind::KeepModerator && event.name == "LUIGI FAN" &&
                        event.text == "127.0.0.1");
    }
    expect(told && kept, "the moderator is told, and the host keeps who they are");

    // A moderator's kick: asked, confirmed, done by the host.
    expect(wait_for([&] {
               const auto list = joiner_chat.commands().help();
               return std::find(list.begin(), list.end(), "CoopCmdKickHelp") != list.end();
           }),
           "a moderator sees /kick on /help");
    type_line(joiner_chat, "/permban THIRD");
    expect(said(joiner_chat, "CoopCmdNoPerms"), "but not /permban's");
    type_line(joiner_chat, "/kick HOSTY");
    expect(said(joiner_chat, "CoopCmdHostKick"), "nor kicks the host");
    type_line(joiner_chat, "/kick third");
    expect(said(joiner_chat, "CoopCmdKickConfirm", "THIRD"), "a name case aside");
    type_line(joiner_chat, "/players");
    type_line(joiner_chat, "/confirm");
    expect(!said(joiner_chat, "CoopCmdKicking"), "another line drops the question");
    type_line(joiner_chat, "/kick THIRD");
    type_line(joiner_chat, "/confirm");
    expect(said(joiner_chat, "CoopCmdKicking", "THIRD"), "/confirm kicks");
    expect(wait_for([&] { return third.state() == SessionState::Idle && host_session.players().size() == 2u; }),
           "the host removes them");
    bool removed = false;
    for (const SessionEvent& event : third.take_events()) {
        removed = removed || (event.kind == SessionEvent::Kind::Closed && event.text == "THE HOST REMOVED YOU FROM THE ROOM");
    }
    expect(removed, "and they are told why");

    // Kicked, they may join again; banned, they may not.
    expect(third.join(join("THIRD")) && wait_for([&] { return third.state() == SessionState::Joined; }),
           "a kicked player joins again");
    expect(wait_for([&] { return host_session.players().size() == 3u; }), "and is in");
    type_line(host_chat, "/ban THIRD");
    type_line(host_chat, "/confirm");
    expect(said(host_chat, "CoopCmdBanning", "THIRD"), "the host bans");
    expect(wait_for([&] { return third.state() == SessionState::Idle; }), "they are out");
    (void)third.take_events();
    expect(third.join(join("THIRD")), "they try again");
    std::string why;
    expect(wait_for([&] {
               for (const SessionEvent& event : third.take_events()) {
                   if (event.kind == SessionEvent::Kind::JoinFailed) why = event.text;
               }
               return !why.empty();
           }) && why == "YOU ARE BANNED FROM THIS ROOM",
           "and are refused while the room is open");

    third.leave();
    joiner.leave();
    host_session.leave();

    // A permanent ban and a moderator, kept, hold in the host's next room.
    Session again, banned, moderator;
    Chat again_chat;
    again_chat.attach(again);
    add_room_commands(again_chat, again);
    host.moderators = {"127.0.0.1"};
    expect(again.host(host), "the host opens another room, with its moderators kept");
    const auto join_again = [&](const char* name) {
        JoinConfig config = join(name);
        config.port = again.port();
        return config;
    };
    expect(moderator.join(join_again("LUIGI FAN")) && wait_for([&] { return moderator.is_moderator(); }),
           "a kept moderator is one again as they join");
    type_line(again_chat, "/permban LUIGI FAN");
    type_line(again_chat, "/confirm");
    expect(said(again_chat, "CoopCmdPermBanning", "LUIGI FAN"), "the host bans for good");
    bool kept_ban = false;
    for (const SessionEvent& event : again.take_events()) {
        kept_ban = kept_ban || (event.kind == SessionEvent::Kind::KeepBan && event.text == "127.0.0.1");
    }
    expect(kept_ban, "and keeps who they are");
    again.leave();

    host.moderators.clear();
    host.bans = {"127.0.0.1"};
    Session last;
    expect(last.host(host), "a later room, with its bans kept");
    JoinConfig refused = join("LUIGI FAN");
    refused.port = last.port();
    why.clear();
    expect(banned.join(refused) && wait_for([&] {
               for (const SessionEvent& event : banned.take_events()) {
                   if (event.kind == SessionEvent::Kind::JoinFailed) why = event.text;
               }
               return !why.empty();
           }) && why == "YOU ARE BANNED FROM THIS ROOM",
           "refuses them");
    last.leave();
}

}  // namespace

int main() {
    test_options();
    test_registry();
    test_complete();
    test_chat();
    test_room();
    if (s_failures == 0) std::printf("coop commands tests passed\n");
    return s_failures == 0 ? 0 : 1;
}
