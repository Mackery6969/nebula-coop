// The direct transport (galaxy/net/transport.h): the lobby over UDP, joined by the host's address:port, as
// sm64coopdx's socket NetworkSystem with CoopNet's lobby on top. The host's socket is the lobby: it admits members
// (password, size, protocol), gives each a peer id, tells everyone who joins and leaves, and passes members' messages
// on to the others. Members only ever talk to the host, so only its port has to be forwarded (or reached over a VPN).
//
// Or no port at all, through NAT traversal (NebulaNet's room codes, docs/COOP_PLAN.md "Hosting"): each side asks a
// STUN server which public address its socket has (discover_public_address), NebulaNet swaps them, the host punches
// toward the joiner (punch) while the joiner's hellos go to every address the host may have (JoinConfig's
// alternates), and both routers let the other side in.
#pragma once

#include "galaxy/net/link.h"
#include "galaxy/net/stun.h"
#include "galaxy/net/transport.h"
#include "galaxy/net/udp.h"

#include <memory>
#include <random>
#include <string_view>
#include <vector>

namespace galaxy::net {

class DirectTransport final : public Transport {
public:
    // The transport's protocol: a host and a joiner with different ones refuse each other (NetError::Version).
    static constexpr std::uint8_t kProtocol = 1u;
    // Silence this long from a member (or the host) and it's gone.
    static constexpr std::chrono::seconds kTimeout{10};
    // How long a join tries before NetError::LobbyNotFound (JoinConfig's timeout replaces it).
    static constexpr std::chrono::seconds kJoinTimeout{10};
    // How long a STUN server has to answer, and how long a host punches toward one joiner.
    static constexpr std::chrono::seconds kDiscoveryTimeout{3};
    static constexpr std::chrono::seconds kPunchFor{20};

    // Where asking a STUN server is.
    enum class Discovery : std::uint8_t { Idle, Asking, Found, Failed };

    DirectTransport();
    ~DirectTransport() override;

    [[nodiscard]] const char* name() const noexcept override { return "Direct"; }
    bool lobby_create(const LobbyConfig& config) override;
    bool lobby_join(const JoinConfig& config) override;
    void lobby_leave() override;
    void unpeer(PeerId peer) override;
    void update(std::chrono::milliseconds wait = std::chrono::milliseconds(0)) override;
    bool send_to(PeerId peer, Channel channel, std::span<const std::uint8_t> data) override;

    [[nodiscard]] bool in_lobby() const noexcept override { return state_ == State::Lobby; }
    [[nodiscard]] bool joining() const noexcept { return state_ == State::Joining; }
    [[nodiscard]] bool is_host() const noexcept override { return host_; }
    [[nodiscard]] PeerId self() const noexcept override { return self_; }
    [[nodiscard]] PeerId owner() const noexcept override { return owner_; }
    [[nodiscard]] std::chrono::milliseconds ping(PeerId peer) const noexcept override;
    [[nodiscard]] std::uint16_t port() const noexcept override { return socket_.local_port(); }
    [[nodiscard]] std::string peer_identity(PeerId peer) const override;
    // The lobby's members, this one included.
    [[nodiscard]] std::vector<PeerId> members() const;

    // Asks the STUN server at `server` (galaxy/net/stun.h), from the lobby's socket, which public address the
    // internet sees it at; update() goes on asking until it answers (public_address) or kDiscoveryTimeout passes.
    // False when there is no socket (no lobby being made or joined) or the server's name doesn't resolve. Blocks
    // while the name is looked up.
    bool discover_public_address(std::string_view server, std::uint16_t port = stun::kDefaultPort);
    [[nodiscard]] Discovery discovery() const noexcept { return discovery_; }
    // The address the STUN server saw, once discovery() is Found.
    [[nodiscard]] const Address& public_address() const noexcept { return public_address_; }
    // A host's: sends a few datagrams to `joiner` for kPunchFor, so this host's router lets that joiner's hellos in
    // (its own hellos open the joiner's router). Ends early once the joiner is a member.
    void punch(const Address& joiner);

    // Tests: drop this share (0 to 1) of the datagrams sent, at random.
    void set_test_loss(double share) noexcept { loss_ = share; }

private:
    enum class State { Closed, Joining, Lobby };
    struct Remote;

    Remote* find(const Address& address);
    Remote* find(PeerId peer);
    void send_raw(const Address& to, std::uint8_t type, std::span<const std::uint8_t> body);
    void send_routed(Remote& remote, Channel channel, std::uint8_t kind, PeerId from, PeerId to,
                     std::span<const std::uint8_t> data);
    void receive_datagram(std::span<const std::uint8_t> datagram, const Address& from, Clock::time_point now);
    void receive_hello(std::span<const std::uint8_t> body, const Address& from, Clock::time_point now);
    void receive_welcome(std::span<const std::uint8_t> body, const Address& from, Clock::time_point now);
    void receive_message(Remote& remote, const LinkMessage& message);
    bool is_join_address(const Address& address) const;
    void update_discovery(Clock::time_point now);
    void update_punches(Clock::time_point now);
    // A member gone (a host's view: tell the others), or the host gone (a joiner's: the lobby ends).
    void drop(PeerId peer, bool intentional, NetError error);
    void fail(NetError error);
    void close_now();
    PeerId new_id();

    UdpSocket socket_;
    State state_ = State::Closed;
    bool host_ = false;
    PeerId self_ = kNoPeer;
    PeerId owner_ = kNoPeer;
    std::uint64_t lobby_ = 0u;
    LobbyConfig config_;
    std::vector<std::unique_ptr<Remote>> remotes_;  // a host's members; a joiner's one: the host
    std::vector<PeerId> others_;                    // a joiner's: the other members, as the host told
    // Joining: the addresses tried (the first is the one asked for), the nonce the host answers with, and when it
    // began.
    std::vector<Address> join_addresses_;
    std::chrono::milliseconds join_timeout_{kJoinTimeout};
    std::uint64_t join_nonce_ = 0u;
    std::string join_password_;
    Clock::time_point join_started_{};
    Clock::time_point join_last_hello_{};
    // Asking a STUN server.
    Discovery discovery_ = Discovery::Idle;
    Address stun_server_;
    stun::TransactionId stun_id_{};
    Address public_address_;
    Clock::time_point discovery_started_{};
    Clock::time_point discovery_last_ask_{};
    // A host's punches: toward whom, until when, and the last one sent.
    struct Punch {
        Address to;
        Clock::time_point until{};
        Clock::time_point last{};
    };
    std::vector<Punch> punches_;
    std::mt19937_64 random_;
    double loss_ = 0.0;
};

}  // namespace galaxy::net
