// galaxy/net/web.h on Linux, macOS and Android: the system's libcurl, loaded when first needed (no build dependency;
// a system without it plays by address only). libcurl gives the request and a TLS stream (CONNECT_ONLY); the
// WebSocket's frames are galaxy/net/web.h's own, which works with every libcurl since 7.45.
#if !defined(_WIN32)

#include "galaxy/net/web.h"

#include <dlfcn.h>
#include <poll.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace galaxy::net {

namespace {

// libcurl's numbers (curl/curl.h), stable across its versions.
constexpr int kOptUrl = 10002, kOptPostFields = 10015, kOptPostFieldSize = 60, kOptHttpHeader = 10023;
constexpr int kOptWriteFunction = 20011, kOptWriteData = 10001, kOptTimeoutMs = 155, kOptConnectTimeoutMs = 156;
constexpr int kOptConnectOnly = 141, kOptNoSignal = 99, kOptUserAgent = 10018;
constexpr int kInfoResponseCode = 0x200002, kInfoActiveSocket = 0x50002C;
constexpr int kCurlOk = 0, kCurlAgain = 81;
constexpr long kGlobalDefault = 3;
constexpr std::size_t kMaxAnswer = 64u * 1024u;

struct Curl {
    void* (*easy_init)() = nullptr;
    int (*easy_setopt)(void*, int, ...) = nullptr;
    int (*easy_perform)(void*) = nullptr;
    int (*easy_getinfo)(void*, int, ...) = nullptr;
    void (*easy_cleanup)(void*) = nullptr;
    int (*easy_send)(void*, const void*, std::size_t, std::size_t*) = nullptr;
    int (*easy_recv)(void*, void*, std::size_t, std::size_t*) = nullptr;
    void* (*slist_append)(void*, const char*) = nullptr;
    void (*slist_free_all)(void*) = nullptr;
    int (*global_init)(long) = nullptr;
};

const Curl* load_curl() {
    static const Curl* loaded = [] () -> const Curl* {
        static const char* const kNames[] = {"libcurl.so.4", "libcurl.so", "libcurl.4.dylib", "libcurl.dylib",
                                             "/usr/lib/libcurl.4.dylib"};
        void* library = nullptr;
        for (const char* name : kNames) {
            library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (library != nullptr) break;
        }
        if (library == nullptr) return nullptr;
        static Curl curl;
        const auto get = [library](auto& out, const char* name) {
            out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(dlsym(library, name));
            return out != nullptr;
        };
        if (!get(curl.easy_init, "curl_easy_init") || !get(curl.easy_setopt, "curl_easy_setopt") ||
            !get(curl.easy_perform, "curl_easy_perform") || !get(curl.easy_getinfo, "curl_easy_getinfo") ||
            !get(curl.easy_cleanup, "curl_easy_cleanup") || !get(curl.easy_send, "curl_easy_send") ||
            !get(curl.easy_recv, "curl_easy_recv") || !get(curl.slist_append, "curl_slist_append") ||
            !get(curl.slist_free_all, "curl_slist_free_all") || !get(curl.global_init, "curl_global_init")) {
            return nullptr;
        }
        if (curl.global_init(kGlobalDefault) != kCurlOk) return nullptr;
        return &curl;
    }();
    return loaded;
}

std::size_t collect(char* data, std::size_t size, std::size_t count, void* user) {
    auto* out = static_cast<std::string*>(user);
    const std::size_t bytes = size * count;
    if (out->size() + bytes > kMaxAnswer) return 0u;  // too long: libcurl stops
    out->append(data, bytes);
    return bytes;
}

// A WebSocket over libcurl's stream. One thread (its own) touches the handle: it sends what send() queued, reads,
// answers pings and passes text messages on.
class CurlWebSocket final : public WebSocket {
public:
    CurlWebSocket(const Curl& curl, void* handle, int socket, WsReader reader)
        : curl_(curl), handle_(handle), socket_(socket), reader_(std::move(reader)), random_(std::random_device{}()) {
        thread_ = std::thread([this] { run(); });
    }
    ~CurlWebSocket() override {
        close();
        if (thread_.joinable()) thread_.join();
        curl_.easy_cleanup(handle_);
    }

    bool send(std::string_view text) override {
        const std::lock_guard<std::mutex> lock(lock_);
        if (closed_) return false;
        outgoing_.push_back(frame(WsOpcode::Text, text));
        return true;
    }

