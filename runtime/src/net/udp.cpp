#include "galaxy/net/udp.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
// Not declared by every SDK's headers (it's in mstcpip.h behind version checks).
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace galaxy::net {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
constexpr Native kNone = INVALID_SOCKET;

// Winsock, started once for the process and left running (it ends with the process).
bool start_sockets() {
    static std::once_flag once;
    static bool started = false;
    std::call_once(once, [] {
        WSADATA data{};
        started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return started;
}
void close_native(Native socket) { closesocket(socket); }
#else
using Native = int;
constexpr Native kNone = -1;
bool start_sockets() { return true; }
void close_native(Native socket) { ::close(socket); }
#endif

Native native(std::intptr_t handle) { return static_cast<Native>(handle); }

// An IPv4 address mapped into IPv6 (::ffff:a.b.c.d) as the IPv4 address itself.
sockaddr_storage unmapped(const Address& address) {
    sockaddr_storage out{};
    std::memcpy(&out, address.data(), std::min<std::size_t>(address.length(), sizeof(out)));
    if (out.ss_family == AF_INET6) {
        const auto* six = reinterpret_cast<const sockaddr_in6*>(&out);
        if (IN6_IS_ADDR_V4MAPPED(&six->sin6_addr)) {
            sockaddr_in four{};
            four.sin_family = AF_INET;
            four.sin_port = six->sin6_port;
            std::memcpy(&four.sin_addr, reinterpret_cast<const std::uint8_t*>(&six->sin6_addr) + 12, 4);
            out = {};
            std::memcpy(&out, &four, sizeof(four));
        }
    }
    return out;
}

}  // namespace

bool Address::ipv6() const noexcept {
    const sockaddr_storage address = unmapped(*this);
    return address.ss_family == AF_INET6;
}

std::uint16_t Address::port() const noexcept {
    const sockaddr_storage address = unmapped(*this);
    if (address.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
    if (address.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    return 0u;
}

std::string Address::to_string() const {
    if (!valid()) return "(none)";
    const sockaddr_storage address = unmapped(*this);
    char text[INET6_ADDRSTRLEN + 1]{};
    if (address.ss_family == AF_INET) {
        const auto* four = reinterpret_cast<const sockaddr_in*>(&address);
        inet_ntop(AF_INET, &four->sin_addr, text, sizeof(text));
        return std::string(text) + ':' + std::to_string(ntohs(four->sin_port));
    }
    if (address.ss_family == AF_INET6) {
        const auto* six = reinterpret_cast<const sockaddr_in6*>(&address);
        inet_ntop(AF_INET6, &six->sin6_addr, text, sizeof(text));
        return '[' + std::string(text) + "]:" + std::to_string(ntohs(six->sin6_port));
    }
    return "(unknown)";
}

std::string Address::host_text() const {
    if (!valid()) return {};
    const sockaddr_storage address = unmapped(*this);
    char text[INET6_ADDRSTRLEN + 1]{};
    if (address.ss_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(&address)->sin_addr, text, sizeof(text));
    } else if (address.ss_family == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr, text, sizeof(text));
    }
    return text;
}

std::size_t Address::ip_bytes(std::span<std::uint8_t, 16> out) const noexcept {
    if (!valid()) return 0u;
    const sockaddr_storage address = unmapped(*this);
    if (address.ss_family == AF_INET) {
        std::memcpy(out.data(), &reinterpret_cast<const sockaddr_in*>(&address)->sin_addr, 4);
        return 4u;
    }
    if (address.ss_family == AF_INET6) {
        std::memcpy(out.data(), &reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr, 16);
        return 16u;
    }
    return 0u;
}

Address make_address(std::span<const std::uint8_t> ip, std::uint16_t port) noexcept {
    Address out;
    if (ip.size() == 4u) {
        sockaddr_in four{};
        four.sin_family = AF_INET;
        four.sin_port = htons(port);
        std::memcpy(&four.sin_addr, ip.data(), 4);
        std::memcpy(out.data(), &four, sizeof(four));
        out.set_length(static_cast<std::uint32_t>(sizeof(four)));
    } else if (ip.size() == 16u) {
        sockaddr_in6 six{};
        six.sin6_family = AF_INET6;
        six.sin6_port = htons(port);
        std::memcpy(&six.sin6_addr, ip.data(), 16);
        std::memcpy(out.data(), &six, sizeof(six));
        out.set_length(static_cast<std::uint32_t>(sizeof(six)));
    }
    return out;
}

bool operator==(const Address& a, const Address& b) noexcept {
    const sockaddr_storage x = unmapped(a);
    const sockaddr_storage y = unmapped(b);
    if (x.ss_family != y.ss_family) return false;
    if (x.ss_family == AF_INET) {
        const auto* p = reinterpret_cast<const sockaddr_in*>(&x);
        const auto* q = reinterpret_cast<const sockaddr_in*>(&y);
        return p->sin_port == q->sin_port && std::memcmp(&p->sin_addr, &q->sin_addr, sizeof(p->sin_addr)) == 0;
    }
    if (x.ss_family == AF_INET6) {
        const auto* p = reinterpret_cast<const sockaddr_in6*>(&x);
        const auto* q = reinterpret_cast<const sockaddr_in6*>(&y);
        return p->sin6_port == q->sin6_port && std::memcmp(&p->sin6_addr, &q->sin6_addr, sizeof(p->sin6_addr)) == 0;
    }
    return false;
}

bool resolve(std::string_view host, std::uint16_t port, Address& address) {
    if (!start_sockets()) return false;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    const std::string name = host.empty() ? std::string("localhost") : std::string(host);
    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    if (getaddrinfo(name.c_str(), service.c_str(), &hints, &results) != 0 || results == nullptr) return false;
    // IPv4 first: a host's address that has both is reached most often by it (routers forward IPv4 ports).
    const addrinfo* chosen = nullptr;
    for (const addrinfo* at = results; at != nullptr; at = at->ai_next) {
        if (at->ai_family == AF_INET) {
            chosen = at;
            break;
        }
        if (chosen == nullptr && at->ai_family == AF_INET6) chosen = at;
    }
    const bool found = chosen != nullptr && chosen->ai_addrlen <= Address::capacity();
    if (found) {
        address = Address{};
        std::memcpy(address.data(), chosen->ai_addr, chosen->ai_addrlen);
        address.set_length(static_cast<std::uint32_t>(chosen->ai_addrlen));
    }
    freeaddrinfo(results);
    return found;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(std::uint16_t port, bool ipv6) {
    close();
    if (!start_sockets()) return false;
    const auto make = [](int family) {
        Native socket = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == kNone) return socket;
#if defined(_WIN32)
        u_long on = 1;
        ioctlsocket(socket, FIONBIO, &on);
        // A datagram that reached a closed port must not fail the next receive (Windows reports its ICMP reply).
        BOOL report = FALSE;
        DWORD returned = 0;
        WSAIoctl(socket, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
#else
        fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#if defined(SO_NOSIGPIPE)
        int no = 1;
        setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &no, sizeof(no));
#endif
#endif
        // Room for a burst (a mod download, many players' snapshots) between two reads.
        int buffer = 1 << 20;
        setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        return socket;
    };
    if (ipv6) {
        Native socket = make(AF_INET6);
        if (socket != kNone) {
            int only = 0;
            setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof(only));
            sockaddr_in6 any{};
            any.sin6_family = AF_INET6;  // the address all zeros: any
            any.sin6_port = htons(port);
            if (bind(socket, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) == 0) {
                handle_ = static_cast<std::intptr_t>(socket);
                ipv6_ = true;
                return true;
            }
            close_native(socket);
        }
    }
    Native socket = make(AF_INET);
    if (socket == kNone) return false;
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    any.sin_port = htons(port);
    if (bind(socket, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0) {
        close_native(socket);
        return false;
    }
    handle_ = static_cast<std::intptr_t>(socket);
    ipv6_ = false;
    return true;
}

void UdpSocket::close() noexcept {
    if (handle_ == kInvalid) return;
    close_native(native(handle_));
    handle_ = kInvalid;
}

std::uint16_t UdpSocket::local_port() const noexcept {
    if (handle_ == kInvalid) return 0u;
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (getsockname(native(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0u;
    if (address.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
    if (address.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    return 0u;
}

bool UdpSocket::send_to(const Address& to, std::span<const std::uint8_t> data) noexcept {
    if (handle_ == kInvalid || !to.valid()) return false;
    sockaddr_storage target = unmapped(to);
    socklen_t length = target.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (ipv6_ && target.ss_family == AF_INET) {
        // The dual-stack socket reaches IPv4 through the mapped form.
        const auto* four = reinterpret_cast<const sockaddr_in*>(&target);
        sockaddr_in6 six{};
        six.sin6_family = AF_INET6;
        six.sin6_port = four->sin_port;
        auto* bytes = reinterpret_cast<std::uint8_t*>(&six.sin6_addr);
        bytes[10] = 0xff;
        bytes[11] = 0xff;
        std::memcpy(bytes + 12, &four->sin_addr, 4);
        target = {};
        std::memcpy(&target, &six, sizeof(six));
        length = sizeof(six);
    } else if (!ipv6_ && target.ss_family == AF_INET6) {
        return false;
    }
#if defined(MSG_NOSIGNAL)
    constexpr int kFlags = MSG_NOSIGNAL;
#else
    constexpr int kFlags = 0;
#endif
    const auto sent = ::sendto(native(handle_), reinterpret_cast<const char*>(data.data()),
                               static_cast<int>(data.size()), kFlags, reinterpret_cast<const sockaddr*>(&target),
                               length);
    return sent >= 0 && static_cast<std::size_t>(sent) == data.size();
}

std::ptrdiff_t UdpSocket::receive_from(std::span<std::uint8_t> buffer, Address& from) noexcept {
    if (handle_ == kInvalid) return -1;
    for (;;) {
        sockaddr_storage address{};
        socklen_t length = sizeof(address);
        const auto received = ::recvfrom(native(handle_), reinterpret_cast<char*>(buffer.data()),
                                         static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&address),
                                         &length);
        if (received < 0) {
#if defined(_WIN32)
            const int error = WSAGetLastError();
            // A datagram longer than the buffer: dropped (nothing Nebula sends is), the next one read.
            if (error == WSAEMSGSIZE || error == WSAECONNRESET) continue;
#endif
            return -1;
        }
        from = Address{};
        std::memcpy(from.data(), &address, std::min<std::size_t>(length, Address::capacity()));
        from.set_length(static_cast<std::uint32_t>(length));
        return static_cast<std::ptrdiff_t>(received);
    }
}

bool UdpSocket::wait(int milliseconds) noexcept {
    if (handle_ == kInvalid) return false;
#if defined(_WIN32)
    WSAPOLLFD poll{};
    poll.fd = native(handle_);
    poll.events = POLLRDNORM;
    return WSAPoll(&poll, 1, milliseconds) > 0;
#else
    pollfd poll{};
    poll.fd = native(handle_);
    poll.events = POLLIN;
    return ::poll(&poll, 1, milliseconds) > 0;
#endif
}

}  // namespace galaxy::net
