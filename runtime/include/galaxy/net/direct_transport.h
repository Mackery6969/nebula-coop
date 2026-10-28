// The direct transport (galaxy/net/transport.h): the lobby over UDP, joined by the host's address:port, as
// sm64coopdx's socket NetworkSystem with CoopNet's lobby on top. The host's socket is the lobby: it admits members
// (password, size, protocol), gives each a peer id, tells everyone who joins and leaves, and passes members' messages
// on to the others. Members only ever talk to the host, so only its port has to be forwarded (or reached over a VPN).
#pragma once

#include "galaxy/net/link.h"
#include "galaxy/net/transport.h"
#include "galaxy/net/udp.h"

#include <memory>
#include <random>
#include <vector>

namespace galaxy::net {

class DirectTransport final : public Transport {
public:
    // The transport's protocol: a host and a joiner with different ones refuse each other (NetError::Version).
    static constexpr std::uint8_t kProtocol = 1u;
    // Silence this long from a member (or the host) and it's gone.
    static constexpr std::chrono::seconds kTimeout{10};
    // How long a join tries before NetError::LobbyNotFound.
    static constexpr std::chrono::seconds kJoinTimeout{10};

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
    // Joining: the address tried, the nonce the host answers with, and when it began.
    Address join_address_;
    std::uint64_t join_nonce_ = 0u;
    std::string join_password_;
    Clock::time_point join_started_{};
    Clock::time_point join_last_hello_{};
    std::mt19937_64 random_;
    double loss_ = 0.0;
};

}  // namespace galaxy::net
