// HTTPS and WebSockets, as NebulaNet needs them (galaxy/nebulanet.h): one request and its answer, and a WebSocket of
// text messages. Windows has them through WinHTTP (web_win.cpp); Linux and macOS through the system's libcurl, loaded
// when first needed (web_curl.cpp), with the WebSocket's frames written here (ws_*). Android has none yet, so a game
// there plays by address only.
//
// Every call blocks: NebulaNet's own thread makes them, never the game's or the session's.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::net {

struct WebAnswer {
    int status = 0;  // the HTTP status; 0: no answer (no network, the name didn't resolve, TLS failed)
    std::string body;
};

class WebSocket {
public:
    virtual ~WebSocket() = default;
    // Sends a text message. False once the socket is closed.
    virtual bool send(std::string_view text) = 0;
    // The next text message, waiting at most `wait_ms` for one; nothing on time out. Once the socket closed, nothing
    // at once (closed() says so).
    virtual std::optional<std::string> receive(int wait_ms) = 0;
    [[nodiscard]] virtual bool closed() const = 0;
    // Closes it (from any thread: a receive waiting returns).
    virtual void close() = 0;
};

class Web {
public:
    virtual ~Web() = default;
    // POSTs a JSON body to `url` ("https://host/path", or "http://" for a local test server).
    virtual WebAnswer post(const std::string& url, std::string_view json) = 0;
    // Opens a WebSocket to `url` ("https://host/path" or "http://", as the WebSocket's wss:// and ws://). Null when
    // it can't (the server refused, no network).
    virtual std::unique_ptr<WebSocket> connect(const std::string& url) = 0;
};

// This platform's, or null where there is none (or its library is missing).
[[nodiscard]] std::shared_ptr<Web> platform_web();

// A URL's parts: "https://host:port/path" (http, https; IPv6 hosts in brackets).
struct Url {
    bool secure = true;
    std::string host;
    std::uint16_t port = 443u;
    std::string path = "/";
};
[[nodiscard]] std::optional<Url> parse_url(std::string_view url);

// The WebSocket's own frames (RFC 6455), for a library that gives a TLS stream but no WebSocket.
// The opening request for `url`'s host and path, with `key` (16 random bytes, base64).
[[nodiscard]] std::string ws_handshake(const Url& url, std::string_view key);
// Whether `response` (the head up to its blank line) accepts the handshake made with `key`.
[[nodiscard]] bool ws_accepts(std::string_view response, std::string_view key);
// 16 bytes as base64.
[[nodiscard]] std::string base64(std::span<const std::uint8_t> bytes);
// A client's frame (masked with `mask`): a text message, a close, a ping or a pong.
enum class WsOpcode : std::uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};
[[nodiscard]] std::vector<std::uint8_t> ws_frame(WsOpcode opcode, std::span<const std::uint8_t> payload,
                                                 const std::uint8_t (&mask)[4]);
// Takes a server's frames out of `buffer` as they complete, joining fragmented messages.
class WsReader {
public:
    struct Message {
        WsOpcode opcode = WsOpcode::Text;
        std::string payload;
    };
    void add(std::span<const std::uint8_t> bytes);
    // The next whole message (a text, a close, a ping or a pong), or nothing until more bytes come. A frame broken
    // (too long, masked by the server, a control frame fragmented) sets failed().
    std::optional<Message> next();
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    std::vector<std::uint8_t> buffer_;
    std::string partial_;
    WsOpcode partial_opcode_ = WsOpcode::Text;
    bool failed_ = false;
};

}  // namespace galaxy::net
