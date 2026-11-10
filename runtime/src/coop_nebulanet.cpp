#include "galaxy/coop_nebulanet.h"

#include "galaxy/net/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace galaxy::coop {

namespace {

constexpr int kReceiveWaitMs = 200;
constexpr auto kKeepalive = std::chrono::seconds(30);
constexpr auto kRetryFirst = std::chrono::seconds(2);
constexpr auto kRetryMost = std::chrono::seconds(30);

std::string server_url(std::string server, std::string_view path) {
    while (!server.empty() && server.back() == '/') server.pop_back();
    return server + std::string(path);
}

std::string candidates_json(const std::vector<std::string>& candidates) {
    std::string out = "[";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i != 0u) out += ',';
        out += net::json::quote(candidates[i]);
    }
    return out + "]";
}

// A room as NebulaNet lists it or answers a join with it: the fields every answer has.
NebulaNetRoom room_of(const net::json::Value& json) {
    NebulaNetRoom room;
    room.code = json.text("code");
    room.name = json.text("name");
    room.version = json.text("version");
    room.mode = json.text("mode");
    room.description = json.text("description");
    room.players = static_cast<unsigned>(std::max(0LL, json.integer("players", 1)));
    room.max = static_cast<unsigned>(std::max(0LL, json.integer("max", 8)));
    const net::json::Value* locked = json.find("private");
    room.locked = locked != nullptr && locked->kind == net::json::Value::Kind::Bool && locked->boolean;
    return room;
}

std::vector<std::string> valid_candidates(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    for (const std::string& text : in) {
        net::Address address;
        if (parse_candidate(text, address) && out.size() < 6u) out.push_back(text);
    }
    return out;
}

}  // namespace

std::optional<std::string> parse_room_code(std::string_view text) {
    std::string code;
    for (const char c : text) {
        if (c == ' ' || c == '-' || c == '\t') continue;
        const char upper = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        if (kRoomCodeLetters.find(upper) == std::string_view::npos) return std::nullopt;
        code += upper;
    }
    if (code.size() != kRoomCodeLength) return std::nullopt;
    return code;
}

std::string candidate_text(const net::Address& address) {
    if (!address.valid()) return {};
    return address.ipv6() ? "[" + address.host_text() + "]:" + std::to_string(address.port())
                          : address.host_text() + ":" + std::to_string(address.port());
}

bool parse_candidate(std::string_view text, net::Address& address) {
    std::string host;
    std::string_view port;
    if (text.starts_with("[")) {
        const std::size_t close = text.find("]:");
        if (close == std::string_view::npos) return false;
        host = std::string(text.substr(1, close - 1));
        port = text.substr(close + 2);
    } else {
        const std::size_t colon = text.rfind(':');
        if (colon == std::string_view::npos || text.find(':') != colon) return false;
        host = std::string(text.substr(0, colon));
        port = text.substr(colon + 1);
        // Numbers and dots only: no name is ever looked up.
        if (host.empty() || host.find_first_not_of("0123456789.") != std::string::npos) return false;
    }
    if (host.empty() || host.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos) return false;
    if (port.empty() || port.size() > 5u || port.find_first_not_of("0123456789") != std::string_view::npos) {
        return false;
    }
    const unsigned number = static_cast<unsigned>(std::stoul(std::string(port)));
    if (number == 0u || number > 65535u) return false;
    return net::resolve(host, static_cast<std::uint16_t>(number), address);
}

// What the link's thread and the session share: the thread may outlive the link (stop() doesn't wait for it).
struct NebulaNetLink::Shared {
    std::mutex lock;
    std::vector<NebulaNetEvent> events;
    std::atomic<bool> stopped{false};
    std::atomic<unsigned> players{1u};
    std::shared_ptr<net::WebSocket> socket;  // a host's, while connected (stop() closes it)

    void push(NebulaNetEvent event) {
        const std::lock_guard<std::mutex> guard(lock);
        if (!stopped.load()) events.push_back(std::move(event));
    }
    void fail(std::string why) {
        NebulaNetEvent event;
        event.kind = NebulaNetEvent::Kind::Failed;
        event.text = std::move(why);
        push(std::move(event));
    }
};

NebulaNetLink::NebulaNetLink(std::shared_ptr<net::Web> web) : web_(std::move(web)) {}

NebulaNetLink::~NebulaNetLink() { stop(); }

void NebulaNetLink::stop() {
    if (!shared_) return;
    shared_->stopped.store(true);
    std::shared_ptr<net::WebSocket> socket;
    {
        const std::lock_guard<std::mutex> guard(shared_->lock);
        socket = shared_->socket;
        shared_->events.clear();
    }
    if (socket) socket->close();
    shared_.reset();
}

