// Online co-op's chat (docs/COOP_PLAN.md, "Chat"), as sm64coopdx's: a keyboard opens a line (T), types a message and
// sends it to everyone in the room with Enter (Escape drops it). Each line shows with its sender's name in their
// character's colour, and joins and leaves show there too. Controllers have no way to type, so their players read it:
// the newest lines show over play for a while and fade, and all of the last few show while a line is typed.
//
// A message is its text alone: who said it is the session's sender, so a player can't speak as another. Its text is
// printable ASCII (what the game's font draws), cut to kChatMessageLimit. Chat may be called from any thread; the
// session's thread adds the lines that come.
//
// A line starting with '/' is a command (galaxy/coop_commands.h), run here and never sent: what it says shows for
// this player alone. Tab completes it, and a player's name in a message.
#pragma once

#include "galaxy/coop_commands.h"
#include "galaxy/coop_session.h"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

// The session message a chat line travels in (a feature's, galaxy/coop_session.h).
inline constexpr std::uint8_t kChatMessage = kFirstFeatureMessage + 16u;
// The longest message, in characters.
inline constexpr std::size_t kChatMessageLimit = 100u;
// The lines kept, and the most shown at once.
inline constexpr std::size_t kChatLogLimit = 64u;
inline constexpr std::size_t kChatShownLines = 8u;
// A line shows this long over play, the last part of it fading.
inline constexpr std::uint64_t kChatLineMs = 10000u;
inline constexpr std::uint64_t kChatFadeMs = 1000u;
// A player's lines past this many in kChatBurstMs are dropped (a flood), theirs as everyone else's.
inline constexpr std::size_t kChatBurstLines = 5u;
inline constexpr std::uint64_t kChatBurstMs = 3000u;

struct ChatLine {
    enum class Kind : std::uint8_t {
        Message,  // `name` said `text`
        Joined,   // `name` joined the room
        Left,     // `name` left it
        Notice,   // a command's words to this player: `text`, a NebulaText id (kCommandTextPrefix...) with {name}
                  // as `name`, or words shown as they are
    };
    std::uint32_t id = 0u;  // its own number, from 1: a line never changes, so a drawn one is drawn again as is
    Kind kind = Kind::Message;
    std::string name;
    CoopCharacter character = CoopCharacter::Mario;  // a message's sender's
    bool local = false;  // this player's own
    std::string text;
    std::uint64_t at_ms = 0u;
};

struct ChatView {
    struct Line {
        ChatLine line;
        float alpha = 1.0f;  // 1, down to 0 as it fades
    };
    std::vector<Line> lines;  // oldest first, at most kChatShownLines
    bool typing = false;
    std::string input;
    std::size_t cursor = 0u;
    std::uint32_t input_serial = 0u;  // changes with the typed text, its cursor or the typing starting
};

// The clock the chat's times are on (milliseconds, steady): the session's thread stamps the lines that come with it.
[[nodiscard]] std::uint64_t chat_clock_ms();

// A message as everyone sees it: printable ASCII, no spaces at its ends, at most kChatMessageLimit characters.
[[nodiscard]] std::string clean_chat_text(std::string_view text);

class Chat {
public:
    // Takes the session's chat messages and sends this player's with it. Once, before the session hosts or joins.
    void attach(Session& session);

    // A message `from` came (the session's thread, or tests).
    void receive(const SessionPlayer& from, std::span<const std::uint8_t> data, std::uint64_t now_ms);
    // Someone joined or left the room.
    void notice(ChatLine::Kind kind, std::string name, std::uint64_t now_ms);
    // A command's words, for this player alone (a Notice line).
    void say(std::string text, std::string name = {});
    // The commands a '/' line runs.
    [[nodiscard]] Commands& commands() { return commands_; }
    // The room ended: no lines, no typing.
    void clear();

    // Starts typing a line; false when one is typed already.
    bool open();
    // Drops the line typed.
    void close();
    [[nodiscard]] bool typing() const;
    // A key typed into the line (a printable character or a galaxy::coop::TextKey). Enter sends it to the room (or
    // runs its command) and closes it, Escape drops it, Tab completes it. False when no line is typed.
    bool type(char32_t key, std::uint64_t now_ms);

    // What shows at `now_ms`: while typing, the last kChatShownLines lines; otherwise the ones still showing.
    [[nodiscard]] ChatView view(std::uint64_t now_ms) const;
    // Every line kept, oldest first.
    [[nodiscard]] std::vector<ChatLine> lines() const;

private:
    void add(ChatLine line);
    // Whether a line from `from` at `now_ms` is within their burst (and counts it).
    bool allow(net::PeerId from, std::uint64_t now_ms);
    void send(const std::string& text, std::uint64_t now_ms);
    void run_command(const std::string& line);
    // Tab: the next of what the line may become (Commands::complete), from the line as the first Tab found it.
    void complete(const std::vector<std::string>& players);
    void set_input(std::string text);

    mutable std::mutex lock_;
    Session* session_ = nullptr;
    std::vector<ChatLine> lines_;
    std::uint32_t next_id_ = 1u;
    std::vector<std::pair<net::PeerId, std::uint64_t>> recent_;  // who sent a line when, within kChatBurstMs
    bool typing_ = false;
    std::string input_;
    std::size_t cursor_ = 0u;
    std::uint32_t input_serial_ = 0u;
    // Tabs in a row: the line before the first, and the next completion they give.
    bool completing_ = false;
    std::string completion_base_;
    std::size_t completion_next_ = 0u;
    Commands commands_;
};

}  // namespace galaxy::coop
