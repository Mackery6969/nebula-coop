#include "galaxy/coop_commands.h"

#include "galaxy/coop_chat.h"
#include "galaxy/coop_session.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace galaxy::coop {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return lower(x) == lower(y);
           });
}

bool starts(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && same(text.substr(0, prefix.size()), prefix);
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > kCommandNameLimit) return false;
    // /help, /? and /confirm are the chat's own.
    if (same(name, "help") || same(name, "confirm")) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

}  // namespace

std::vector<std::string> Commands::options_in(std::string_view description) {
    std::vector<std::string> out;
    const std::size_t open = description.find('[');
    const std::size_t close = open == std::string_view::npos ? open : description.find(']', open);
    if (close == std::string_view::npos) return out;
    const std::string_view inside = description.substr(open + 1u, close - open - 1u);
    // As sm64coopdx: words with no spaces between the bars; anything else is the description's own words.
    if (inside.empty() || inside.find(' ') != std::string_view::npos) return out;
    std::size_t start = 0u;
    while (start <= inside.size()) {
        const std::size_t bar = std::min(inside.find('|', start), inside.size());
        if (bar > start) out.emplace_back(inside.substr(start, bar - start));
        start = bar + 1u;
    }
    return out;
}

bool Commands::add(std::string_view name, std::string_view description, ChatCommand::Handler run) {
    ChatCommand command;
    command.name = std::string(name);
    command.help = std::string(description);
    command.options = options_in(description);
    command.run = std::move(run);
    return add(std::move(command));
}

bool Commands::add(ChatCommand command) {
    if (!valid_name(command.name) || !command.run) return false;
    const std::lock_guard<std::mutex> lock(lock_);
    if (find(command.name) != nullptr) return false;
    commands_.push_back(std::move(command));
    return true;
}

bool Commands::describe(std::string_view name, std::string_view description) {
    const std::lock_guard<std::mutex> lock(lock_);
    for (ChatCommand& command : commands_) {
        if (!same(command.name, name)) continue;
        command.help = std::string(description);
        command.options = options_in(description);
        return true;
    }
    return false;
}

void Commands::remove(std::string_view name) {
    const std::lock_guard<std::mutex> lock(lock_);
    std::erase_if(commands_, [name](const ChatCommand& command) { return same(command.name, name); });
}

const ChatCommand* Commands::find(std::string_view name) const {
    for (const ChatCommand& command : commands_) {
        if (same(command.name, name)) return &command;
    }
    return nullptr;
}

bool Commands::run(std::string_view line) {
    if (line.empty() || line.front() != '/') return false;
    line.remove_prefix(1u);
    const std::size_t space = line.find(' ');
    const std::string_view name = line.substr(0, space);
    const std::string_view words = space == std::string_view::npos ? std::string_view() : line.substr(space + 1u);
    std::function<void()> question;
    ChatCommand::Handler handler;
    {
        const std::lock_guard<std::mutex> lock(lock_);
        // Whatever is typed next answers a question: /confirm says yes, anything else no.
        question = std::exchange(question_, {});
        if (!(question && same(name, "confirm") && words.empty())) {
            question = {};
            if (const ChatCommand* command = find(name)) handler = command->run;
        }
    }
    // Run without the lock: a command may ask, describe or add.
    if (question) {
        question();
        return true;
    }
    return handler && handler(words);
}

void Commands::ask(std::function<void()> yes) {
    const std::lock_guard<std::mutex> lock(lock_);
    question_ = std::move(yes);
}

bool Commands::asking() const {
    const std::lock_guard<std::mutex> lock(lock_);
    return static_cast<bool>(question_);
}

std::vector<std::string> Commands::help() const {
    std::vector<ChatCommand> commands;
    bool asking = false;
    {
        const std::lock_guard<std::mutex> lock(lock_);
        commands = commands_;
        asking = static_cast<bool>(question_);
    }
    std::vector<std::string> out;
    for (const ChatCommand& command : commands) {
        if (command.shown && !command.shown()) continue;
        if (command.help.starts_with(kCommandTextPrefix)) {
            out.push_back(command.help);
        } else {
            out.push_back("/" + command.name + (command.help.empty() ? "" : " " + command.help));
        }
    }
    if (asking) out.emplace_back("CoopCmdConfirmHelp");
    return out;
}

std::vector<std::string> Commands::complete(std::string_view line, const std::vector<std::string>& players) const {
    std::vector<ChatCommand> commands;
    bool asking = false;
    {
        const std::lock_guard<std::mutex> lock(lock_);
        commands = commands_;
        asking = static_cast<bool>(question_);
    }
    std::erase_if(commands, [](const ChatCommand& command) { return command.shown && !command.shown(); });
    const auto by_name = [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) { return lower(x) < lower(y); });
    };
    std::vector<std::string> out;
    const auto add_players = [&](std::string_view before, std::string_view word) {
        std::vector<std::string> names;
        for (const std::string& name : players) {
            if (starts(name, word) && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        }
        std::sort(names.begin(), names.end(), by_name);
        for (const std::string& name : names) out.push_back(std::string(before) + name);
    };
    if (line.empty() || line.front() != '/') {
        // A message: a player's name for its last word.
        const std::size_t space = line.rfind(' ');
        const std::size_t start = space == std::string_view::npos ? 0u : space + 1u;
        if (start < line.size()) add_players(line.substr(0, start), line.substr(start));
        return out;
    }
    const std::string_view typed = line.substr(1u);
    const std::size_t space = typed.find(' ');
    if (space == std::string_view::npos) {
        std::vector<std::string> names{"help"};
        if (asking) names.emplace_back("confirm");
        for (const ChatCommand& command : commands) names.push_back(command.name);
        std::sort(names.begin(), names.end(), by_name);
        for (const std::string& name : names) {
            if (starts(name, typed)) out.push_back("/" + name);
        }
        return out;
    }
    const std::string_view name = typed.substr(0, space);
    const std::string_view word = typed.substr(space + 1u);
    if (word.find(' ') != std::string_view::npos) return out;
    const auto it = std::find_if(commands.begin(), commands.end(),
                                 [name](const ChatCommand& command) { return same(command.name, name); });
    if (it == commands.end()) return out;
    const std::string before = "/" + it->name + " ";
    for (const std::string& option : it->options) {
        if (starts(option, word)) out.push_back(before + option);
    }
    if (it->players) add_players(before, word);
    return out;
}