    std::optional<std::string> receive(int wait_ms) override {
        std::unique_lock<std::mutex> lock(lock_);
        changed_.wait_for(lock, std::chrono::milliseconds(wait_ms), [this] { return !incoming_.empty() || closed_; });
        if (incoming_.empty()) return std::nullopt;
        std::string out = std::move(incoming_.front());
        incoming_.pop_front();
        return out;
    }

    [[nodiscard]] bool closed() const override {
        const std::lock_guard<std::mutex> lock(lock_);
        return closed_ && incoming_.empty();
    }

    void close() override {
        const std::lock_guard<std::mutex> lock(lock_);
        if (closing_) return;
        closing_ = true;
        outgoing_.push_back(frame(WsOpcode::Close, {}));
    }

private:
    std::vector<std::uint8_t> frame(WsOpcode opcode, std::string_view payload) {
        std::uint8_t mask[4];
        for (auto& byte : mask) byte = static_cast<std::uint8_t>(random_());
        return ws_frame(opcode,
                        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                                      payload.size()),
                        mask);
    }

    bool write_all(const std::vector<std::uint8_t>& bytes) {
        std::size_t done = 0u;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (done < bytes.size()) {
            std::size_t sent = 0u;
            const int result = curl_.easy_send(handle_, bytes.data() + done, bytes.size() - done, &sent);
            if (result == kCurlOk) {
                done += sent;
                continue;
            }
            if (result != kCurlAgain || std::chrono::steady_clock::now() > until) return false;
            pollfd p{socket_, POLLOUT, 0};
            (void)::poll(&p, 1, 100);
        }
        return true;
    }

    void finish() {
        const std::lock_guard<std::mutex> lock(lock_);
        closed_ = true;
        changed_.notify_all();
    }

    void run() {
        bool sent_close = false;
        const auto deliver = [this] {
            while (auto message = reader_.next()) {
                if (message->opcode == WsOpcode::Text) {
                    const std::lock_guard<std::mutex> lock(lock_);
                    incoming_.push_back(std::move(message->payload));
                    changed_.notify_all();
                } else if (message->opcode == WsOpcode::Ping) {
                    const std::lock_guard<std::mutex> lock(lock_);
                    outgoing_.push_back(frame(WsOpcode::Pong, message->payload));
                } else if (message->opcode == WsOpcode::Close) {
                    return false;
                }
            }
            return !reader_.failed();
        };
        if (!deliver()) return finish();  // frames that came with the handshake's answer
        for (;;) {
            std::deque<std::vector<std::uint8_t>> out;
            bool closing = false;
            {
                const std::lock_guard<std::mutex> lock(lock_);
                out.swap(outgoing_);
                closing = closing_;
            }
            for (const auto& bytes : out) {
                if (!write_all(bytes)) return finish();
            }
            if (closing) {
                if (sent_close) return finish();
                sent_close = true;
            }
            pollfd p{socket_, POLLIN, 0};
            if (::poll(&p, 1, closing ? 500 : 100) <= 0) {
                if (closing) return finish();
                continue;
            }
            std::uint8_t buffer[4096];
            for (;;) {
                std::size_t got = 0u;
                const int result = curl_.easy_recv(handle_, buffer, sizeof(buffer), &got);
                if (result == kCurlAgain) break;
                if (result != kCurlOk || got == 0u) return finish();
                reader_.add(std::span<const std::uint8_t>(buffer, got));
            }
            if (!deliver()) return finish();
        }
    }

    const Curl& curl_;
    void* handle_;
    int socket_;
    WsReader reader_;
    std::mt19937 random_;
    mutable std::mutex lock_;
    std::condition_variable changed_;
    std::deque<std::vector<std::uint8_t>> outgoing_;
    std::deque<std::string> incoming_;
    bool closing_ = false;
    bool closed_ = false;
    std::thread thread_;
};

class CurlWeb final : public Web {
public:
    explicit CurlWeb(const Curl& curl) : curl_(curl) {}

