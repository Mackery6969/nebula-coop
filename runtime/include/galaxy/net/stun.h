// STUN's Binding request and answer (RFC 8489), the small part of it NAT traversal needs (docs/COOP_PLAN.md,
// "Hosting"): a game asks a STUN server, from the socket it plays on, which address the internet sees that socket at
// (its router's public address and port), so NebulaNet can pass it to the other side and both can punch through.
//
// A STUN message starts with two zero bits and carries the magic cookie in bytes 4-7, so it never looks like one of
// the transport's own datagrams ("NB" first) and both share a socket.
#pragma once

#include "galaxy/net/udp.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::net::stun {

// STUN's port, as stun.cloudflare.com and most servers answer on.
inline constexpr std::uint16_t kDefaultPort = 3478u;
// The server NebulaNet's games ask by default (Cloudflare's, free and without limits).
inline constexpr const char* kDefaultServer = "stun.cloudflare.com";

using TransactionId = std::array<std::uint8_t, 12>;

// Whether `datagram` is a STUN message (its first bits, size and cookie).
[[nodiscard]] bool is_stun(std::span<const std::uint8_t> datagram) noexcept;

// A Binding request.
[[nodiscard]] std::vector<std::uint8_t> binding_request(const TransactionId& id);
// The address a Binding success answer to `id` reports (XOR-MAPPED-ADDRESS, or the older MAPPED-ADDRESS). False when
// `datagram` is not one.
[[nodiscard]] bool parse_binding_response(std::span<const std::uint8_t> datagram, const TransactionId& id,
                                          Address& mapped);

// A server's side, for tests: whether `datagram` is a Binding request (and its id), and the success answer that
// reports `mapped`.
[[nodiscard]] bool parse_binding_request(std::span<const std::uint8_t> datagram, TransactionId& id) noexcept;
[[nodiscard]] std::vector<std::uint8_t> binding_response(const TransactionId& id, const Address& mapped);

}  // namespace galaxy::net::stun