std::vector<NebulaNetEvent> NebulaNetLink::take_events() {
    std::vector<NebulaNetEvent> out;
    if (!shared_) return out;
    const std::lock_guard<std::mutex> guard(shared_->lock);
    out.swap(shared_->events);
    return out;
}

void NebulaNetLink::set_players(unsigned players) {
    if (shared_) shared_->players.store(std::max(players, 1u));
}

void NebulaNetLink::host(std::string server, NebulaNetRoom room) {
    stop();
    shared_ = std::make_shared<Shared>();
    shared_->players.store(std::max(room.players, 1u));
    std::thread([shared = shared_, web = web_, server = std::move(server), room = std::move(room)] {
        if (!web) {
            shared->fail("NEBULANET ISN'T AVAILABLE ON THIS SYSTEM");
            return;
        }
        std::string code;  // kept across reconnections, so friends' codes keep working
        auto retry = std::chrono::duration_cast<std::chrono::milliseconds>(kRetryFirst);
        bool told_lost = false;
        while (!shared->stopped.load()) {
            const std::string url = server_url(server, code.empty() ? "/v1/host" : "/v1/host?code=" + code);
            std::shared_ptr<net::WebSocket> socket = web->connect(url);
            bool listed = false;
            if (socket) {
                {
                    const std::lock_guard<std::mutex> guard(shared->lock);
                    shared->socket = socket;
                }
                if (shared->stopped.load()) socket->close();
                unsigned players = shared->players.load();
                const std::string open =
                    "{\"t\":\"open\",\"protocol\":" + std::to_string(kNebulaNetProtocol) +
                    ",\"game\":\"Nebula\",\"version\":" + net::json::quote(room.version) +
                    ",\"name\":" + net::json::quote(room.name) + ",\"mode\":" + net::json::quote(room.mode) +
                    ",\"description\":" + net::json::quote(room.description) +
                    ",\"password\":" + net::json::quote(room.password) + ",\"players\":" + std::to_string(players) +
                    ",\"max\":" + std::to_string(room.max) +
                    ",\"candidates\":" + candidates_json(room.candidates) + "}";
                (void)socket->send(open);
                auto last_sent = std::chrono::steady_clock::now();
                while (!shared->stopped.load() && !socket->closed()) {
                    if (const std::optional<std::string> text = socket->receive(kReceiveWaitMs)) {
                        if (*text == "pong") continue;
                        const std::optional<net::json::Value> message = net::json::parse(*text);
                        if (!message) continue;
                        const std::string type = message->text("t");
                        if (type == "room") {
                            const std::optional<std::string> got = parse_room_code(message->text("code"));
                            if (!got) continue;
                            code = *got;
                            listed = true;
                            told_lost = false;
                            retry = std::chrono::duration_cast<std::chrono::milliseconds>(kRetryFirst);
                            NebulaNetEvent event;
                            event.kind = NebulaNetEvent::Kind::RoomCode;
                            event.text = code;
                            shared->push(std::move(event));
                        } else if (type == "joiner") {
                            NebulaNetEvent event;
                            event.kind = NebulaNetEvent::Kind::Joiner;
                            event.candidates = valid_candidates(message->strings("candidates"));
                            if (!event.candidates.empty()) shared->push(std::move(event));
                        } else if (type == "joiner_failed") {
                            NebulaNetEvent event;
                            event.kind = NebulaNetEvent::Kind::JoinerFailed;
                            event.candidates = valid_candidates(message->strings("candidates"));
                            shared->push(std::move(event));
                        } else if (type == "error") {
                            shared->fail(message->text("error") == "old_game"
                                             ? "NEBULANET NO LONGER TAKES THIS VERSION OF NEBULA: UPDATE IT"
                                             : "NEBULANET REFUSED THE ROOM (" + message->text("error") + ")");
                        }
                    }
                    const auto now = std::chrono::steady_clock::now();
                    const unsigned now_players = shared->players.load();
                    if (listed && now_players != players) {
                        players = now_players;
                        (void)socket->send("{\"t\":\"update\",\"players\":" + std::to_string(players) + "}");
                        last_sent = now;
                    } else if (now - last_sent > kKeepalive) {
                        (void)socket->send("ping");
                        last_sent = now;
                    }
                }
                socket->close();
                const std::lock_guard<std::mutex> guard(shared->lock);
                shared->socket.reset();
            }
            if (shared->stopped.load()) return;
            if (!told_lost) {
                shared->fail(code.empty() ? "NEBULANET COULD NOT BE REACHED: FRIENDS CAN JOIN BY ADDRESS"
                                          : "NEBULANET WAS LOST: TRYING AGAIN");
                told_lost = true;
            }
            // Waits before trying again, longer each time, a little at a time so stop() isn't kept waiting.
            const auto until = std::chrono::steady_clock::now() + retry;
            while (!shared->stopped.load() && std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            retry = std::min(retry * 2, std::chrono::duration_cast<std::chrono::milliseconds>(kRetryMost));
        }
    }).detach();
}

void NebulaNetLink::join(std::string server, std::string code, std::vector<std::string> candidates,
                         std::string password) {
    stop();
    shared_ = std::make_shared<Shared>();
    std::thread([shared = shared_, web = web_, server = std::move(server), code = std::move(code),
                 candidates = std::move(candidates), password = std::move(password)] {
        if (!web) {
            shared->fail("NEBULANET ISN'T AVAILABLE ON THIS SYSTEM");
            return;
        }
        const std::string body = "{\"code\":" + net::json::quote(code) + ",\"candidates\":" +
                                 candidates_json(candidates) + ",\"password\":" + net::json::quote(password) +
                                 ",\"protocol\":" + std::to_string(kNebulaNetProtocol) + "}";
        const net::WebAnswer answer = web->post(server_url(server, "/v1/join"), body);
        if (shared->stopped.load()) return;
        const std::optional<net::json::Value> json = net::json::parse(answer.body);
        if (answer.status == 200 && json) {
            NebulaNetEvent event;
            event.kind = NebulaNetEvent::Kind::Answer;
            event.room = room_of(*json);
            event.room.code = code;
            event.room.candidates = valid_candidates(json->strings("candidates"));
            if (!event.room.candidates.empty()) {
                shared->push(std::move(event));
                return;
            }
            shared->fail("NEBULANET'S ANSWER HAD NO ADDRESS");
            return;
        }
        switch (answer.status) {
        case 0: shared->fail("NEBULANET COULD NOT BE REACHED"); return;
        case 404: shared->fail("NO ROOM HAS THAT CODE"); return;
        case 403: shared->fail("WRONG PASSWORD: TYPE THE ROOM'S ON THE JOIN PAGE"); return;
        case 409: shared->fail("THE ROOM IS FULL"); return;
        case 426: shared->fail("NEBULANET NO LONGER TAKES THIS VERSION OF NEBULA: UPDATE IT"); return;
        case 429:
        case 503: shared->fail("NEBULANET IS BUSY: TRY AGAIN IN A MINUTE"); return;
        default: shared->fail("NEBULANET ANSWERED " + std::to_string(answer.status)); return;
        }
    }).detach();
}

void NebulaNetLink::list(std::string server, std::string password) {
    stop();
    shared_ = std::make_shared<Shared>();
    std::thread([shared = shared_, web = web_, server = std::move(server), password = std::move(password)] {
        NebulaNetEvent event;
        event.kind = NebulaNetEvent::Kind::Rooms;
        if (!web) {
            event.text = "NEBULANET ISN'T AVAILABLE ON THIS SYSTEM";
            shared->push(std::move(event));
            return;
        }
        const std::string body = "{\"game\":\"Nebula\",\"password\":" + net::json::quote(password) + "}";
        const net::WebAnswer answer = web->post(server_url(server, "/v1/list"), body);
        if (shared->stopped.load()) return;
        const std::optional<net::json::Value> json = net::json::parse(answer.body);
        const net::json::Value* rooms = json ? json->find("rooms") : nullptr;
        if (answer.status == 200 && rooms != nullptr && rooms->kind == net::json::Value::Kind::Array) {
            for (const net::json::Value& item : rooms->array) {
                if (item.kind != net::json::Value::Kind::Object) continue;
                NebulaNetRoom room = room_of(item);
                if (!parse_room_code(room.code) || room.name.empty()) continue;
                event.rooms.push_back(std::move(room));
            }
        } else {
            event.text = answer.status == 0     ? "NEBULANET COULD NOT BE REACHED"
                         : answer.status == 429 ? "NEBULANET IS BUSY: TRY AGAIN IN A MINUTE"
                         : answer.status == 404 ? "THIS NEBULANET HAS NO ROOM LIST"
                                                : "NEBULANET ANSWERED " + std::to_string(answer.status);
        }
        shared->push(std::move(event));
    }).detach();
}

void NebulaNetLink::report(std::shared_ptr<net::Web> web, std::string server, std::string code, bool ok,
                           std::vector<std::string> candidates) {
    if (!web || code.empty()) return;
    std::thread([web = std::move(web), server = std::move(server), code = std::move(code), ok,
                 candidates = std::move(candidates)] {
        std::string body = "{\"code\":" + net::json::quote(code) + ",\"ok\":" + (ok ? "true" : "false");
        if (!candidates.empty()) body += ",\"candidates\":" + candidates_json(candidates);
        body += "}";
        (void)web->post(server_url(server, "/v1/report"), body);
    }).detach();
}

}  // namespace galaxy::coop
