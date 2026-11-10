// SPDX-License-Identifier: GPL-3.0-only
// netbulanet.h. NebulaNet signaling client using WinHTTP.

#include "galaxy/net/netbulanet.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace galaxy::netbulanet {

namespace {

// A simple JSON string escaper for building/reading NebulaNet messages.
static std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

// Extract a JSON string value for a given key from a flat JSON object.
// Returns empty string if not found.
static std::optional<std::string> json_string(std::string_view json, std::string_view key) {
    // Look for "key" : "value"
    std::string needle = std::string('"') + std::string(key) + std::string("\" : \"");
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        // Try without space around colon
        needle = std::string('"') + std::string(key) + std::string("\":\"");
        pos = json.find(needle);
        if (pos == std::string_view::npos) return std::nullopt;
    }
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(json.substr(pos, end - pos));
}

// Extract a JSON numeric value for a given key.
static std::optional<unsigned> json_number(std::string_view json, std::string_view key) {
    std::string needle = std::string('"') + std::string(key) + std::string("\" : ");
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = std::string('"') + std::string(key) + std::string("\":");
        pos = json.find(needle);
        if (pos == std::string_view::npos) return std::nullopt;
    }
    pos += needle.size();
    auto end = json.find_first_of(",}]", pos);
    if (end == std::string_view::npos) end = json.size();
    std::string num(json.substr(pos, end - pos));
    // Trim whitespace
    num.erase(0, num.find_first_not_of(" \t\r\n"));
    num.erase(num.find_last_not_of(" \t\r\n") + 1);
    try { return std::stoul(num); } catch (...) { return std::nullopt; }
}

// Parse "ip:port" into a Candidate.
static Candidate parse_candidate(std::string_view s) {
    Candidate c;
    auto colon = s.rfind(':');
    if (colon != std::string_view::npos) {
        c.type = "stun";
        c.address = std::string(s);
    } else {
        c.address = std::string(s);
    }
    return c;
}

// Build a simple JSON array of candidates as NebulaNet expects:
// [{"type":"stun","address":"ip:port"},...]
static std::string build_candidates_json(const std::vector<Candidate>& candidates) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"type\":\"" << json_escape(candidates[i].type) << "\","
            << "\"address\":\"" << json_escape(candidates[i].address) << "\"}";
    }
    oss << "]";
    return oss.str();
}

// WinHTTP context for async callbacks.
struct WinHttpCtx {
    std::promise<std::string> response_promise;
    std::promise<Error> error_promise;
    std::function<void(std::string)> on_progress;
    bool done = false;
};

// WinHTTP callback.
void __stdcall winhttp_callback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus,
                                LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) {
    auto* ctx = reinterpret_cast<WinHttpCtx*>(dwContext);
    if (!ctx) return;

    switch (dwInternetStatus) {
    case WINHTTP_CALLBACK_STATUS_FLAG_REQUEST_ERROR: {
        auto* info = reinterpret_cast<WINHTTP_WEBREQUEST_CALLBACK_DATA*>(lpvStatusInformation);
        if (info && info->dwError) {
            ctx->error_promise.set_value(Error::Network);
            ctx->done = true;
        }
        break;
    }
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        // Request sent, waiting for response.
        break;
    case WINHTTP_CALLBACK_STATUS_RECEIVE_RESPONSE_COMPLETE:
        // Response headers received, data coming.
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
        if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE) {
            // Check if data is available
            if (lpvStatusInformation == nullptr) break;
            DWORD* pdwBytes = static_cast<DWORD*>(lpvStatusInformation);
            if (*pdwBytes == 0) break;
            // Fall through to read
        }
        // Read available data
        if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            auto* info = reinterpret_cast<WINHTTP_ASYNC_RESULT*>(lpvStatusInformation);
            if (!info || info->dwResult == 0) break;
        }
        // Try to read data
        std::array<char, 4096> buf;
        DWORD bytesRead = 0;
        if (WinHttpReadData(hInternet, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead) && bytesRead > 0) {
            ctx->response_promise.get_future().wait();  // ensure buffer exists
            // We'll use a different approach: accumulate in the promise
        }
        break;
    }
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
        ctx->error_promise.set_value(Error::Network);
        ctx->done = true;
        break;
    }
}