void add_room_commands(Chat& chat, Session& session) {
    Commands& commands = chat.commands();
    // A player by their number or their name (sm64coopdx's chat_get_network_player): exactly, then case aside.
    const auto find = [&session](std::string_view who) -> std::optional<SessionPlayer> {
        const std::vector<SessionPlayer> players = session.players();
        if (!who.empty() && std::all_of(who.begin(), who.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            for (const SessionPlayer& player : players) {
                if (std::to_string(player.index) == who) return player;
            }
        }
        for (const SessionPlayer& player : players) {
            if (player.name == who) return player;
        }
        for (const SessionPlayer& player : players) {
            if (same(player.name, who)) return player;
        }
        return std::nullopt;
    };
    const auto host_or_moderator = [&session] { return session.is_host() || session.is_moderator(); };
    const auto host = [&session] { return session.is_host(); };

    ChatCommand players;
    players.name = "players";
    players.help = "CoopCmdPlayersHelp";
    players.run = [&chat, &session](std::string_view words) {
        if (!words.empty()) return false;
        chat.say("CoopCmdPlayers");
        for (const SessionPlayer& player : session.players()) {
            std::string line = std::to_string(player.index) + " - " + player.name;
            chat.say(player.host ? "CoopCmdPlayerHost" : player.moderator ? "CoopCmdPlayerModerator" : line, line);
        }
        return true;
    };
    (void)commands.add(std::move(players));

    // /kick, /ban and /permban: who, then a question that /confirm answers.
    struct Removing {
        const char* name;
        const char* help;
        Removal how;
        const char* confirm;
        const char* doing;
        const char* self;
    };
    static constexpr Removing kRemovals[] = {
        {"kick", "CoopCmdKickHelp", Removal::Kick, "CoopCmdKickConfirm", "CoopCmdKicking", "CoopCmdSelfKick"},
        {"ban", "CoopCmdBanHelp", Removal::Ban, "CoopCmdBanConfirm", "CoopCmdBanning", "CoopCmdSelfBan"},
        {"permban", "CoopCmdPermBanHelp", Removal::PermanentBan, "CoopCmdPermBanConfirm", "CoopCmdPermBanning",
         "CoopCmdSelfBan"},
    };
    for (const Removing& removing : kRemovals) {
        ChatCommand command;
        command.name = removing.name;
        command.help = removing.help;
        command.players = true;
        const bool host_only = removing.how == Removal::PermanentBan;
        command.shown = host_only ? std::function<bool()>(host) : std::function<bool()>(host_or_moderator);
        command.run = [&chat, &session, find, removing, host_only](std::string_view words) {
            const bool allowed = host_only ? session.is_host() : session.is_host() || session.is_moderator();
            if (!allowed) {
                chat.say("CoopCmdNoPerms");
                return true;
            }
            const std::optional<SessionPlayer> player = find(words);
            if (!player) {
                chat.say("CoopCmdNotFound");
                return true;
            }
            if (player->local) {
                chat.say(removing.self);
                return true;
            }
            if (player->host) {
                chat.say("CoopCmdHostKick");
                return true;
            }
            chat.say(removing.confirm, player->name);
            chat.commands().ask([&chat, &session, removing, id = player->id, name = player->name] {
                if (!session.remove_player(id, removing.how)) return;
                chat.say(removing.doing, name);
            });
            return true;
        };
        (void)commands.add(std::move(command));
    }

    ChatCommand moderator;
    moderator.name = "moderator";
    moderator.help = "CoopCmdModeratorHelp";
    moderator.players = true;
    moderator.shown = host;
    moderator.run = [&chat, &session, find](std::string_view words) {
        if (!session.is_host()) {
            chat.say("CoopCmdHostOnly");
            return true;
        }
        const std::optional<SessionPlayer> player = find(words);
        if (!player) {
            chat.say("CoopCmdNotFound");
            return true;
        }
        if (player->local) {
            chat.say("CoopCmdSelfModerator");
            return true;
        }
        chat.say("CoopCmdModeratorConfirm", player->name);
        chat.commands().ask([&chat, &session, id = player->id, name = player->name] {
            if (!session.make_moderator(id)) return;
            chat.say("CoopCmdAddModerator", name);
        });
        return true;
    };
    (void)commands.add(std::move(moderator));
}

}  // namespace galaxy::coop
