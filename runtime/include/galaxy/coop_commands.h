// Online co-op's chat commands (docs/COOP_PLAN.md, "Chat commands"), as sm64coopdx's: a line typed into the chat
// that starts with '/' runs a command on this player's game instead of going to the room. /help (or /? or /) lists
// them, an unknown one says so, and Tab completes a command's name, its options and players' names.
//
// The room's own (add_room_commands) are sm64coopdx's: /players, and for the host /kick, /ban, /permban and
// /moderator, each asking first (/confirm does it, any other line drops it); a moderator may /kick and /ban. Any
// other feature adds its own with add(), as a sm64coopdx mod does with hook_chat_command (the Personal Star Counter's
// /psc).
//
// What a command says shows in the chat for this player alone (Chat::say). A built-in text is a NebulaText id
// (kCommandTextPrefix...), which the game shows in the played language with {name} filled in; any other is shown as
// it is.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

class Chat;
class Session;

// The NebulaText ids of the commands' own texts start with this: the game looks them up, and shows any other text
// as it is.
inline constexpr std::string_view kCommandTextPrefix = "CoopCmd";
// The longest command name.
inline constexpr std::size_t kCommandNameLimit = 24u;

// A command: /name, then its words.
struct ChatCommand {
    // Runs it with what follows the name (its first space dropped). False when it doesn't take those words: the
    // chat says the command isn't known, as sm64coopdx does for a mod's command that returns false.
    using Handler = std::function<bool(std::string_view words)>;

    std::string name;  // without its '/': letters, digits, '-' and '_'
    // Its line on /help: a NebulaText id (kCommandTextPrefix...) or "/name" and this.
    std::string help;
    std::vector<std::string> options;  // what Tab offers after its name ("on", "off"...)
    bool players = false;              // Tab offers the room's players' names after its name
    std::function<bool()> shown;       // whether /help and Tab show it now (none: always)
    Handler run;
};

class Commands {
public:
    // Adds /`name`, as sm64coopdx's hook_chat_command: `description` is its line on /help after the name, and a
    // "[On|Off]" in it are the options Tab offers. False when the name is taken (case aside) or isn't one.
    bool add(std::string_view name, std::string_view description, ChatCommand::Handler run);
    // Adds one as it is (a NebulaText help line, options, when it shows). False as add's.
    bool add(ChatCommand command);
    // A new description for /`name` (sm64coopdx's update_chat_command_description). False when there is none.
    bool describe(std::string_view name, std::string_view description);
    void remove(std::string_view name);

    // Runs `line` ("/name words"): false when no command takes it. A question asked (ask) is dropped first, but by
    // /confirm, which answers it. /help is the chat's (Chat::type).
    bool run(std::string_view line);
    // A command's question (sm64coopdx's "Type /confirm"): /confirm runs `yes`, any other command line drops it.
    void ask(std::function<void()> yes);
    // Whether a question waits for /confirm.
    [[nodiscard]] bool asking() const;

    // /help's lines: each command shown now, in the order added, then /confirm while a question waits.
    [[nodiscard]] std::vector<std::string> help() const;
    // What Tab may make of `line`, each a whole line, in order: a command's name after '/', then its options or
    // `players` after the name; outside a command, `players` for the last word. Matched from their start, case aside.
    [[nodiscard]] std::vector<std::string> complete(std::string_view line, const std::vector<std::string>& players) const;

    // "[On|Off]" in a description: its words ("On", "Off"); none without one.
    [[nodiscard]] static std::vector<std::string> options_in(std::string_view description);

private:
    [[nodiscard]] const ChatCommand* find(std::string_view name) const;

    mutable std::mutex lock_;
    std::vector<ChatCommand> commands_;
    std::function<void()> question_;
};

// The room's commands (sm64coopdx's /players, /kick, /ban, /permban, /moderator) on `chat`'s, over `session`. Once,
// with the chat attached.
void add_room_commands(Chat& chat, Session& session);

}  // namespace galaxy::coop
