#include "galaxy/net/direct_transport.h"

#include "galaxy/net/bytes.h"

#include <algorithm>
#include <array>
#include <utility>

namespace galaxy::net {

namespace {

// A datagram: "NB", the protocol, its type, then the type's body.
constexpr std::uint8_t kMagic0 = 'N', kMagic1 = 'B';
enum DatagramType : std::uint8_t {
    kHello = 0x01,    // joiner: nonce, password
    kWelcome = 0x02,  // host: nonce, lobby, the joiner's id, the host's, the other members
    kRefuse = 0x03,   // host: nonce, NetError
    kPunch = 0x04,    // host: nothing; it only opens the host's router to a joiner (older versions ignore it)
    kLinkFrame = 0x10,  // the sender's id, a link frame (galaxy/net/link.h)
    kPing = 0x20,     // the sender's id, its clock
    kPong = 0x21,     // the sender's id, the ping's clock
    kBye = 0x30,      // the sender's id: it left
};

// A link message's kind: a member's message (from, to, its bytes) or the lobby's news, which the host sends in order.
enum RoutedKind : std::uint8_t { kRoutedApp = 0, kRoutedJoined = 1, kRoutedLeft = 2, kRoutedKicked = 3 };
constexpr std::size_t kRoutedHeader = 17u;

constexpr auto kHelloEvery = std::chrono::milliseconds(250);
constexpr auto kPunchEvery = std::chrono::milliseconds(250);
constexpr auto kAskStunEvery = std::chrono::milliseconds(500);
constexpr auto kPingEvery = std::chrono::seconds(1);
constexpr int kByeRepeats = 3;

std::uint64_t micros(Transport::Clock::time_point at) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(at.time_since_epoch()).count());
}

}  // namespace

const char* net_error_text(NetError error) noexcept {
    switch (error) {
    case NetError::None: return "no error";
    case NetError::LobbyNotFound: return "no answer from the host";
    case NetError::LobbyFull: return "the room is full";
    case NetError::LobbyJoinFailed: return "the address could not be reached";
    case NetError::PasswordIncorrect: return "the password is wrong";
    case NetError::Version: return "the host plays another version of online play";
    case NetError::PeerFailed: return "a player stopped answering";
    case NetError::Kicked: return "the host removed you from the room";
    case NetError::HostLeft: return "the host closed the room";
    }
    return "unknown error";
}

struct DirectTransport::Remote {
    Address address;
    PeerId id = kNoPeer;
    Link link;
    std::uint64_t join_nonce = 0u;  // the hello it joined with
    Clock::time_point last_heard{};
    Clock::time_point last_ping{};
    std::chrono::microseconds rtt{0};

    Remote(DirectTransport& owner, const Address& at, PeerId peer)
        : address(at),
          id(peer),
          link([&owner, this](std::span<const std::uint8_t> frame) {
              ByteWriter body;
              body.u64(owner.self_).raw(frame);
              owner.send_raw(address, kLinkFrame, body.bytes());
          }) {}
};

DirectTransport::DirectTransport() : random_(std::random_device{}()) {}

DirectTransport::~DirectTransport() { lobby_leave(); }

PeerId DirectTransport::new_id() {
    PeerId id = kNoPeer;
    while (id == kNoPeer || id == kEveryone || id == self_ || find(id) != nullptr) id = random_();
    return id;
}

DirectTransport::Remote* DirectTransport::find(const Address& address) {
    for (const auto& remote : remotes_) {
        if (remote->address == address) return remote.get();
    }
    return nullptr;
}

DirectTransport::Remote* DirectTransport::find(PeerId peer) {
    for (const auto& remote : remotes_) {
        if (remote->id == peer) return remote.get();
    }
    return nullptr;
}

std::vector<PeerId> DirectTransport::members() const {
    std::vector<PeerId> out;
    if (state_ != State::Lobby) return out;
    out.push_back(self_);
    for (const auto& remote : remotes_) out.push_back(remote->id);
    out.insert(out.end(), others_.begin(), others_.end());
    return out;
}

