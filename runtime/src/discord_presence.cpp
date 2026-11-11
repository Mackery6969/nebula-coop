#include "galaxy/discord_presence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
// Discord on a Mac listens on a Unix socket in the user's temporary folder (discord-ipc-0 to 9), with the same frames as
// Windows' pipe.
#define NEBULA_DISCORD_SOCKET 1
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#endif
#if !defined(NEBULA_DISCORD_SOCKET)
#define NEBULA_DISCORD_SOCKET 0
#endif

namespace galaxy::discord {

namespace {
// Discord's IPC opcodes.
constexpr std::uint32_t kOpHandshake = 0u;
constexpr std::uint32_t kOpFrame = 1u;
constexpr std::uint32_t kOpClose = 2u;
constexpr std::uint32_t kOpPing = 3u;
constexpr std::uint32_t kOpPong = 4u;

std::mutex s_mutex;
std::condition_variable s_wake;
std::string s_client_id;
Activity s_activity;
bool (*s_enabled_provider)() = nullptr;
bool s_dirty = true;
bool s_started = false;
std::filesystem::path::string_type s_launch_command;
std::optional<std::string> s_join_secret;

void append_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80u) {
        out += static_cast<char>(c);
    } else if (c < 0x800u) {
        out += static_cast<char>(0xC0u | (c >> 6u));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
    } else if (c < 0x10000u) {
        out += static_cast<char>(0xE0u | (c >> 12u));
        out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (c >> 18u));
        out += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
        out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
    }
}

// Four hex digits at `text[at]`; nullopt when they aren't.
std::optional<std::uint32_t> hex4(std::string_view text, std::size_t at) {
    if (at + 4u > text.size()) return std::nullopt;
    std::uint32_t value = 0u;
    for (std::size_t i = at; i < at + 4u; ++i) {
        const char c = text[i];
        value <<= 4u;
        if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

constexpr std::string_view kJoinPrefix = "nebulanet:";
}  // namespace

std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20u) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += ch;  // UTF-8 passes through
            }
        }
    }
    return out + "\"";
}

std::string frame(std::uint32_t opcode, std::string_view json) {
    std::string out(8u, '\0');
    const std::uint32_t length = static_cast<std::uint32_t>(json.size());
    for (unsigned i = 0; i < 4u; ++i) {
        out[i] = static_cast<char>((opcode >> (8u * i)) & 0xFFu);
        out[4u + i] = static_cast<char>((length >> (8u * i)) & 0xFFu);
    }
    out.append(json);
    return out;
}

bool take_frame(std::string& buffer, std::uint32_t* opcode, std::string* json) {
    if (buffer.size() < 8u) return false;
    std::uint32_t op = 0u, length = 0u;
    for (unsigned i = 0; i < 4u; ++i) {
        op |= static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[i])) << (8u * i);
        length |= static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[4u + i])) << (8u * i);
    }
    if (buffer.size() - 8u < length) return false;
    if (opcode != nullptr) *opcode = op;
    if (json != nullptr) json->assign(buffer, 8u, length);
    buffer.erase(0u, 8u + static_cast<std::size_t>(length));
    return true;
}

std::optional<std::string> json_field(std::string_view json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    for (std::size_t at = json.find(quoted); at != std::string_view::npos; at = json.find(quoted, at + 1u)) {
        std::size_t i = at + quoted.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
        if (i >= json.size() || json[i] != ':') continue;  // a value that happens to be the key's text
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
        if (i >= json.size() || json[i] != '"') return std::nullopt;  // not a string
        std::string out;
        for (++i; i < json.size(); ++i) {
            const char c = json[i];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (++i >= json.size()) return std::nullopt;
            switch (json[i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                std::optional<std::uint32_t> unit = hex4(json, i + 1u);
                if (!unit) return std::nullopt;
                i += 4u;
                std::uint32_t c32 = *unit;
                if (c32 >= 0xD800u && c32 <= 0xDBFFu && i + 2u < json.size() && json[i + 1u] == '\\' &&
                    json[i + 2u] == 'u') {
                    if (const std::optional<std::uint32_t> low = hex4(json, i + 3u);
                        low && *low >= 0xDC00u && *low <= 0xDFFFu) {
                        c32 = 0x10000u + ((c32 - 0xD800u) << 10u) + (*low - 0xDC00u);
                        i += 6u;
                    }
                }
                append_utf8(out, c32);
                break;
            }
            default: out += json[i]; break;  // \" \\ \/
            }
        }
        return std::nullopt;  // cut off
    }
    return std::nullopt;
}

