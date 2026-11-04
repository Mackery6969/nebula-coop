// Online co-op's sockets (docs/COOP_PLAN.md, "Phase 1"): one non-blocking UDP socket, IPv4 and IPv6, on Windows
// (Winsock) and the POSIX systems (Linux, Android, macOS). The system headers stay in udp.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace galaxy::net {

// A socket address (a sockaddr_storage's bytes), IPv4 or IPv6.
class Address {
public:
    Address() = default;

    [[nodiscard]] bool valid() const noexcept { return length_ != 0u; }
    [[nodiscard]] bool ipv6() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    // "1.2.3.4:7777" or "[::1]:7777"; an IPv4 address mapped into IPv6 shows as IPv4.
    [[nodiscard]] std::string to_string() const;
    // The host alone, "1.2.3.4" or "::1" (an IPv4 address mapped into IPv6 shows as IPv4).
    [[nodiscard]] std::string host_text() const;
    // The IP address's bytes in network order into `out`: 4 for IPv4 (an IPv4 address mapped into IPv6 too), 16 for
    // IPv6, 0 when there's none.
    [[nodiscard]] std::size_t ip_bytes(std::span<std::uint8_t, 16> out) const noexcept;

    // The bytes as the system takes them (a sockaddr).
    [[nodiscard]] const void* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] void* data() noexcept { return bytes_.data(); }
    [[nodiscard]] std::uint32_t length() const noexcept { return length_; }
    void set_length(std::uint32_t length) noexcept { length_ = length <= bytes_.size() ? length : 0u; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return 128u; }

    // The same host and port; an IPv4 address and its IPv6-mapped form are the same.
    friend bool operator==(const Address& a, const Address& b) noexcept;

private:
    alignas(8) std::array<std::uint8_t, 128> bytes_{};
    std::uint32_t length_ = 0u;
};

// The address of an IP address's bytes (4: IPv4, 16: IPv6, in network order) and a port; invalid for another size.
[[nodiscard]] Address make_address(std::span<const std::uint8_t> ip, std::uint16_t port) noexcept;

// The address of `host` ("1.2.3.4", "::1", "example.com"; "" the local machine) on `port`: false when it doesn't
// resolve. Blocks while a name is looked up.
[[nodiscard]] bool resolve(std::string_view host, std::uint16_t port, Address& address);

// A UDP socket that never blocks: send and receive return at once.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // A socket on `port` (0: any the system gives) for every address: IPv6 taking IPv4 too where the system can,
    // else IPv4 only. `ipv6` false: IPv4 only. False when it can't be opened (the port taken, no network).
    bool open(std::uint16_t port, bool ipv6 = true);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return handle_ != kInvalid; }
    // The port it is on (the one the system chose for 0).
    [[nodiscard]] std::uint16_t local_port() const noexcept;

    // False when the datagram was not sent (an address of the other family, the system's buffers full).
    bool send_to(const Address& to, std::span<const std::uint8_t> data) noexcept;
    // The next datagram's bytes (at most `buffer`'s size; a longer one is cut) and its sender, or -1 when none waits.
    [[nodiscard]] std::ptrdiff_t receive_from(std::span<std::uint8_t> buffer, Address& from) noexcept;
    // Until a datagram waits or `milliseconds` pass: true when one waits.
    bool wait(int milliseconds) noexcept;

private:
    static constexpr std::intptr_t kInvalid = -1;
    std::intptr_t handle_ = kInvalid;
    bool ipv6_ = false;
};

}  // namespace galaxy::net