std::chrono::milliseconds DirectTransport::ping(PeerId peer) const noexcept {
    for (const auto& remote : remotes_) {
        if (remote->id == peer || !host_) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(remote->rtt);
        }
    }
    return std::chrono::milliseconds(0);
}

std::string DirectTransport::peer_identity(PeerId peer) const {
    if (!host_) return {};
    for (const auto& remote : remotes_) {
        if (remote->id == peer) return remote->address.host_text();
    }
    return {};
}

void DirectTransport::send_raw(const Address& to, std::uint8_t type, std::span<const std::uint8_t> body) {
    if (loss_ > 0.0 && std::uniform_real_distribution<double>(0.0, 1.0)(random_) < loss_) return;
    std::array<std::uint8_t, kMaxDatagram + 32u> datagram{};
    if (body.size() + 4u > datagram.size()) return;
    datagram[0] = kMagic0;
    datagram[1] = kMagic1;
    datagram[2] = kProtocol;
    datagram[3] = type;
    std::copy(body.begin(), body.end(), datagram.begin() + 4);
    (void)socket_.send_to(to, std::span<const std::uint8_t>(datagram.data(), body.size() + 4u));
}

void DirectTransport::send_routed(Remote& remote, Channel channel, std::uint8_t kind, PeerId from, PeerId to,
                                  std::span<const std::uint8_t> data) {
    ByteWriter message;
    message.u8(kind).u64(from).u64(to).raw(data);
    (void)remote.link.send(channel, message.bytes(), Clock::now());
}

bool DirectTransport::lobby_create(const LobbyConfig& config) {
    lobby_leave();
    if (!socket_.open(config.port)) return false;
    config_ = config;
    config_.max_members = std::clamp(config.max_members, 1u, 64u);
    host_ = true;
    self_ = new_id();
    owner_ = self_;
    lobby_ = random_();
    state_ = State::Lobby;
    if (callbacks_.on_lobby_joined) callbacks_.on_lobby_joined(self_, owner_, lobby_);
    return true;
}

bool DirectTransport::lobby_join(const JoinConfig& config) {
    lobby_leave();
    // The addresses asked for, those that resolve, each once; the socket takes IPv6 when one of them is.
    std::vector<Address> addresses;
    bool ipv6 = false;
    const auto add = [&](const std::string& host, std::uint16_t port) {
        Address address;
        if (host.empty() || !resolve(host, port, address)) return;
        if (std::find(addresses.begin(), addresses.end(), address) != addresses.end()) return;
        ipv6 = ipv6 || address.ipv6();
        addresses.push_back(address);
    };
    add(config.address, config.port);
    for (const Endpoint& alternate : config.alternates) add(alternate.address, alternate.port);
    if (addresses.empty() || !socket_.open(0u, ipv6)) {
        if (callbacks_.on_error) callbacks_.on_error(NetError::LobbyJoinFailed, kNoPeer);
        return false;
    }
    host_ = false;
    join_addresses_ = std::move(addresses);
    join_timeout_ = config.timeout.count() > 0
                        ? config.timeout
                        : std::chrono::duration_cast<std::chrono::milliseconds>(kJoinTimeout);
    join_password_ = config.password;
    join_nonce_ = random_();
    join_started_ = Clock::now();
    join_last_hello_ = {};
    state_ = State::Joining;
    return true;
}

void DirectTransport::lobby_leave() {
    if (state_ == State::Closed) return;
    // Bye, more than once: a datagram may be lost, and nothing waits for an answer.
    if (state_ == State::Lobby) {
        ByteWriter body;
        body.u64(self_);
        for (int i = 0; i < kByeRepeats; ++i) {
            for (const auto& remote : remotes_) {
                remote->link.update(Clock::now());  // what was still due goes first
                send_raw(remote->address, kBye, body.bytes());
            }
        }
    }
    close_now();
}

