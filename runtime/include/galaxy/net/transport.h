// Online co-op's transport (docs/COOP_PLAN.md, "Hosting"), shaped like sm64coopdx's CoopNet: a lobby the host
// creates and others join, every member known by a peer id, and callbacks for what happens (the lobby made or joined,
// members joining and leaving, messages, errors). As sm64coopdx picks a NetworkSystem (its direct sockets or
// CoopNet), Nebula picks a Transport: DirectTransport (direct UDP to address:port) now; a relay that joins by a code
// could be another later without the session above it changing.
//
// A transport carries messages between members on the three channels of galaxy/net/link.h, routes them (the host
// passes a member's messages on to the others, so only the host's port needs to be reachable) and admits members to
// the lobby: its password, its size and the transport's protocol. What the game checks at a join (its version, its
// mods, the disc) is the session's, above it.
//
// Not thread-safe: one thread calls everything, and the callbacks come from update().
#pragma once

#include "galaxy/net/link.h"
#include "galaxy/net/udp.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::net {

using PeerId = std::uint64_t;
inline constexpr PeerId kNoPeer = 0u;
// send_to's "every member but me".
inline constexpr PeerId kEveryone = ~PeerId{0};
// The longest unreliable or reliable message a transport carries (an ordered one: kMaxOrderedMessage); its routing
// takes the rest of a datagram.
inline constexpr std::size_t kMaxRoutedSmallMessage = kMaxSmallMessage - 17u;

// Why a lobby couldn't be made, joined or kept (CoopNet's MPacketErrorNumber).
enum class NetError : std::uint8_t {
    None,
    LobbyNotFound,      // no answer from the address
    LobbyFull,
    LobbyJoinFailed,    // the address didn't resolve, or the socket couldn't open
    PasswordIncorrect,
    Version,            // the transport's protocol differs
    PeerFailed,         // a member stopped answering
    Kicked,             // the host removed this member
    HostLeft,           // the host closed the lobby
};

[[nodiscard]] const char* net_error_text(NetError error) noexcept;

// What a lobby says about itself, as a host creates it.
struct LobbyConfig {
    std::uint16_t port = 0u;  // the host's UDP port (0: any, for tests)
    std::string game;         // "Nebula"
    std::string version;      // the game's, shown before joining
    std::string host_name;
    std::string password;     // empty: none
    unsigned max_members = 8u;  // the host included
};

// A host name or IP and a port.
struct Endpoint {
    std::string address;
    std::uint16_t port = 0u;
};

struct JoinConfig {
    std::string address;  // a host name or IP
    std::uint16_t port = 0u;
    std::string password;
    // More addresses the same host may be reached at, tried at the same time (a room code's: the host's public
    // address as its STUN server saw it, and its addresses on its own network); the first that answers is the host.
    std::vector<Endpoint> alternates;
    // How long the join tries before NetError::LobbyNotFound; 0: the transport's own.
    std::chrono::milliseconds timeout{0};
};

struct TransportCallbacks {
    // Joined or created: this member's id, the host's and the lobby's.
    std::function<void(PeerId self, PeerId owner, std::uint64_t lobby)> on_lobby_joined;
    // Another member joined or left the lobby (left: intentionally, or it stopped answering).
    std::function<void(PeerId peer)> on_peer_joined;
    std::function<void(PeerId peer, bool intentional)> on_peer_left;
    std::function<void(PeerId from, Channel channel, std::span<const std::uint8_t> data)> on_receive;
    // The lobby failed or ended for this member (the transport is closed then, but for PeerFailed about another).
    std::function<void(NetError error, PeerId tag)> on_error;
};

class Transport {
public:
    using Clock = std::chrono::steady_clock;

    virtual ~Transport() = default;

    // The transport's name, as sm64coopdx's NetworkSystem has ("Direct").
    [[nodiscard]] virtual const char* name() const noexcept = 0;
    void set_callbacks(TransportCallbacks callbacks) { callbacks_ = std::move(callbacks); }

    // Hosts a lobby: on_lobby_joined at once (or false when the port can't be opened).
    virtual bool lobby_create(const LobbyConfig& config) = 0;
    // Joins one: on_lobby_joined once the host lets this member in, on_error when it doesn't.
    virtual bool lobby_join(const JoinConfig& config) = 0;
    // Leaves (a host: closes) the lobby, telling the others.
    virtual void lobby_leave() = 0;
    // The host removes a member.
    virtual void unpeer(PeerId peer) = 0;

    // Receives, sends what is due and notices who stopped answering. `wait`: how long to wait for a datagram first.
    virtual void update(std::chrono::milliseconds wait = std::chrono::milliseconds(0)) = 0;

    // To one member, or kEveryone. False when not in a lobby, or the message is too long for the channel.
    virtual bool send_to(PeerId peer, Channel channel, std::span<const std::uint8_t> data) = 0;

    [[nodiscard]] virtual bool in_lobby() const noexcept = 0;
    [[nodiscard]] virtual bool is_host() const noexcept = 0;
    [[nodiscard]] virtual PeerId self() const noexcept = 0;
    [[nodiscard]] virtual PeerId owner() const noexcept = 0;
    // The round trip to the member (to the host for a joiner's view of the others), 0 when unknown.
    [[nodiscard]] virtual std::chrono::milliseconds ping(PeerId peer) const noexcept = 0;
    // The port a host listens on.
    [[nodiscard]] virtual std::uint16_t port() const noexcept = 0;
    // NAT traversal (NebulaNet's room codes, docs/COOP_PLAN.md "Hosting"), where the transport has it (the direct
    // one; another does nothing). A joiner opens its socket before it knows the host's addresses, so a STUN server
    // can tell it its own; the next lobby_join keeps that socket.
    virtual bool open_for_join() { return false; }
    // Asks a STUN server which public address this transport's socket has (false: it can't ask).
    virtual bool discover_public_address(std::string_view server, std::uint16_t port) {
        (void)server;
        (void)port;
        return false;
    }
    // True while it waits for the STUN server's answer.
    [[nodiscard]] virtual bool discovering() const noexcept { return false; }
    // The answer (invalid when there was none).
    [[nodiscard]] virtual Address public_address() const { return {}; }
    // A host's: opens its router to a joiner at `address` for a while.
    virtual void punch(const Address& address) { (void)address; }

    // A host's: who the member is beyond this lobby, what bans and moderators are kept by (sm64coopdx's get_id_str:
    // the direct transport's is the member's IP address). Empty when unknown.
    [[nodiscard]] virtual std::string peer_identity(PeerId peer) const = 0;

protected:
    TransportCallbacks callbacks_;
};

}  // namespace galaxy::net