    WebAnswer post(const std::string& url, std::string_view json) override {
        WebAnswer answer;
        void* handle = curl_.easy_init();
        if (handle == nullptr) return answer;
        void* headers = curl_.slist_append(nullptr, "Content-Type: application/json");
        const std::string body(json);
        curl_.easy_setopt(handle, kOptUrl, url.c_str());
        curl_.easy_setopt(handle, kOptHttpHeader, headers);
        curl_.easy_setopt(handle, kOptPostFields, body.c_str());
        curl_.easy_setopt(handle, kOptPostFieldSize, static_cast<long>(body.size()));
        curl_.easy_setopt(handle, kOptWriteFunction, &collect);
        curl_.easy_setopt(handle, kOptWriteData, &answer.body);
        curl_.easy_setopt(handle, kOptTimeoutMs, 10000L);
        curl_.easy_setopt(handle, kOptConnectTimeoutMs, 5000L);
        curl_.easy_setopt(handle, kOptNoSignal, 1L);
        curl_.easy_setopt(handle, kOptUserAgent, "Nebula");
        if (curl_.easy_perform(handle) == kCurlOk) {
            long status = 0;
            curl_.easy_getinfo(handle, kInfoResponseCode, &status);
            answer.status = static_cast<int>(status);
        } else {
            answer.body.clear();
        }
        curl_.slist_free_all(headers);
        curl_.easy_cleanup(handle);
        return answer;
    }

    std::unique_ptr<WebSocket> connect(const std::string& url) override {
        const std::optional<Url> parts = parse_url(url);
        if (!parts) return nullptr;
        void* handle = curl_.easy_init();
        if (handle == nullptr) return nullptr;
        curl_.easy_setopt(handle, kOptUrl, url.c_str());
        curl_.easy_setopt(handle, kOptConnectOnly, 1L);
        curl_.easy_setopt(handle, kOptConnectTimeoutMs, 5000L);
        curl_.easy_setopt(handle, kOptNoSignal, 1L);
        int socket = -1;  // curl_socket_t: an int on these systems
        if (curl_.easy_perform(handle) != kCurlOk ||
            curl_.easy_getinfo(handle, kInfoActiveSocket, &socket) != kCurlOk || socket < 0) {
            curl_.easy_cleanup(handle);
            return nullptr;
        }
        const int fd = socket;
        std::uint8_t nonce[16];
        std::random_device random;
        for (auto& byte : nonce) byte = static_cast<std::uint8_t>(random());
        const std::string key = base64(nonce);
        const std::string request = ws_handshake(*parts, key);
        // The handshake: the request out, the answer's head in (anything after it is the first frames).
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::size_t done = 0u;
        while (done < request.size()) {
            std::size_t sent = 0u;
            const int result = curl_.easy_send(handle, request.data() + done, request.size() - done, &sent);
            if (result == kCurlOk) {
                done += sent;
            } else if (result != kCurlAgain || std::chrono::steady_clock::now() > until) {
                curl_.easy_cleanup(handle);
                return nullptr;
            } else {
                pollfd p{fd, POLLOUT, 0};
                (void)::poll(&p, 1, 100);
            }
        }
        std::string head;
        while (head.find("\r\n\r\n") == std::string::npos) {
            if (std::chrono::steady_clock::now() > until || head.size() > 16384u) {
                curl_.easy_cleanup(handle);
                return nullptr;
            }
            char buffer[2048];
            std::size_t got = 0u;
            const int result = curl_.easy_recv(handle, buffer, sizeof(buffer), &got);
            if (result == kCurlAgain) {
                pollfd p{fd, POLLIN, 0};
                (void)::poll(&p, 1, 100);
                continue;
            }
            if (result != kCurlOk || got == 0u) {
                curl_.easy_cleanup(handle);
                return nullptr;
            }
            head.append(buffer, got);
        }
        const std::size_t end = head.find("\r\n\r\n") + 4u;
        if (!ws_accepts(std::string_view(head).substr(0, end), key)) {
            curl_.easy_cleanup(handle);
            return nullptr;
        }
        WsReader reader;
        reader.add(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(head.data()) + end,
                                                 head.size() - end));
        return std::make_unique<CurlWebSocket>(curl_, handle, fd, std::move(reader));
    }

private:
    const Curl& curl_;
};

}  // namespace

std::shared_ptr<Web> platform_web() {
    const Curl* curl = load_curl();
    if (curl == nullptr) return nullptr;
    return std::make_shared<CurlWeb>(*curl);
}

}  // namespace galaxy::net

#endif