std::string join_secret(std::string_view code, std::string_view password) {
    return std::string(kJoinPrefix) + std::string(code) + ":" + std::string(password);
}

std::optional<JoinRoom> parse_join_secret(std::string_view secret) {
    if (secret.substr(0u, kJoinPrefix.size()) != kJoinPrefix) return std::nullopt;
    secret.remove_prefix(kJoinPrefix.size());
    const std::size_t colon = secret.find(':');
    JoinRoom room;
    room.code = std::string(secret.substr(0u, colon));
    if (colon != std::string_view::npos) room.password = std::string(secret.substr(colon + 1u));  // may hold ':'
    if (room.code.empty()) return std::nullopt;
    return room;
}

std::string party_id(std::string_view code) {
    std::uint64_t hash = 0xCBF29CE484222325ull;  // FNV-1a
    for (const char c : code) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001B3ull;
    }
    char text[32];
    std::snprintf(text, sizeof(text), "nebulanet-%016llx", static_cast<unsigned long long>(hash));
    return text;
}

std::string subscribe_json(std::string_view event, std::uint32_t nonce) {
    return "{\"cmd\":\"SUBSCRIBE\",\"args\":{},\"evt\":" + json_string(event) + ",\"nonce\":\"" +
           std::to_string(nonce) + "\"}";
}

std::string handshake_json(std::string_view client_id) {
    return "{\"v\":1,\"client_id\":" + json_string(client_id) + "}";
}

std::string set_activity_json(std::uint32_t pid, const Activity& activity, std::int64_t start_seconds,
                              std::uint32_t nonce, bool clear, bool platforms) {
    std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
    if (clear) {
        json += "null";
    } else {
        json += "{";
        bool first = true;
        const auto field = [&](const char* name, const std::string& value) {
            if (value.empty()) return;
            json += first ? "" : ",";
            json += "\"" + std::string(name) + "\":" + json_string(value);
            first = false;
        };
        field("details", activity.details);
        field("state", activity.state);
        json += first ? "" : ",";
        json += "\"timestamps\":{\"start\":" + std::to_string(start_seconds) + "}";
        json += ",\"assets\":{\"large_image\":\"smg\",\"large_text\":\"Super Mario Galaxy (Nebula)\"}";
        // An online room: its party ("2 of 8" beside the status) and, with a secret, the Join button.
        const Party& party = activity.party;
        const bool counted = party.size > 0u && party.max >= party.size;
        if (!party.id.empty() || counted) {
            json += ",\"party\":{";
            if (!party.id.empty()) json += "\"id\":" + json_string(party.id);
            if (counted) {
                json += party.id.empty() ? "" : ",";
                json += "\"size\":[" + std::to_string(party.size) + "," + std::to_string(party.max) + "]";
            }
            json += "}";
        }
        if (!party.id.empty() && !party.join_secret.empty()) {
            json += ",\"secrets\":{\"join\":" + json_string(party.join_secret) + "}";
            // Where a Join can start the game: without it Discord offers no "Invite to play" and no Join button
            // (Discord's game invites guide: "check if Rich Presence has party, secret, and platforms set").
            if (platforms) json += ",\"supported_platforms\":[\"desktop\"]";
        }
        json += ",\"instance\":false}";
    }
    json += "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return json;
}

