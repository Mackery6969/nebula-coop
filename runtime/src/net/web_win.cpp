// galaxy/net/web.h on Windows: WinHTTP, the system's own HTTPS and WebSocket client (Windows 8 and later), with the
// system's certificates and proxy settings.
#if defined(_WIN32)

#include "galaxy/net/web.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace galaxy::net {

namespace {

constexpr std::size_t kMaxAnswer = 64u * 1024u;

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

struct Handle {
    HINTERNET handle = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET h) : handle(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Handle() { reset(); }
    void reset() {
        if (handle != nullptr) WinHttpCloseHandle(handle);
        handle = nullptr;
    }
    explicit operator bool() const { return handle != nullptr; }
};

// A WebSocket: WinHTTP's receive blocks, so a thread of its own waits on it and passes text messages on.
class WinWebSocket final : public WebSocket {
public:
    WinWebSocket(Handle connection, Handle socket) : connection_(std::move(connection)), socket_(std::move(socket)) {
        thread_ = std::thread([this] { run(); });
    }
    ~WinWebSocket() override {
        close();
        if (thread_.joinable()) thread_.join();
    }

    bool send(std::string_view text) override {
        const std::lock_guard<std::mutex> sending(send_lock_);
        {
            const std::lock_guard<std::mutex> lock(lock_);
            if (closed_ || closing_) return false;
        }
        const DWORD result = WinHttpWebSocketSend(socket_.handle, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                                  const_cast<char*>(text.data()), static_cast<DWORD>(text.size()));
        return result == NO_ERROR;
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
        {
            const std::lock_guard<std::mutex> lock(lock_);
            if (closing_) return;
            closing_ = true;
        }
        // Tells the server, then the receive waiting on the thread returns.
        const std::lock_guard<std::mutex> sending(send_lock_);
        (void)WinHttpWebSocketShutdown(socket_.handle, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        socket_.reset();
    }

private:
    void run() {
        std::string message;
        std::vector<char> buffer(4096);
        for (;;) {
            HINTERNET socket = nullptr;
            {
                const std::lock_guard<std::mutex> lock(lock_);
                if (!closing_) socket = socket_.handle;
            }
            if (socket == nullptr) break;
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD result = WinHttpWebSocketReceive(socket, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                         &read, &type);
            if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE &&
                type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                continue;  // binary: not NebulaNet's
            }
            message.append(buffer.data(), read);
            if (message.size() > kMaxAnswer) break;
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                const std::lock_guard<std::mutex> lock(lock_);
                incoming_.push_back(std::move(message));
                message.clear();
                changed_.notify_all();
            }
        }
        const std::lock_guard<std::mutex> lock(lock_);
        closed_ = true;
        changed_.notify_all();
    }

    Handle connection_;
    Handle socket_;
    std::mutex send_lock_;  // a send and the close, never at once
    mutable std::mutex lock_;
    std::condition_variable changed_;
    std::deque<std::string> incoming_;
    bool closing_ = false;
    bool closed_ = false;
    std::thread thread_;
};

class WinWeb final : public Web {
public:
    WinWeb() {
        session_ = Handle(WinHttpOpen(L"Nebula", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
        // Windows 8.0 has no automatic proxy: the default one.
        if (!session_) {
            session_ = Handle(WinHttpOpen(L"Nebula", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS, 0));
        }
        if (session_) (void)WinHttpSetTimeouts(session_.handle, 5000, 5000, 10000, 10000);
    }
    [[nodiscard]] bool ready() const { return static_cast<bool>(session_); }

    WebAnswer post(const std::string& url, std::string_view json) override {
        WebAnswer answer;
        Handle connection;
        Handle request = open(url, L"POST", connection);
        if (!request) return answer;
        static const wchar_t kHeaders[] = L"Content-Type: application/json\r\n";
        if (!WinHttpSendRequest(request.handle, kHeaders, static_cast<DWORD>(-1L), const_cast<char*>(json.data()),
                                static_cast<DWORD>(json.size()), static_cast<DWORD>(json.size()), 0) ||
            !WinHttpReceiveResponse(request.handle, nullptr)) {
            return answer;
        }
        answer.status = status(request.handle);
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.handle, &available) || available == 0) break;
            if (answer.body.size() + available > kMaxAnswer) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request.handle, chunk.data(), available, &read) || read == 0) break;
            answer.body.append(chunk.data(), read);
        }
        return answer;
    }

    std::unique_ptr<WebSocket> connect(const std::string& url) override {
        Handle connection;
        Handle request = open(url, L"GET", connection);
        if (!request) return nullptr;
        if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
            !WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.handle, nullptr) || status(request.handle) != 101) {
            return nullptr;
        }
        Handle socket(WinHttpWebSocketCompleteUpgrade(request.handle, 0));
        if (!socket) return nullptr;
        return std::make_unique<WinWebSocket>(std::move(connection), std::move(socket));
    }

private:
    Handle open(const std::string& url, const wchar_t* verb, Handle& connection) {
        const std::optional<Url> parts = parse_url(url);
        if (!parts || !session_) return {};
        connection = Handle(WinHttpConnect(session_.handle, wide(parts->host).c_str(), parts->port, 0));
        if (!connection) return {};
        return Handle(WinHttpOpenRequest(connection.handle, verb, wide(parts->path).c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         parts->secure ? WINHTTP_FLAG_SECURE : 0));
    }

    static int status(HINTERNET request) {
        DWORD code = 0;
        DWORD size = sizeof(code);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) {
            return 0;
        }
        return static_cast<int>(code);
    }

    Handle session_;
};

}  // namespace

std::shared_ptr<Web> platform_web() {
    static const std::shared_ptr<WinWeb> web = [] {
        auto made = std::make_shared<WinWeb>();
        return made->ready() ? made : nullptr;
    }();
    return web;
}

}  // namespace galaxy::net

#endif
