#include "galaxy/net/stun.h"

#include <algorithm>

namespace galaxy::net::stun {

namespace {

constexpr std::uint32_t kCookie = 0x2112A442u;
constexpr std::uint16_t kBindingRequest = 0x0001u;
constexpr std::uint16_t kBindingSuccess = 0x0101u;
constexpr std::uint16_t kMappedAddress = 0x0001u;
constexpr std::uint16_t kXorMappedAddress = 0x0020u;
constexpr std::size_t kHeader = 20u;

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::vector<std::uint8_t> header(std::uint16_t type, std::uint16_t length, const TransactionId& id) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeader + length);
    put16(out, type);
    put16(out, length);
    put16(out, static_cast<std::uint16_t>(kCookie >> 16));
    put16(out, static_cast<std::uint16_t>(kCookie));
    out.insert(out.end(), id.begin(), id.end());
    return out;
}

// The cookie and the transaction id, which XOR-MAPPED-ADDRESS's address is masked with.
std::array<std::uint8_t, 16> mask(const TransactionId& id) {
    std::array<std::uint8_t, 16> out{};
    out[0] = static_cast<std::uint8_t>(kCookie >> 24);
    out[1] = static_cast<std::uint8_t>(kCookie >> 16);
    out[2] = static_cast<std::uint8_t>(kCookie >> 8);
    out[3] = static_cast<std::uint8_t>(kCookie);
    std::copy(id.begin(), id.end(), out.begin() + 4);
    return out;
}

// An address attribute's value: a zero byte, the family (1: IPv4, 2: IPv6), the port, the address.
bool read_address(std::span<const std::uint8_t> value, bool masked, const TransactionId& id, Address& out) {
    if (value.size() < 8u) return false;
    const std::uint8_t family = value[1];
    const std::size_t size = family == 0x01u ? 4u : family == 0x02u ? 16u : 0u;
    if (size == 0u || value.size() < 4u + size) return false;
    std::uint16_t port = u16(value, 2);
    std::array<std::uint8_t, 16> ip{};
    std::copy_n(value.begin() + 4, size, ip.begin());
    if (masked) {
        port ^= static_cast<std::uint16_t>(kCookie >> 16);
        const auto bits = mask(id);
        for (std::size_t i = 0; i < size; ++i) ip[i] ^= bits[i];
    }
    out = make_address(std::span<const std::uint8_t>(ip.data(), size), port);
    return out.valid();
}

}  // namespace

bool is_stun(std::span<const std::uint8_t> datagram) noexcept {
    return datagram.size() >= kHeader && (datagram[0] & 0xC0u) == 0u && u16(datagram, 4) == (kCookie >> 16) &&
           u16(datagram, 6) == (kCookie & 0xFFFFu) && (u16(datagram, 2) & 3u) == 0u &&
           kHeader + u16(datagram, 2) <= datagram.size();
}

std::vector<std::uint8_t> binding_request(const TransactionId& id) { return header(kBindingRequest, 0u, id); }

bool parse_binding_response(std::span<const std::uint8_t> datagram, const TransactionId& id, Address& mapped) {
    if (!is_stun(datagram) || u16(datagram, 0) != kBindingSuccess) return false;
    if (!std::equal(id.begin(), id.end(), datagram.begin() + 8)) return false;
    const std::size_t end = kHeader + u16(datagram, 2);
    Address plain;
    bool have_plain = false;
    for (std::size_t at = kHeader; at + 4u <= end;) {
        const std::uint16_t type = u16(datagram, at);
        const std::size_t length = u16(datagram, at + 2);
        if (at + 4u + length > end) return false;
        const auto value = datagram.subspan(at + 4u, length);
        if (type == kXorMappedAddress && read_address(value, true, id, mapped)) return true;
        if (type == kMappedAddress && !have_plain) have_plain = read_address(value, false, id, plain);
        at += 4u + ((length + 3u) & ~std::size_t{3});
    }
    if (have_plain) mapped = plain;
    return have_plain;
}

bool parse_binding_request(std::span<const std::uint8_t> datagram, TransactionId& id) noexcept {
    if (!is_stun(datagram) || u16(datagram, 0) != kBindingRequest) return false;
    std::copy_n(datagram.begin() + 8, id.size(), id.begin());
    return true;
}

std::vector<std::uint8_t> binding_response(const TransactionId& id, const Address& mapped) {
    std::array<std::uint8_t, 16> ip{};
    const std::size_t size = mapped.ip_bytes(ip);
    const auto length = static_cast<std::uint16_t>(4u + size);
    std::vector<std::uint8_t> out = header(kBindingSuccess, static_cast<std::uint16_t>(4u + length), id);
    put16(out, kXorMappedAddress);
    put16(out, length);
    out.push_back(0u);
    out.push_back(size == 4u ? 0x01u : 0x02u);
    put16(out, static_cast<std::uint16_t>(mapped.port() ^ (kCookie >> 16)));
    const auto bits = mask(id);
    for (std::size_t i = 0; i < size; ++i) out.push_back(static_cast<std::uint8_t>(ip[i] ^ bits[i]));
    return out;
}

}  // namespace galaxy::net::stun