void DirectTransport::close_now() {
    remotes_.clear();
    others_.clear();
    join_addresses_.clear();
    punches_.clear();
    discovery_ = Discovery::Idle;
    public_address_ = Address{};
    socket_.close();
    state_ = State::Closed;
    host_ = false;
    self_ = owner_ = kNoPeer;
    lobby_ = 0u;
}

void DirectTransport::fail(NetError error) {
    close_now();
    if (callbacks_.on_error) callbacks_.on_error(error, kNoPeer);
}

void DirectTransport::unpeer(PeerId peer) {
    if (!host_) return;
    Remote* remote = find(peer);
    if (remote == nullptr) return;
    send_routed(*remote, Channel::Ordered, kRoutedKicked, self_, peer, {});
    remote->link.update(Clock::now());
    drop(peer, true, NetError::None);
}

bool DirectTransport::send_to(PeerId peer, Channel channel, std::span<const std::uint8_t> data) {
    if (state_ != State::Lobby || peer == self_ || peer == kNoPeer) return false;
    if (channel != Channel::Ordered && data.size() > kMaxRoutedSmallMessage) return false;
    if (data.size() > kMaxOrderedMessage - kRoutedHeader) return false;
    if (host_) {
        if (peer == kEveryone) {
            for (const auto& remote : remotes_) send_routed(*remote, channel, kRoutedApp, self_, kEveryone, data);
            return true;
        }
        Remote* remote = find(peer);
        if (remote == nullptr) return false;
        send_routed(*remote, channel, kRoutedApp, self_, peer, data);
        return true;
    }
    // A joiner's go through the host.
    if (remotes_.empty()) return false;
    if (peer != kEveryone && peer != owner_ && std::find(others_.begin(), others_.end(), peer) == others_.end()) {
        return false;
    }
    send_routed(*remotes_.front(), channel, kRoutedApp, self_, peer, data);
    return true;
}

void DirectTransport::drop(PeerId peer, bool intentional, NetError error) {
    if (host_) {
        const auto it = std::find_if(remotes_.begin(), remotes_.end(),
                                     [peer](const auto& remote) { return remote->id == peer; });
        if (it == remotes_.end()) return;
        remotes_.erase(it);
        ByteWriter news;
        news.u8(intentional ? 1u : 0u);
        for (const auto& remote : remotes_) {
            send_routed(*remote, Channel::Ordered, kRoutedLeft, self_, peer, news.bytes());
        }
        if (!intentional && callbacks_.on_error) callbacks_.on_error(NetError::PeerFailed, peer);
        if (callbacks_.on_peer_left) callbacks_.on_peer_left(peer, intentional);
        return;
    }
    if (peer == owner_) {
        fail(error == NetError::None ? NetError::HostLeft : error);
        return;
    }
    const auto it = std::find(others_.begin(), others_.end(), peer);
    if (it == others_.end()) return;
    others_.erase(it);
    if (callbacks_.on_peer_left) callbacks_.on_peer_left(peer, intentional);
}