// A blocking HTTP POST that returns the response body and status code.
struct HttpResponse {
    DWORD status = 0;
    std::string body;
};

static HttpResponse http_post(std::string_view server, std::string_view path, std::string_view body,
                              std::function<void(std::string)> on_progress = nullptr) {
    HttpResponse result;

    // Parse server URL
    std::string url(server.begin(), server.end());
    std::wstring wurl(url.begin(), url.end());

    // Open session
    HINTERNET hSession = WinHttpOpen(L"NebulaNet", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        result.status = 0;
        return result;
    }

    WinHttpSetTimeouts(hSession, 15000, 15000, 15000, 15000);

    // Parse host and use SSL
    std::wstring whost, wpath;
    bool secure = false;
    auto scheme_end = wurl.find(L"://");
    if (scheme_end != std::string::npos) {
        secure = (wurl.substr(0, scheme_end) == L"https");
        whost = wurl.substr(scheme_end + 3);
    } else {
        whost = wurl;
    }
    auto slash_pos = whost.find(L'/');
    if (slash_pos != std::string::npos) {
        wpath = whost.substr(slash_pos);
        whost = whost.substr(0, slash_pos);
    } else {
        wpath = L"/";
    }

    // Remove port from host if present
    auto colon_pos = whost.find(L':');
    if (colon_pos != std::string::npos) {
        whost = whost.substr(0, colon_pos);
    }

    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
                                        secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
                                        0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        result.status = 0;
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = 0;
        return result;
    }

    // Set protocols
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    // Set headers
    std::wstring content_type = L"Content-Type: application/json\r\n";
    std::wstring content_length;
    content_length.append(L"Content-Length: ");
    content_length.append(std::to_wstring(body.size()));
    content_length.append(L"\r\n");

    std::wstring headers = content_type + content_length + L"\r\n";
    std::string body_str(body.begin(), body.end());

    if (!WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<char*>(body_str.c_str()), static_cast<DWORD>(body_str.size()),
                            static_cast<DWORD>(body_str.size()), 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = 0;
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = 0;
        return result;
    }

    // Read response
    DWORD total_bytes = 0;
    std::string response_body;
    std::array<char, 4096> buf;
    DWORD bytes_read = 0;

    do {
        bytes_read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), static_cast<DWORD>(buf.size()), &bytes_read)) {
            break;
        }
        if (bytes_read > 0) {
            response_body.append(buf.data(), bytes_read);
            total_bytes += bytes_read;
            if (on_progress) {
                on_progress("Received " + std::to_string(total_bytes) + " bytes");
            }
        }
    } while (bytes_read > 0);

    // Get status code
    DWORD status_size = sizeof(result.status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &status_size, WINHTTP_NO_HEADER_INDEX);

    result.body = std::move(response_body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

}  // namespace

std::string normalize_server_url(std::string_view server) noexcept {
    std::string s(server);
    if (s.empty()) return "https://nebulanet.mackery.com";
    if (s.find("://") == std::string::npos) {
        s = "https://" + s;
    }
    return s;
}

