#include "galaxy/coop_chat.h"

#include "galaxy/coop_menu.h"
#include "galaxy/net/bytes.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace galaxy::coop {

std::uint64_t chat_clock_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

std::string clean_chat_text(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (out.size() >= kChatMessageLimit) break;
        if (c >= 0x20 && c < 0x7f) out += c;
    }
    const std::size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out.erase(0, first);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void Chat::attach(Session& session) {
    {
        const std::lock_guard<std::mutex> lock(lock_);
        session_ = &session;
    }
    session.on_message(kChatMessage, [this](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        receive(from, data, chat_clock_ms());
    });
}

void Chat::receive(const SessionPlayer& from, std::span<const std::uint8_t> data, std::uint64_t now_ms) {
    net::ByteReader in(data);
    // Read past the limit, then cut: a longer message (another build's) still shows its start.
    std::string text = clean_chat_text(in.str(kChatMessageLimit * 4u));
    if (!in.ok() || text.empty()) return;
    const std::lock_guard<std::mutex> lock(lock_);
    if (!allow(from.id, now_ms)) return;
    ChatLine line;
    line.kind = ChatLine::Kind::Message;
    line.name = from.name;
    line.character = from.character;
    line.local = from.local;
    line.text = std::move(text);
    line.at_ms = now_ms;
    add(std::move(line));
}

void Chat::notice(ChatLine::Kind kind, std::string name, std::uint64_t now_ms) {
    ChatLine line;
    line.kind = kind;
    line.name = std::move(name);
    line.at_ms = now_ms;
    const std::lock_guard<std::mutex> lock(lock_);
    add(std::move(line));
}

void Chat::say(std::string text, std::string name) {
    ChatLine line;
    line.kind = ChatLine::Kind::Notice;
    line.name = std::move(name);
    line.text = std::move(text);
    line.local = true;
    line.at_ms = chat_clock_ms();
    const std::lock_guard<std::mutex> lock(lock_);
    add(std::move(line));
}

void Chat::clear() {
    const std::lock_guard<std::mutex> lock(lock_);
    lines_.clear();
    recent_.clear();
    if (typing_) ++input_serial_;
    typing_ = false;
    input_.clear();
    cursor_ = 0u;
    completing_ = false;
}

bool Chat::open() {
    const std::lock_guard<std::mutex> lock(lock_);
    if (typing_) return false;
    typing_ = true;
    input_.clear();
    cursor_ = 0u;
    ++input_serial_;
    return true;
}

void Chat::close() {
    const std::lock_guard<std::mutex> lock(lock_);
    if (!typing_) return;
    typing_ = false;
    input_.clear();
    cursor_ = 0u;
    ++input_serial_;
}

bool Chat::typing() const {
    const std::lock_guard<std::mutex> lock(lock_);
    return typing_;
}

bool Chat::type(char32_t key, std::uint64_t now_ms) {
    if (key == kTextTab) {
        // The players' names, from the session without the chat's lock (its thread holds its own giving lines).
        Session* session = nullptr;
        {
            const std::lock_guard<std::mutex> lock(lock_);
            if (!typing_) return false;
            session = session_;
        }
        std::vector<std::string> names;
        if (session != nullptr) {
            for (const SessionPlayer& player : session->players()) names.push_back(player.name);
        }
        complete(names);
        return true;
    }
    std::string text;
    {
        const std::lock_guard<std::mutex> lock(lock_);
        if (!typing_) return false;
        completing_ = false;
        switch (key) {
        case kTextEnter:
            text = clean_chat_text(input_);
            break;
        case kTextEscape:
            typing_ = false;
            input_.clear();
            cursor_ = 0u;
            break;
        case kTextBackspace:
            if (cursor_ > 0u) input_.erase(--cursor_, 1u);
            break;
        case kTextDelete:
            if (cursor_ < input_.size()) input_.erase(cursor_, 1u);
            break;
        case kTextLeft:
            if (cursor_ > 0u) --cursor_;
            break;
        case kTextRight:
            if (cursor_ < input_.size()) ++cursor_;
            break;
        case kTextHome:
        case kTextUp: cursor_ = 0u; break;
        case kTextEnd:
        case kTextDown: cursor_ = input_.size(); break;
        default:
            if (key < 0x20u || key >= 0x7fu || input_.size() >= kChatMessageLimit) return true;
            input_.insert(cursor_++, 1u, static_cast<char>(key));
            break;
        }
        ++input_serial_;
        if (key != kTextEnter) return true;
        if (text.empty() || text.front() == '/') {
            // Nothing to say, or a command: the line closes.
            typing_ = false;
            input_.clear();
            cursor_ = 0u;
            if (text.empty()) return true;
        }
    }
    if (text.front() == '/') {
        run_command(text);
    } else {
        send(text, now_ms);
    }
    return true;
}