void DirectTransport::update(std::chrono::milliseconds wait) {
    if (state_ == State::Closed) return;
    if (wait.count() > 0) (void)socket_.wait(static_cast<int>(wait.count()));
    std::array<std::uint8_t, 2048> buffer{};
    Address from;
    // Everything waiting, but not forever under a flood: the rest next time.
    for (int i = 0; i < 4096 && state_ != State::Closed; ++i) {
        const std::ptrdiff_t size = socket_.receive_from(buffer, from);
        if (size < 0) break;
        receive_datagram(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(size)), from,
                         Clock::now());
    }
    if (state_ == State::Closed) return;
    const Clock::time_point now = Clock::now();
    update_discovery(now);
    if (state_ == State::Joining) {
        if (now - join_started_ > join_timeout_) {
            fail(NetError::LobbyNotFound);
            return;
        }
        if (now - join_last_hello_ >= kHelloEvery) {
            join_last_hello_ = now;
            ByteWriter body;
            body.u64(join_nonce_).str(join_password_);
            for (const Address& address : join_addresses_) send_raw(address, kHello, body.bytes());
        }
        return;
    }
    update_punches(now);
    std::vector<std::pair<PeerId, NetError>> gone;
    for (const auto& remote : remotes_) {
        remote->link.update(now);
        if (remote->link.broken() || now - remote->last_heard > kTimeout) {
            gone.emplace_back(remote->id, NetError::PeerFailed);
            continue;
        }
        if (now - remote->last_ping >= kPingEvery) {
            remote->last_ping = now;
            ByteWriter body;
            body.u64(self_).u64(micros(now));
            send_raw(remote->address, kPing, body.bytes());
        }
    }
    for (const auto& [peer, error] : gone) {
        drop(peer, false, error);
        if (state_ == State::Closed) return;
    }
}

bool DirectTransport::is_join_address(const Address& address) const {
    return std::find(join_addresses_.begin(), join_addresses_.end(), address) != join_addresses_.end();
}

bool DirectTransport::discover_public_address(std::string_view server, std::uint16_t port) {
    if (state_ == State::Closed || !socket_.is_open()) return false;
    Address address;
    if (!resolve(server, port, address)) {
        discovery_ = Discovery::Failed;
        return false;
    }
    stun_server_ = address;
    for (auto& byte : stun_id_) byte = static_cast<std::uint8_t>(random_());
    public_address_ = Address{};
    discovery_ = Discovery::Asking;
    discovery_started_ = Clock::now();
    discovery_last_ask_ = {};
    update_discovery(discovery_started_);
    return true;
}

void DirectTransport::update_discovery(Clock::time_point now) {
    if (discovery_ != Discovery::Asking) return;
    if (now - discovery_started_ > kDiscoveryTimeout) {
        discovery_ = Discovery::Failed;
        return;
    }
    if (now - discovery_last_ask_ < kAskStunEvery) return;
    discovery_last_ask_ = now;
    if (loss_ > 0.0 && std::uniform_real_distribution<double>(0.0, 1.0)(random_) < loss_) return;
    (void)socket_.send_to(stun_server_, stun::binding_request(stun_id_));
}

void DirectTransport::punch(const Address& joiner) {
    if (!host_ || state_ != State::Lobby || !joiner.valid() || find(joiner) != nullptr) return;
    const Clock::time_point now = Clock::now();
    for (Punch& punch : punches_) {
        if (punch.to == joiner) {
            punch.until = now + kPunchFor;
            return;
        }
    }
    punches_.push_back({joiner, now + kPunchFor, {}});
    update_punches(now);
}

void DirectTransport::update_punches(Clock::time_point now) {
    std::erase_if(punches_, [&](const Punch& punch) { return now > punch.until || find(punch.to) != nullptr; });
    for (Punch& punch : punches_) {
        if (now - punch.last < kPunchEvery) continue;
        punch.last = now;
        send_raw(punch.to, kPunch, {});
    }
}