std::future<std::variant<RoomInfo, Error>> create_room(
    std::string_view server,
    std::string_view game,
    std::string_view version,
    std::string_view room_name,
    unsigned players,
    unsigned max_players,
    const std::vector<Candidate>& candidates,
    std::function<void(std::string)> on_progress) {

    auto fut = std::async(std::launch::async, [server = std::string(server), game = std::string(game),
                                                version = std::string(version),
                                                room_name = std::string(room_name),
                                                players, max_players, candidates,
                                                on_progress]() -> std::variant<RoomInfo, Error> {
        std::string normalized = normalize_server_url(server);

        if (on_progress) on_progress("Connecting to NebulaNet...");

        // Build the host message
        std::ostringstream msg;
        msg << "{\"t\":\"open\",\"game\":\"" << json_escape(game) << "\",\"version\":\""
            << json_escape(version) << "\",\"name\":\"" << json_escape(room_name) << "\",\"players\":"
            << players << ",\"max\":" << max_players << ",\"candidates\":" << build_candidates_json(candidates)
            << "}";

        if (on_progress) on_progress("Creating room...");

        HttpResponse resp = http_post(normalized, "/v1/host?game=" + json_escape(game) + "&version=" +
                                                     json_escape(version) + "&name=" + json_escape(room_name) +
                                                     "&players=" + std::to_string(players) +
                                                     "&max=" + std::to_string(max_players),
                                      msg.str(), on_progress);

        if (resp.status == 0) {
            return Error::Network;
        }

        if (resp.status == 200) {
            std::string code = json_string(resp.body, "code").value_or("");
            if (code.empty()) return Error::Server;
            RoomInfo info;
            info.code = code;
            info.server = normalized;
            if (on_progress) on_progress("Room created: " + code);
            return info;
        }

        // Check for error in response
        if (auto err = json_string(resp.body, "error")) {
            if (on_progress) on_progress("Error: " + *err);
        }

        if (resp.status == 409) return Error::Taken;
        return Error::Server;
    });

    return fut;
}

std::future<Error> close_room(std::string_view server, std::string_view code) {
    auto fut = std::async(std::launch::async, [server = std::string(server), code = std::string(code)]() -> Error {
        std::string normalized = normalize_server_url(server);

        HttpResponse resp = http_post(normalized, "/v1/host/" + json_escape(code), "", nullptr);

        if (resp.status == 0) return Error::Network;
        if (resp.status == 200 || resp.status == 404) return Error::None;  // 404: already closed
        return Error::Server;
    });

    return fut;
}

std::future<std::variant<JoinInfo, Error>> join_room(
    std::string_view server,
    std::string_view code,
    const std::vector<Candidate>& candidates,
    std::function<void(std::string)> on_progress) {

    auto fut = std::async(std::launch::async, [server = std::string(server), code = std::string(code),
                                               candidates, on_progress]() -> std::variant<JoinInfo, Error> {
        std::string normalized = normalize_server_url(server);

        if (on_progress) on_progress("Looking up room...");

        std::ostringstream msg;
        msg << "{\"code\":\"" << json_escape(code) << "\",\"candidates\":" << build_candidates_json(candidates)
            << "}";

        HttpResponse resp = http_post(normalized, "/v1/join", msg.str(), on_progress);

        if (resp.status == 0) {
            return Error::Network;
        }

        if (resp.status == 200) {
            JoinInfo info;
            info.name = json_string(resp.body, "name").value_or("");
            info.game = json_string(resp.body, "game").value_or("");
            info.version = json_string(resp.body, "version").value_or("");
            info.players = json_number(resp.body, "players").value_or(0);
            info.max = json_number(resp.body, "max").value_or(0);

            // Parse candidates from the response
            // Simple extraction of candidate strings from the JSON
            std::string candidates_str = json_string(resp.body, "candidates").value_or("[]");
            // Extract each candidate's address
            std::string::size_type pos = 0;
            while ((pos = candidates_str.find("\"address\"", pos)) != std::string::npos) {
                pos += 10;  // skip "\"address\""
                pos = candidates_str.find(':', pos);
                if (pos == std::string::npos) break;
                ++pos;
                pos = candidates_str.find('"', pos);
                if (pos == std::string::npos) break;
                ++pos;
                auto end = candidates_str.find('"', pos);
                if (end == std::string::npos) break;
                info.candidates.push_back(parse_candidate(candidates_str.substr(pos, end - pos)));
                pos = end + 1;
            }

            if (on_progress) on_progress("Room found: " + info.name);
            return info;
        }

        if (resp.status == 404) return Error::NotFound;

        if (auto err = json_string(resp.body, "error")) {
            if (on_progress) on_progress("Error: " + *err);
        }

        return Error::Server;
    });

    return fut;
}

}  // namespace galaxy::netbulanet