#if defined(_WIN32) || NEBULA_DISCORD_SOCKET
namespace {
#if defined(_WIN32)
std::uint32_t process_id() { return static_cast<std::uint32_t>(GetCurrentProcessId()); }

// The pipe is opened for overlapped I/O: a write or a read waits on an event with a time limit, not on a sleep.
class Pipe {
public:
    Pipe() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Pipe() {
        close();
        if (event_ != nullptr) CloseHandle(event_);
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    bool open() {
        if (event_ == nullptr) return false;
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            handle_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                                  nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return true;
        }
        handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    bool write(const std::string& bytes) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event_;
        DWORD written = 0;
        if (WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &overlapped) == FALSE) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, 2000) != WAIT_OBJECT_0) {
                (void)CancelIoEx(handle_, &overlapped);
                (void)GetOverlappedResult(handle_, &overlapped, &written, TRUE);
                return false;
            }
        }
        return GetOverlappedResult(handle_, &overlapped, &written, FALSE) != FALSE && written == bytes.size();
    }
    // Waits up to `milliseconds` for Discord to send something and appends it to `out`; false when the pipe broke.
    bool read_wait(std::string* out, DWORD milliseconds) {
        char buffer[4096];
        OVERLAPPED overlapped{};
        overlapped.hEvent = event_;
        DWORD got = 0;
        if (ReadFile(handle_, buffer, sizeof(buffer), nullptr, &overlapped) == FALSE) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, milliseconds) != WAIT_OBJECT_0) {
                // Nothing came in time: what arrived while the read was being cancelled still counts.
                (void)CancelIoEx(handle_, &overlapped);
                (void)GetOverlappedResult(handle_, &overlapped, &got, TRUE);
                if (got != 0 && out != nullptr) out->append(buffer, got);
                return true;
            }
        }
        if (GetOverlappedResult(handle_, &overlapped, &got, FALSE) == FALSE) return false;
        if (out != nullptr) out->append(buffer, got);
        return true;
    }
    // Reads whatever Discord has sent (its replies), without waiting; false when the pipe broke.
    bool drain(std::string* first_frame) {
        for (;;) {
            DWORD available = 0;
            if (PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr) == FALSE) return false;
            if (available == 0) return true;
            if (!read_wait(first_frame, 1000)) return false;
        }
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
};
#else
std::uint32_t process_id() { return static_cast<std::uint32_t>(getpid()); }

// Discord's socket, non-blocking once connected: a write or a read waits in poll() with a time limit, not on a sleep.
class Pipe {
public:
    Pipe() = default;
    ~Pipe() { close(); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    bool open() {
        // Where Discord makes it: the folder in TMPDIR on a Mac (XDG_RUNTIME_DIR, TMP and TEMP where those are set).
        const char* const folders[] = {std::getenv("XDG_RUNTIME_DIR"), std::getenv("TMPDIR"), std::getenv("TMP"),
                                       std::getenv("TEMP"), "/tmp"};
        for (const char* folder : folders) {
            if (folder == nullptr || folder[0] == 0) continue;
            std::string base = folder;
            while (base.size() > 1u && base.back() == '/') base.pop_back();
            for (int i = 0; i < 10; ++i) {
                const std::string path = base + "/discord-ipc-" + std::to_string(i);
                sockaddr_un address{};
                if (path.size() >= sizeof(address.sun_path)) continue;
                address.sun_family = AF_UNIX;
                std::memcpy(address.sun_path, path.c_str(), path.size() + 1u);
                const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) return false;
                const int on = 1;
                (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));  // a closed socket is an error, not a signal
                if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
                    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0) {
                    fd_ = fd;
                    return true;
                }
                ::close(fd);
            }
        }
        return false;
    }
    bool is_open() const { return fd_ >= 0; }
    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }
    bool write(const std::string& bytes) {
        std::size_t sent = 0u;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (sent < bytes.size()) {
            const ssize_t n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0 || !wait(POLLOUT, static_cast<int>(left.count()))) return false;
        }
        return true;
    }
    // Waits up to `milliseconds` for Discord to send something and appends it to `out`; false when the socket broke.
    bool read_wait(std::string* out, unsigned milliseconds) {
        pollfd ready{fd_, POLLIN, 0};
        const int got = poll(&ready, 1, static_cast<int>(milliseconds));
        if (got < 0) return errno == EINTR;
        if (got == 0) return true;
        char buffer[4096];
        const ssize_t n = ::recv(fd_, buffer, sizeof(buffer), 0);
        if (n == 0) return false;  // Discord closed it
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        if (out != nullptr) out->append(buffer, static_cast<std::size_t>(n));
        return true;
    }
    // Reads whatever Discord has sent (its replies), without waiting; false when the socket broke.
    bool drain(std::string* first_frame) {
        for (;;) {
            pollfd ready{fd_, POLLIN, 0};
            const int got = poll(&ready, 1, 0);
            if (got < 0) return errno == EINTR;
            if (got == 0) return true;
            if (!read_wait(first_frame, 1000)) return false;
        }
    }