void DirectTransport::receive_datagram(std::span<const std::uint8_t> datagram, const Address& from,
                                       Clock::time_point now) {
    if (discovery_ == Discovery::Asking && stun::is_stun(datagram)) {
        Address mapped;
        if (from == stun_server_ && stun::parse_binding_response(datagram, stun_id_, mapped)) {
            public_address_ = mapped;
            discovery_ = Discovery::Found;
        }
        return;
    }
    if (datagram.size() < 4u || datagram[0] != kMagic0 || datagram[1] != kMagic1) return;
    const std::uint8_t type = datagram[3];
    const std::span<const std::uint8_t> body = datagram.subspan(4);
    if (datagram[2] != kProtocol) {
        // Another protocol: a host answers a join so the joiner knows why; a joiner hearing it gives up.
        if (host_ && type == kHello && body.size() >= 8u) {
            ByteReader reader(body);
            ByteWriter refuse;
            refuse.u64(reader.u64()).u8(static_cast<std::uint8_t>(NetError::Version));
            send_raw(from, kRefuse, refuse.bytes());
        } else if (state_ == State::Joining && is_join_address(from)) {
            fail(NetError::Version);
        }
        return;
    }
    switch (type) {
    case kHello:
        if (host_) receive_hello(body, from, now);
        return;
    case kWelcome:
        if (state_ == State::Joining && is_join_address(from)) receive_welcome(body, from, now);
        return;
    case kRefuse: {
        if (state_ != State::Joining || !is_join_address(from)) return;
        ByteReader reader(body);
        const std::uint64_t nonce = reader.u64();
        const auto error = static_cast<NetError>(reader.u8());
        if (reader.ok() && nonce == join_nonce_) fail(error);
        return;
    }
    case kPunch: return;  // it opened a router on its way; nothing to do
    default: break;
    }
    // The rest come from a member (the host, for a joiner) and carry its id, which must match its address.
    ByteReader reader(body);
    const PeerId sender = reader.u64();
    if (!reader.ok()) return;
    Remote* remote = find(from);
    if (remote == nullptr || remote->id != sender) return;
    remote->last_heard = now;
    switch (type) {
    case kLinkFrame: {
        std::vector<LinkMessage> messages;
        remote->link.receive(reader.rest(), now, messages);
        const PeerId id = remote->id;
        for (const LinkMessage& message : messages) {
            // A message may end the lobby or drop this member: look the remote up again each time.
            Remote* still = find(id);
            if (still == nullptr || state_ != State::Lobby) return;
            receive_message(*still, message);
        }
        return;
    }
    case kPing: {
        ByteWriter pong;
        pong.u64(self_).u64(reader.u64());
        send_raw(from, kPong, pong.bytes());
        return;
    }
    case kPong: {
        const std::uint64_t sent = reader.u64();
        const std::uint64_t at = micros(now);
        if (reader.ok() && sent <= at) {
            const auto sample = std::chrono::microseconds(static_cast<long long>(at - sent));
            remote->rtt = remote->rtt.count() == 0 ? sample : (remote->rtt * 7 + sample) / 8;
        }
        return;
    }
    case kBye: drop(sender, true, NetError::HostLeft); return;
    default: return;
    }
}

void DirectTransport::receive_hello(std::span<const std::uint8_t> body, const Address& from, Clock::time_point now) {
    ByteReader reader(body);
    const std::uint64_t nonce = reader.u64();
    const std::string password = reader.str(256u);
    if (!reader.ok()) return;
    Remote* known = find(from);
    if (known == nullptr) {
        // The same joiner's hello along another way (a joiner tries every address the host may have): it's already
        // a member by the first way, and the welcome went back along that one.
        for (const auto& remote : remotes_) {
            if (remote->join_nonce == nonce) return;
        }
    }
    NetError refused = NetError::None;
    if (known == nullptr) {
        if (!config_.password.empty() && password != config_.password) {
            refused = NetError::PasswordIncorrect;
        } else if (remotes_.size() + 1u >= config_.max_members) {
            refused = NetError::LobbyFull;
        }
    }
    if (refused != NetError::None) {
        ByteWriter refuse;
        refuse.u64(nonce).u8(static_cast<std::uint8_t>(refused));
        send_raw(from, kRefuse, refuse.bytes());
        return;
    }
    if (known == nullptr) {
        // A new member: the others hear of it in order, before any of its messages.
        const PeerId id = new_id();
        for (const auto& remote : remotes_) send_routed(*remote, Channel::Ordered, kRoutedJoined, self_, id, {});
        remotes_.push_back(std::make_unique<Remote>(*this, from, id));
        known = remotes_.back().get();
        known->join_nonce = nonce;
        known->last_heard = now;
        known->last_ping = now;
        if (callbacks_.on_peer_joined) callbacks_.on_peer_joined(id);
        known = find(from);
        if (known == nullptr) return;  // the callback removed it
    }
    // Welcome (again, when the first was lost): the lobby and who is in it.
    ByteWriter welcome;
    welcome.u64(nonce).u64(lobby_).u64(known->id).u64(self_);
    std::vector<PeerId> others;
    for (const auto& remote : remotes_) {
        if (remote.get() != known) others.push_back(remote->id);
    }
    welcome.u8(static_cast<std::uint8_t>(others.size()));
    for (const PeerId other : others) welcome.u64(other);
    send_raw(from, kWelcome, welcome.bytes());
}