// A '/' line: /help (or /? or /) lists the commands, another runs; one no command takes says so, as sm64coopdx's.
void Chat::run_command(const std::string& line) {
    if (line == "/" || line == "/help" || line == "/?") {
        for (std::string& help : commands_.help()) say(std::move(help));
        return;
    }
    if (!commands_.run(line)) say("CoopCmdUnknown");
}

void Chat::complete(const std::vector<std::string>& players) {
    const std::lock_guard<std::mutex> lock(lock_);
    if (!typing_) return;
    if (!completing_) {
        completing_ = true;
        completion_base_ = input_;
        completion_next_ = 0u;
    }
    const std::vector<std::string> found = commands_.complete(completion_base_, players);
    if (found.empty()) return;
    set_input(found[completion_next_ % found.size()]);
    completion_next_ = (completion_next_ + 1u) % found.size();
}

void Chat::set_input(std::string text) {
    if (text.size() > kChatMessageLimit) text.resize(kChatMessageLimit);
    input_ = std::move(text);
    cursor_ = input_.size();
    ++input_serial_;
}

// Enter on a line with words: to the room, and shown here as everyone sees it. A line past this player's burst stays
// typed (sent once the burst passes); outside a room it is dropped.
void Chat::send(const std::string& text, std::uint64_t now_ms) {
    Session* session = nullptr;
    {
        const std::lock_guard<std::mutex> lock(lock_);
        session = session_;
    }
    // The session is called without the chat's lock: its thread holds its own while it gives the chat a message.
    std::optional<SessionPlayer> self;
    if (session != nullptr) {
        for (const SessionPlayer& player : session->players()) {
            if (player.local) self = player;
        }
    }
    {
        const std::lock_guard<std::mutex> lock(lock_);
        if (self && !allow(self->id, now_ms)) return;
        typing_ = false;
        input_.clear();
        cursor_ = 0u;
        ++input_serial_;
    }
    if (!self) return;
    net::ByteWriter body;
    body.str(text);
    if (!session->send(kChatMessage, net::Channel::Ordered, body.bytes())) return;
    ChatLine line;
    line.kind = ChatLine::Kind::Message;
    line.name = self->name;
    line.character = self->character;
    line.local = true;
    line.text = text;
    line.at_ms = now_ms;
    const std::lock_guard<std::mutex> lock(lock_);
    add(std::move(line));
}

ChatView Chat::view(std::uint64_t now_ms) const {
    const std::lock_guard<std::mutex> lock(lock_);
    ChatView view;
    view.typing = typing_;
    view.input = input_;
    view.cursor = cursor_;
    view.input_serial = input_serial_;
    std::size_t first = lines_.size() - std::min(lines_.size(), kChatShownLines);
    for (std::size_t i = first; i < lines_.size(); ++i) {
        const ChatLine& line = lines_[i];
        float alpha = 1.0f;
        if (!typing_) {
            const std::uint64_t age = now_ms > line.at_ms ? now_ms - line.at_ms : 0u;
            if (age >= kChatLineMs) continue;
            if (age > kChatLineMs - kChatFadeMs) {
                alpha = static_cast<float>(kChatLineMs - age) / static_cast<float>(kChatFadeMs);
            }
        }
        view.lines.push_back({line, alpha});
    }
    return view;
}

std::vector<ChatLine> Chat::lines() const {
    const std::lock_guard<std::mutex> lock(lock_);
    return lines_;
}

void Chat::add(ChatLine line) {
    line.id = next_id_++;
    if (next_id_ == 0u) next_id_ = 1u;
    lines_.push_back(std::move(line));
    if (lines_.size() > kChatLogLimit) lines_.erase(lines_.begin(), lines_.end() - kChatLogLimit);
}

bool Chat::allow(net::PeerId from, std::uint64_t now_ms) {
    std::erase_if(recent_, [now_ms](const auto& sent) {
        return now_ms > sent.second && now_ms - sent.second >= kChatBurstMs;
    });
    const auto count = std::count_if(recent_.begin(), recent_.end(), [from](const auto& sent) { return sent.first == from; });
    if (static_cast<std::size_t>(count) >= kChatBurstLines) return false;
    recent_.emplace_back(from, now_ms);
    return true;
}

}  // namespace galaxy::coop