private:
    bool wait(short events, int milliseconds) {
        pollfd ready{fd_, events, 0};
        const int got = poll(&ready, 1, milliseconds);
        return got > 0 && (ready.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0;
    }
    int fd_ = -1;
};
#endif

#if defined(_WIN32)
// How Discord starts Nebula for a Join pressed while it is closed: the URL protocol discord-<client id> under the
// user's own classes (no administrator), as Discord's libraries register it. Rewritten each start, so it follows the
// install; the uninstaller removes it.
void register_launch(const std::string& client_id, const std::wstring& command) {
    if (command.empty()) return;
    const std::wstring key = L"Software\\Classes\\discord-" + std::wstring(client_id.begin(), client_id.end());
    const std::wstring description = L"URL:Run game " + std::wstring(client_id.begin(), client_id.end()) + L" protocol";
    const auto set = [&](const wchar_t* subkey, const wchar_t* name, const std::wstring& value) {
        return RegSetKeyValueW(HKEY_CURRENT_USER, (key + subkey).c_str(), name, REG_SZ, value.c_str(),
                               static_cast<DWORD>((value.size() + 1u) * sizeof(wchar_t))) == ERROR_SUCCESS;
    };
    const bool ok = set(L"", nullptr, description) && set(L"", L"URL Protocol", L"") &&
                    set(L"\\shell\\open\\command", nullptr, command);
    std::fprintf(stderr, ok ? "[discord] Join starts Nebula\n" : "[discord] Join could not be set up to start Nebula\n");
}
#else
void register_launch(const std::string&, const std::filesystem::path::string_type&) {}
#endif

// What the worker keeps of Discord's answers.
struct Replies {
    bool platforms = true;      // supported_platforms is sent (false once this Discord refused it)
    bool show_answer = false;   // log Discord's answer to the status just sent (one with a Join secret)
    bool send_again = false;    // the status must go again (sent without what Discord refused)
};

// What Discord sent: a ping answered, a Join's secret kept for the game, its answers to the status and the
// subscription logged. False when the pipe broke.
bool handle_incoming(Pipe& pipe, std::string& buffer, Replies& replies) {
    std::uint32_t opcode = 0u;
    std::string json;
    while (take_frame(buffer, &opcode, &json)) {
        if (opcode == kOpPing) {
            if (!pipe.write(frame(kOpPong, json))) return false;
            continue;
        }
        if (opcode == kOpClose) return false;
        if (opcode != kOpFrame) continue;
        const std::optional<std::string> event = json_field(json, "evt");
        const std::string command = json_field(json, "cmd").value_or("");
        if (!event) {
            // An answer: the subscription took, or the status as Discord keeps it (what it dropped is missing).
            if (command == "SUBSCRIBE") {
                std::fprintf(stderr, "[discord] Join subscribed\n");
            } else if (command == "SET_ACTIVITY" && replies.show_answer) {
                replies.show_answer = false;
                std::fprintf(stderr, "[discord] status taken: %.600s\n", json.c_str());
            }
            continue;
        }
        if (*event == "ACTIVITY_JOIN") {
            if (std::optional<std::string> secret = json_field(json, "secret")) {
                std::fprintf(stderr, "[discord] Join pressed\n");
                std::lock_guard<std::mutex> lock(s_mutex);
                s_join_secret = std::move(secret);
            }
        } else if (*event == "ERROR") {
            const std::optional<std::string> message = json_field(json, "message");
            std::fprintf(stderr, "[discord] error (%s): %s\n", command.c_str(), message ? message->c_str() : json.c_str());
            // An older Discord that doesn't know supported_platforms: the status goes again without it.
            if (command == "SET_ACTIVITY" && replies.platforms && message &&
                message->find("platform") != std::string::npos) {
                replies.platforms = false;
                replies.send_again = true;
            }
        }
    }
    // A frame that says it is huge isn't Discord's: start again rather than keep it.
    if (buffer.size() > (1u << 20u)) buffer.clear();
    return true;
}