void DirectTransport::receive_welcome(std::span<const std::uint8_t> body, const Address& from, Clock::time_point now) {
    ByteReader reader(body);
    const std::uint64_t nonce = reader.u64();
    const std::uint64_t lobby = reader.u64();
    const PeerId self = reader.u64();
    const PeerId owner = reader.u64();
    const unsigned count = reader.u8();
    std::vector<PeerId> others;
    for (unsigned i = 0; i < count; ++i) others.push_back(reader.u64());
    if (!reader.ok() || nonce != join_nonce_ || self == kNoPeer || owner == kNoPeer) return;
    state_ = State::Lobby;
    lobby_ = lobby;
    self_ = self;
    owner_ = owner;
    others_ = std::move(others);
    remotes_.clear();
    remotes_.push_back(std::make_unique<Remote>(*this, from, owner));
    remotes_.back()->last_heard = now;
    remotes_.back()->last_ping = now;
    if (callbacks_.on_lobby_joined) callbacks_.on_lobby_joined(self_, owner_, lobby_);
    if (state_ != State::Lobby) return;
    const std::vector<PeerId> already = others_;
    for (const PeerId other : already) {
        if (callbacks_.on_peer_joined) callbacks_.on_peer_joined(other);
        if (state_ != State::Lobby) return;
    }
}

void DirectTransport::receive_message(Remote& remote, const LinkMessage& message) {
    ByteReader reader(message.data);
    const std::uint8_t kind = reader.u8();
    const PeerId from = reader.u64();
    const PeerId to = reader.u64();
    if (!reader.ok()) return;
    const std::span<const std::uint8_t> data = reader.rest();
    if (host_) {
        // From a member: only its own messages, passed on to whom they are for.
        if (kind != kRoutedApp || from != remote.id) return;
        const PeerId sender = remote.id;
        if (to == kEveryone) {
            for (const auto& other : remotes_) {
                if (other->id != sender) send_routed(*other, message.channel, kRoutedApp, sender, kEveryone, data);
            }
        } else if (to != self_) {
            if (Remote* target = find(to)) send_routed(*target, message.channel, kRoutedApp, sender, to, data);
            return;
        }
        if (callbacks_.on_receive) callbacks_.on_receive(sender, message.channel, data);
        return;
    }
    // From the host: messages (its own or passed on) and the lobby's news.
    switch (kind) {
    case kRoutedApp:
        if (to != kEveryone && to != self_) return;
        if (callbacks_.on_receive) callbacks_.on_receive(from, message.channel, data);
        return;
    case kRoutedJoined:
        if (to == self_ || to == kNoPeer || std::find(others_.begin(), others_.end(), to) != others_.end()) return;
        others_.push_back(to);
        if (callbacks_.on_peer_joined) callbacks_.on_peer_joined(to);
        return;
    case kRoutedLeft: {
        ByteReader news(data);
        const bool intentional = news.u8() != 0u;
        drop(to, intentional, NetError::None);
        return;
    }
    case kRoutedKicked:
        if (to == self_) fail(NetError::Kicked);
        return;
    default: return;
    }
}

}  // namespace galaxy::net