void worker() {
    Pipe pipe;
    std::string incoming;
    Replies replies;
    bool registered = false;
    std::uint32_t nonce = 1u;
    const std::int64_t start = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count();
    auto next_attempt = std::chrono::steady_clock::now();
    bool shown = false;
    for (;;) {
        std::string client_id;
        Activity activity;
        std::filesystem::path::string_type launch_command;
        bool enabled = false, dirty = false;
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_wake.wait_for(lock, std::chrono::seconds(1), [] { return s_dirty; });
            client_id = s_client_id;
            activity = s_activity;
            launch_command = s_launch_command;
            enabled = s_enabled_provider == nullptr || s_enabled_provider();
            dirty = s_dirty;
            s_dirty = false;
        }
        if (!enabled || client_id.empty()) {
            if (pipe.is_open()) {
                // Leaving: Discord is told on a best effort, the pipe closes either way.
                if (shown) (void)pipe.write(frame(kOpFrame, set_activity_json(process_id(), {}, start, nonce++, true)));
                (void)pipe.write(frame(kOpClose, "{}"));
                pipe.close();
                std::fprintf(stderr, "[discord] status off\n");
            }
            shown = false;
            continue;
        }
        if (!pipe.is_open()) {
            if (std::chrono::steady_clock::now() < next_attempt) continue;
            next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            if (!pipe.open()) continue;
            std::string ready;
            bool ok = pipe.write(frame(kOpHandshake, handshake_json(client_id)));
            // Discord answers READY (or closes on an unknown application) within a moment.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (ok && ready.size() < 8u && std::chrono::steady_clock::now() < deadline) {
                ok = pipe.read_wait(&ready, 300);
            }
            if (!ok || ready.size() < 8u || static_cast<unsigned char>(ready[0]) != kOpFrame) {
                std::fprintf(stderr, "[discord] no answer to the handshake; retrying in 15 s\n");
                pipe.close();
                continue;
            }
            std::fprintf(stderr, "[discord] connected\n");
            incoming.clear();
            // A Join pressed in Discord (or an invite's) comes as ACTIVITY_JOIN.
            if (!pipe.write(frame(kOpFrame, subscribe_json("ACTIVITY_JOIN", nonce++)))) {
                pipe.close();
                continue;
            }
            if (!registered) {
                registered = true;
                register_launch(client_id, launch_command);
            }
            dirty = true;
        }
        if (std::exchange(replies.send_again, false)) dirty = true;
        if (dirty) {
            if (!pipe.write(frame(kOpFrame, set_activity_json(process_id(), activity, start, nonce++, false,
                                                              replies.platforms)))) {
                pipe.close();
                shown = false;
                continue;
            }
            shown = true;
            replies.show_answer = !activity.party.join_secret.empty();
            std::fprintf(stderr, "[discord] status: %s | %s%s\n", activity.details.c_str(), activity.state.c_str(),
                         activity.party.size == 0u ? ""
                         : activity.party.join_secret.empty() ? " (in a room)"
                                                               : " (in a room, Join on)");
        }
        if (!pipe.drain(&incoming) || !handle_incoming(pipe, incoming, replies)) {
            pipe.close();
            shown = false;
            std::lock_guard<std::mutex> lock(s_mutex);
            s_dirty = true;  // shown again once Discord is back
        }
    }
}
}  // namespace
#endif

void start(std::string client_id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_started) return;
    s_started = true;
    s_client_id = std::move(client_id);
#if defined(_WIN32)
    if (s_client_id.empty()) return;
    std::thread thread(worker);
    (void)SetThreadPriority(thread.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
    thread.detach();  // ends with the process
#elif NEBULA_DISCORD_SOCKET
    if (s_client_id.empty()) return;
    std::thread([] {
        (void)pthread_setname_np("Nebula Discord");
        (void)pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);  // below the game's threads
        worker();
    }).detach();  // ends with the process
#endif
}

void set_enabled_provider(bool (*provider)()) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_enabled_provider = provider;
}

void set_activity(const Activity& activity) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_activity.details == activity.details && s_activity.state == activity.state) return;
    s_activity.details = activity.details;
    s_activity.state = activity.state;
    s_dirty = true;
    s_wake.notify_one();
}

void set_party(const Party& party) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_activity.party == party) return;
    s_activity.party = party;
    s_dirty = true;
    s_wake.notify_one();
}

void set_launch_command(std::filesystem::path::string_type command) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_launch_command = std::move(command);
}

std::optional<std::string> take_join_secret() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return std::exchange(s_join_secret, std::nullopt);
}

}  // namespace galaxy::discord
