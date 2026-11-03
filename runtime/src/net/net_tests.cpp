// Online co-op's network layer: the channels (galaxy/net/link.h) over a line that loses, repeats and reorders
// datagrams, the direct transport's lobby (galaxy/net/direct_transport.h) and the session over it
// (galaxy/coop_session.h), both over the loopback, with the join's checks and the room's mods (galaxy/coop_mods.h).
#include "galaxy/coop_mods.h"
#include "galaxy/coop_session.h"
#include "galaxy/net/bytes.h"
#include "galaxy/net/direct_transport.h"
#include "galaxy/net/link.h"
#include "galaxy/net/udp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

using namespace std::chrono_literals;
using galaxy::net::Channel;
using galaxy::net::Link;
using galaxy::net::LinkMessage;
using Clock = std::chrono::steady_clock;

std::vector<std::uint8_t> bytes_of(const std::string& text) { return {text.begin(), text.end()}; }
std::string text_of(const std::vector<std::uint8_t>& bytes) { return {bytes.begin(), bytes.end()}; }

// Two links joined by a line that loses `loss` of the datagrams, repeats some and delivers them out of order, on a
// clock the test moves.
struct Line {
    std::mt19937 random{1234u};
    double loss = 0.0;
    std::deque<std::vector<std::uint8_t>> to_b, to_a;
    Link a{[this](std::span<const std::uint8_t> frame) { carry(to_b, frame); }};
    Link b{[this](std::span<const std::uint8_t> frame) { carry(to_a, frame); }};
    Clock::time_point now = Clock::now();
    std::vector<LinkMessage> at_a, at_b;

    void carry(std::deque<std::vector<std::uint8_t>>& queue, std::span<const std::uint8_t> frame) {
        std::uniform_real_distribution<double> chance(0.0, 1.0);
        if (chance(random) < loss) return;
        const std::vector<std::uint8_t> copy(frame.begin(), frame.end());
        // Out of order: sometimes in front of what waits.
        if (!queue.empty() && chance(random) < 0.3) {
            queue.push_front(copy);
        } else {
            queue.push_back(copy);
        }
        if (chance(random) < 0.05) queue.push_back(copy);  // repeated
    }

    void step(std::chrono::milliseconds by = 5ms) {
        now += by;
        while (!to_b.empty()) {
            const std::vector<std::uint8_t> frame = std::move(to_b.front());
            to_b.pop_front();
            b.receive(frame, now, at_b);
        }
        while (!to_a.empty()) {
            const std::vector<std::uint8_t> frame = std::move(to_a.front());
            to_a.pop_front();
            a.receive(frame, now, at_a);
        }
        a.update(now);
        b.update(now);
    }
};

void test_link_perfect_line() {
    Line line;
    expect(line.a.send(Channel::Unreliable, bytes_of("snap"), line.now), "an unreliable message is sent");
    expect(line.a.send(Channel::Reliable, bytes_of("once"), line.now), "a reliable message is sent");
    expect(line.a.send(Channel::Ordered, bytes_of("first"), line.now), "an ordered message is sent");
    for (int i = 0; i < 10; ++i) line.step();
    expect(line.at_b.size() == 3u, "the three arrive over a perfect line");
    expect(line.a.in_flight() == 0u, "and every reliable one is acknowledged");
    expect(line.a.resends() == 0u, "with nothing sent twice");
    expect(!line.a.send(Channel::Unreliable, std::vector<std::uint8_t>(galaxy::net::kMaxSmallMessage + 1u), line.now),
           "an unreliable message longer than a datagram is refused");
}

void test_link_lossy_line() {
    Line line;
    line.loss = 0.3;
    constexpr int kCount = 400;
    for (int i = 0; i < kCount; ++i) {
        expect(line.a.send(Channel::Ordered, bytes_of("ordered " + std::to_string(i)), line.now), "ordered queued");
        expect(line.a.send(Channel::Reliable, bytes_of("reliable " + std::to_string(i)), line.now), "reliable queued");
        if (i % 4 == 0) line.step(1ms);
    }
    for (int i = 0; i < 2000 && (line.a.in_flight() != 0u || line.a.queued() != 0u); ++i) line.step(10ms);
    std::vector<std::string> ordered;
    std::vector<std::string> reliable;
    for (const LinkMessage& message : line.at_b) {
        (message.channel == Channel::Ordered ? ordered : reliable).push_back(text_of(message.data));
    }
    expect(!line.a.broken(), "a lossy line doesn't break the link");
    expect(ordered.size() == static_cast<std::size_t>(kCount), "every ordered message arrives once");
    bool in_order = ordered.size() == static_cast<std::size_t>(kCount);
    for (std::size_t i = 0; in_order && i < ordered.size(); ++i) in_order = ordered[i] == "ordered " + std::to_string(i);
    expect(in_order, "in the order sent");
    std::sort(reliable.begin(), reliable.end());
    expect(std::adjacent_find(reliable.begin(), reliable.end()) == reliable.end() &&
               reliable.size() == static_cast<std::size_t>(kCount),
           "every reliable message arrives once");
    expect(line.a.resends() > 0u, "lost ones were sent again");
}

void test_link_long_message() {
    Line line;
    line.loss = 0.1;
    std::vector<std::uint8_t> save(300000u);
    for (std::size_t i = 0; i < save.size(); ++i) save[i] = static_cast<std::uint8_t>(i * 7u + (i >> 9));
    expect(line.a.send(Channel::Ordered, bytes_of("before"), line.now), "a message before");
    expect(line.a.send(Channel::Ordered, save, line.now), "a long ordered message is cut up");
    expect(line.a.send(Channel::Ordered, bytes_of("after"), line.now), "a message after");
    expect(line.a.queued() > 0u, "more pieces than the window holds wait");
    for (int i = 0; i < 4000 && (line.a.in_flight() != 0u || line.a.queued() != 0u); ++i) line.step(10ms);
    expect(line.at_b.size() == 3u, "it arrives as one message among the others");
    expect(line.at_b.size() == 3u && line.at_b[1].data == save, "joined again as sent");
    expect(line.at_b.size() == 3u && text_of(line.at_b[0].data) == "before" && text_of(line.at_b[2].data) == "after",
           "in its place");
}

void test_link_gives_up() {
    Line line;
    line.loss = 1.0;
    expect(line.a.send(Channel::Reliable, bytes_of("lost"), line.now), "a message into nothing");
    for (int i = 0; i < 200 && !line.a.broken(); ++i) line.step(100ms);
    expect(line.a.broken(), "the link breaks once it went unacknowledged long enough");
    expect(!line.a.send(Channel::Reliable, bytes_of("more"), line.now), "and sends nothing more");
}

void test_bytes() {
    galaxy::net::ByteWriter out;
    out.u8(7).u16(0xbeef).u32(0xdeadbeef).u64(0x0123456789abcdefull).f32(1.5f).str("name").blob(bytes_of("blob"));
    galaxy::net::ByteReader in(out.bytes());
    expect(in.u8() == 7 && in.u16() == 0xbeef && in.u32() == 0xdeadbeef && in.u64() == 0x0123456789abcdefull &&
               in.f32() == 1.5f && in.str() == "name" && text_of(in.blob(16)) == "blob" && in.ok() && in.left() == 0u,
           "numbers, strings and bytes read back as written");
    (void)in.u32();
    expect(!in.ok(), "reading past the end is noticed");
}

void test_addresses() {
    galaxy::net::Address four;
    galaxy::net::Address six;
    expect(galaxy::net::resolve("127.0.0.1", 7777u, four) && four.to_string() == "127.0.0.1:7777" && !four.ipv6(),
           "an IPv4 address resolves");
    if (galaxy::net::resolve("::1", 7778u, six)) {
        expect(six.ipv6() && six.to_string() == "[::1]:7778" && six.port() == 7778u, "an IPv6 address resolves");
    }
    expect(!(four == six), "different addresses differ");
}

// Updates the transports until `done` or the limit passes: long enough for a message lost several times over at 20%
// loss (the resend timeout doubles to 2 s), as `done` ends it as soon as it's true.
// Callbacks by name, the others left empty.
galaxy::net::TransportCallbacks callbacks(std::function<void(galaxy::net::TransportCallbacks&)> fill) {
    galaxy::net::TransportCallbacks out;
    fill(out);
    return out;
}

galaxy::net::TransportCallbacks on_error(std::function<void(galaxy::net::NetError)> handle) {
    galaxy::net::TransportCallbacks out;
    out.on_error = [handle](galaxy::net::NetError error, galaxy::net::PeerId) { handle(error); };
    return out;
}

bool pump(std::vector<galaxy::net::DirectTransport*> transports, const std::function<bool()>& done,
          std::chrono::milliseconds limit = 15000ms) {
    const Clock::time_point until = Clock::now() + limit;
    while (Clock::now() < until) {
        for (auto* transport : transports) transport->update(1ms);
        if (done()) return true;
    }
    return false;
}

void test_transport_lobby() {
    using namespace galaxy::net;
    DirectTransport host, a, b;
    std::vector<std::string> host_got, a_got, b_got;
    PeerId a_id = kNoPeer, b_id = kNoPeer;
    int a_joined_peers = 0;
    bool b_left_seen_by_a = false;
    NetError a_error = NetError::None;
    host.set_callbacks(callbacks([&](TransportCallbacks& c) {
        c.on_receive = [&](PeerId, Channel, std::span<const std::uint8_t> data) {
            host_got.emplace_back(data.begin(), data.end());
        };
    }));
    a.set_callbacks(callbacks([&](TransportCallbacks& c) {
        c.on_lobby_joined = [&](PeerId self, PeerId, std::uint64_t) { a_id = self; };
        c.on_peer_joined = [&](PeerId) { ++a_joined_peers; };
        c.on_peer_left = [&](PeerId, bool intentional) { b_left_seen_by_a = intentional; };
        c.on_receive = [&](PeerId, Channel, std::span<const std::uint8_t> data) {
            a_got.emplace_back(data.begin(), data.end());
        };
        c.on_error = [&](NetError error, PeerId) { a_error = error; };
    }));
    b.set_callbacks(callbacks([&](TransportCallbacks& c) {
        c.on_lobby_joined = [&](PeerId self, PeerId, std::uint64_t) { b_id = self; };
        c.on_receive = [&](PeerId, Channel, std::span<const std::uint8_t> data) {
            b_got.emplace_back(data.begin(), data.end());
        };
    }));

    LobbyConfig config;
    config.password = "secret";
    config.max_members = 3u;
    expect(host.lobby_create(config) && host.in_lobby() && host.is_host() && host.port() != 0u, "the host opens a lobby");
    const std::uint16_t port = host.port();

    // A wrong password.
    DirectTransport wrong;
    NetError wrong_error = NetError::None;
    wrong.set_callbacks(on_error([&](NetError error) { wrong_error = error; }));
    expect(wrong.lobby_join({"127.0.0.1", port, "nope"}), "a join with the wrong password starts");
    expect(pump({&host, &wrong}, [&] { return wrong_error != NetError::None; }) &&
               wrong_error == NetError::PasswordIncorrect && !wrong.in_lobby(),
           "and is refused for its password");

    host.set_test_loss(0.2);
    a.set_test_loss(0.2);
    expect(a.lobby_join({"127.0.0.1", port, "secret"}), "a member joins");
    expect(pump({&host, &a}, [&] { return a.in_lobby(); }), "and is let in over a lossy line");
    expect(a.self() == a_id && a.owner() == host.self() && a_id != host.self(), "with its own id and the host's");
    expect(b.lobby_join({"localhost", port, "secret"}), "another member joins");
    expect(pump({&host, &a, &b}, [&] { return b.in_lobby() && a_joined_peers == 1; }),
           "and the first member hears of it");

    // Full: three members the most.
    DirectTransport full;
    NetError full_error = NetError::None;
    full.set_callbacks(on_error([&](NetError error) { full_error = error; }));
    (void)full.lobby_join({"127.0.0.1", port, "secret"});
    expect(pump({&host, &a, &b, &full}, [&] { return full_error != NetError::None; }) &&
               full_error == NetError::LobbyFull,
           "a member past the lobby's size is refused");

    // Messages: to the host, to everyone (the host passes it on), to one member.
    for (int i = 0; i < 50; ++i) {
        expect(a.send_to(kEveryone, Channel::Ordered, bytes_of("hello " + std::to_string(i))), "a member sends");
    }
    expect(host.send_to(b_id, Channel::Reliable, bytes_of("just b")), "the host sends to one member");
    expect(a.send_to(b_id, Channel::Ordered, bytes_of("a to b")), "a member sends to another");
    expect(pump({&host, &a, &b}, [&] { return host_got.size() == 50u && b_got.size() == 52u; }),
           "every message reaches whom it is for");
    bool in_order = b_got.size() == 52u;
    std::size_t hello = 0u;
    for (const std::string& got : b_got) {
        if (got.rfind("hello ", 0) == 0) in_order = in_order && got == "hello " + std::to_string(hello++);
    }
    expect(in_order, "a member's ordered messages reach another in order, through the host");
    expect(a_got.empty(), "and not back to their sender");

    // Leaving: the others hear of it. Over a perfect line from here: a Bye is never sent again, so a lost one is only
    // noticed when the member times out (DirectTransport::kTimeout), longer than this test waits.
    host.set_test_loss(0.0);
    a.set_test_loss(0.0);
    b.lobby_leave();
    expect(pump({&host, &a}, [&] { return b_left_seen_by_a; }), "a member leaving is told to the others");
    host.lobby_leave();
    expect(pump({&a}, [&] { return a_error != NetError::None; }) && a_error == NetError::HostLeft && !a.in_lobby(),
           "the host closing the lobby ends it for its members");

    // Nobody there.
    DirectTransport alone;
    NetError alone_error = NetError::None;
    alone.set_callbacks(on_error([&](NetError error) { alone_error = error; }));
    (void)alone.lobby_join({"127.0.0.1", port, ""});
    expect(pump({&alone}, [&] { return alone_error != NetError::None; }, 12000ms) &&
               alone_error == NetError::LobbyNotFound,
           "a join nobody answers fails");
}

namespace fs = std::filesystem;

// A folder of its own for a test's files, removed after.
struct TempFolder {
    fs::path path;
    explicit TempFolder(const char* name) {
        path = fs::temp_directory_path() / (std::string("nebula_") + name + "_" +
                                            std::to_string(Clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> pattern(std::size_t size, unsigned seed) {
    std::vector<std::uint8_t> bytes(size);
    std::mt19937 random{seed};
    for (std::uint8_t& b : bytes) b = static_cast<std::uint8_t>(random());
    return bytes;
}

std::uint64_t hash_of(std::string_view text) {
    galaxy::coop::ContentHash hash;
    hash.update(text);
    return hash.finish();
}

void test_coop_mods() {
    using namespace galaxy::coop;
    // XXH64's own test values (python-xxhash), whole and a piece at a time.
    expect(hash_of("") == 0xef46db3751d8e999ull, "the hash of nothing");
    expect(hash_of("abc") == 0x44bc2cf5ad770999ull, "the hash of 3 bytes");
    expect(hash_of("Nobody inspects the spammish repetition") == 0xfbcea83c8a378bf1ull, "the hash of 39 bytes");
    const std::vector<std::uint8_t> long_bytes = pattern(1000u, 7u);
    ContentHash whole;
    whole.update(long_bytes);
    ContentHash pieces;
    for (std::size_t at = 0; at < long_bytes.size();) {
        const std::size_t take = std::min<std::size_t>(1u + at % 37u, long_bytes.size() - at);
        pieces.update(std::span<const std::uint8_t>(long_bytes.data() + at, take));
        at += take;
    }
    expect(whole.finish() == pieces.finish(), "the hash fed in pieces is the hash of the whole");

    expect(safe_mod_path("riivolution/mod.xml") && safe_mod_path("Mario.arc"), "a mod's paths");
    expect(!safe_mod_path("../x") && !safe_mod_path("a/../b") && !safe_mod_path("/abs") && !safe_mod_path("a//b") &&
               !safe_mod_path("c:/x") && !safe_mod_path("a\\b") && !safe_mod_path("") && !safe_mod_path("a/"),
           "and none that leave the mod's folder");

    TempFolder temp("coop_mods");
    const fs::path mod_folder = temp.path / "My Mod";
    write_file(mod_folder / "riivolution" / "mod.xml", bytes_of("<wiidisc/>"));
    write_file(mod_folder / "files" / "StageData" / "Big.arc", pattern(200000u, 1u));
    write_file(mod_folder / "empty.txt", {});
    write_file(mod_folder / "Thumbs.db", bytes_of("system"));
    write_file(mod_folder / ".hidden" / "x", bytes_of("system"));
    SharedMod mod;
    mod.id = "My Mod";
    mod.folder = mod_folder;
    mod.options = {{"key", 2u}};
    expect(describe_mod(mod), "a mod's folder is read");
    expect(mod.files.size() == 3u && mod.files[0].path == "empty.txt" &&
               mod.files[1].path == "files/StageData/Big.arc" && mod.files[2].path == "riivolution/mod.xml" &&
               mod.size() == 200010u,
           "its files by path, the system's own left out");
    const fs::path copy_folder = temp.path / "copy";
    fs::copy(mod_folder, copy_folder, fs::copy_options::recursive);
    fs::remove(copy_folder / "Thumbs.db");
    SharedMod copy = mod;
    copy.folder = copy_folder;
    expect(describe_mod(copy) && copy.hash == mod.hash && copy.plays_like(mod), "a copy has the same hash");
    write_file(copy_folder / "empty.txt", bytes_of("!"));
    expect(describe_mod(copy) && copy.hash != mod.hash, "a changed copy doesn't");
    SharedMod other_choice = mod;
    other_choice.options = {{"key", 1u}};
    expect(!other_choice.plays_like(mod), "another choice plays otherwise");

    galaxy::net::ByteWriter out;
    write_mods(out, std::vector<SharedMod>{mod}, true);
    galaxy::net::ByteReader in(out.bytes());
    const std::optional<std::vector<SharedMod>> back = read_mods(in, true);
    expect(back && back->size() == 1u && (*back)[0].plays_like(mod) && (*back)[0].files == mod.files,
           "a list of mods is read back");
    SharedMod bad = mod;
    bad.files[0].path = "../escape";
    galaxy::net::ByteWriter bad_out;
    write_mods(bad_out, std::vector<SharedMod>{bad}, true);
    galaxy::net::ByteReader bad_in(bad_out.bytes());
    expect(!read_mods(bad_in, true), "and refused when a path leaves the mod's folder");

    // The cache: a download is kept only when it is the mod.
    const ModCache cache(temp.path / "cache");
    expect(!cache.find(mod), "an empty cache has no mod");
    const fs::path part = cache.begin(mod);
    expect(!part.empty(), "a download starts");
    fs::copy(mod_folder, part, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    const std::optional<fs::path> kept = cache.finish(mod);
    expect(kept && cache.find(mod) == kept, "a whole download is kept");
    const fs::path part2 = cache.begin(other_choice);
    write_file(part2 / "wrong.bin", bytes_of("wrong"));
    SharedMod broken = mod;
    broken.hash ^= 1u;
    const fs::path part3 = cache.begin(broken);
    fs::copy(mod_folder, part3, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    expect(!cache.finish(broken) && !fs::exists(part3), "a damaged one is dropped");

    NextJoin join;
    join.address = "example.org:7777";
    join.mods = {mod};
    const fs::path next = temp.path / "next_join";
    expect(write_next_join(next, join), "a join for the next start is written");
    const std::optional<NextJoin> got = take_next_join(next);
    expect(got && got->address == join.address && got->mods.size() == 1u && got->mods[0].plays_like(mod) &&
               got->mods[0].folder == mod_folder,
           "and read back");
    expect(!take_next_join(next), "once");
}

// Until `done` or the time passes, the session's thread doing the work.
bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit = 15000ms) {
    const Clock::time_point until = Clock::now() + limit;
    while (Clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

bool has_event(const std::vector<galaxy::coop::SessionEvent>& events, galaxy::coop::SessionEvent::Kind kind) {
    return std::any_of(events.begin(), events.end(), [kind](const auto& e) { return e.kind == kind; });
}

void test_session() {
    using namespace galaxy::coop;
    using Kind = SessionEvent::Kind;
    Session host_session;
    HostConfig host;
    host.port = 0u;
    host.room.max_players = 3u;
    host.room.cheats = true;
    host.room.password = "pw";
    host.game_version = "test 1";
    host.name = "HOSTY";
    host.save = [] {
        RoomSave save;
        save.file = 3u;
        save.files.push_back({"GameData.bin", std::vector<std::uint8_t>(20000u, 0x5au)});
        return save;
    };
    expect(host_session.host(host) && host_session.is_host(), "a session hosts");
    const std::uint16_t port = host_session.port();
    expect(port != 0u, "on a port");
    expect(host_session.players().size() == 1u && host_session.players()[0].host &&
               host_session.players()[0].name == "HOSTY",
           "with the host as its only player");

    // Another version is refused.
    Session old;
    expect(old.join({"127.0.0.1", port, "pw", "test 0", "OLD", galaxy::CoopCharacter::Mario}), "an old game joins");
    std::vector<SessionEvent> old_events;
    expect(wait_for([&] {
               for (auto& e : old.take_events()) old_events.push_back(e);
               return has_event(old_events, Kind::JoinFailed);
           }),
           "and is refused");
    expect(!old_events.empty() && old_events.back().text.find("ANOTHER VERSION") != std::string::npos,
           "for its version");

    Session joiner;
    expect(joiner.join_step() == JoinStep::None, "a session not joining has no join step");
    expect(joiner.join({"127.0.0.1", port, "pw", "test 1", "LUIGI FAN", galaxy::CoopCharacter::Luigi}), "a player joins");
    expect(joiner.join_step() == JoinStep::Reaching || joiner.join_step() == JoinStep::Asking ||
               joiner.state() == SessionState::Joined,
           "reaching the host, then asking it");
    std::vector<SessionEvent> joiner_events;
    expect(wait_for([&] {
               for (auto& e : joiner.take_events()) joiner_events.push_back(e);
               return has_event(joiner_events, Kind::Joined);
           }),
           "and is let in");
    expect(joiner.join_step() == JoinStep::None, "and is joining no more");
    const std::optional<RoomSave> save = joiner.take_room_save();
    expect(save && save->file == 3u && save->files.size() == 1u && save->files[0].name == "GameData.bin" &&
               save->files[0].bytes == std::vector<std::uint8_t>(20000u, 0x5au),
           "with the host's save");
    expect(joiner.room().cheats && joiner.room().max_players == 3u, "and the room's settings");
    const std::vector<SessionPlayer> players = joiner.players();
    expect(players.size() == 2u && players[0].host && players[0].name == "HOSTY" && players[1].index == 1u &&
               players[1].local && players[1].character == galaxy::CoopCharacter::Luigi,
           "and who is playing");
    std::vector<SessionEvent> host_events = host_session.take_events();
    expect(has_event(host_events, Kind::PlayerJoined) && host_session.players().size() == 2u,
           "the host sees the player join");

    // A feature's messages, both ways and passed on.
    std::atomic<int> host_got{0}, joiner_got{0}, third_got{0};
    host_session.on_message(kFirstFeatureMessage, [&](const SessionPlayer& from, std::span<const std::uint8_t> data) {
        if (from.index == 1u && text_of({data.begin(), data.end()}) == "hi") ++host_got;
    });
    joiner.on_message(kFirstFeatureMessage, [&](const SessionPlayer& from, std::span<const std::uint8_t>) {
        if (from.host) ++joiner_got;
    });
    Session third;
    third.on_message(kFirstFeatureMessage, [&](const SessionPlayer& from, std::span<const std::uint8_t>) {
        if (from.index == 1u) ++third_got;
    });
    expect(third.join({"127.0.0.1", port, "pw", "test 1", "THIRD", galaxy::CoopCharacter::Mario}), "a third joins");
    expect(wait_for([&] { return third.state() == SessionState::Joined && joiner.players().size() == 3u; }),
           "and everyone knows of it");
    expect(joiner.send(kFirstFeatureMessage, galaxy::net::Channel::Reliable, bytes_of("hi")), "a player sends");
    expect(host_session.send(kFirstFeatureMessage, galaxy::net::Channel::Unreliable, bytes_of("yo")), "the host sends");
    expect(wait_for([&] { return host_got == 1 && joiner_got >= 1 && third_got == 1; }),
           "a feature's messages reach every player");

    // Where each plays, for the player list: told to everyone as it changes, and to who joins later with the room.
    const auto place_of = [](const Session& session, unsigned index) {
        for (const SessionPlayer& player : session.players()) {
            if (player.index == index) return player.stage + "/" + std::to_string(player.scenario);
        }
        return std::string("none");
    };
    host_session.set_place("AstroGalaxy", 0);
    joiner.set_place("EggStarGalaxy", 2);
    third.set_place("Bad name!", 1);
    expect(wait_for([&] {
               return place_of(third, 0u) == "AstroGalaxy/0" && place_of(third, 1u) == "EggStarGalaxy/2" &&
                      place_of(host_session, 1u) == "EggStarGalaxy/2" && place_of(joiner, 0u) == "AstroGalaxy/0" &&
                      place_of(joiner, 2u) == "/1" && place_of(joiner, 1u) == "EggStarGalaxy/2";
           }),
           "every player's place reaches everyone (a name the game couldn't have is dropped)");
    const std::vector<SessionPlayer> seen = joiner.players();
    expect(std::all_of(seen.begin(), seen.end(), [](const SessionPlayer& p) { return !p.local || p.ping_ms == 0u; }),
           "and this player has no round trip of its own");

    // A player's new name (Options' PLAYER page) reaches everyone as it changes, cleaned as at a join.
    const auto name_of = [](const Session& session, unsigned index) {
        for (const SessionPlayer& player : session.players()) {
            if (player.index == index) return player.name;
        }
        return std::string("none");
    };
    joiner.set_name("LUIGI\x01 NUMBER ONE FAN FOREVER");
    expect(name_of(joiner, 1u) == "LUIGI NUMBER ONE FAN", "a renamed player sees its new name at once");
    host_session.set_name("");
    expect(wait_for([&] {
               return name_of(host_session, 1u) == "LUIGI NUMBER ONE FAN" &&
                      name_of(third, 1u) == "LUIGI NUMBER ONE FAN" && name_of(joiner, 1u) == "LUIGI NUMBER ONE FAN" &&
                      name_of(joiner, 0u) == "PLAYER" && name_of(third, 0u) == "PLAYER";
           }),
           "a joiner's and the host's new names reach everyone");
    expect(place_of(third, 1u) == "EggStarGalaxy/2" && name_of(third, 2u) == "THIRD",
           "keeping their place, and the others' names");
    host_session.set_name("HOSTY");
    expect(wait_for([&] { return name_of(joiner, 0u) == "HOSTY" && name_of(third, 0u) == "HOSTY"; }),
           "and changes again");

    // Each player's Power Stars this room (the Personal Star Counter): the host counts them and tells everyone.
    const auto stars_of = [](const Session& session, unsigned index) {
        for (const SessionPlayer& player : session.players()) {
            if (player.index == index) return static_cast<int>(player.stars);
        }
        return -1;
    };
    const auto id_of = [](const Session& session, unsigned index) {
        for (const SessionPlayer& player : session.players()) {
            if (player.index == index) return player.id;
        }
        return galaxy::net::kNoPeer;
    };
    expect(host_session.stars() == 0u && joiner.stars() == 0u && stars_of(third, 1u) == 0, "a room starts with no stars");
    Session idle;
    idle.credit_star(id_of(host_session, 1u));
    joiner.credit_star(id_of(joiner, 1u));
    expect(idle.stars() == 0u && stars_of(host_session, 1u) == 0, "only the host counts them");
    host_session.credit_star(id_of(host_session, 1u));
    host_session.credit_star(id_of(host_session, 1u));
    host_session.credit_star(id_of(host_session, 0u));
    expect(stars_of(host_session, 1u) == 2 && host_session.stars() == 1u, "a player's stars count up");
    expect(wait_for([&] {
               return stars_of(third, 1u) == 2 && stars_of(third, 0u) == 1 && joiner.stars() == 2u &&
                      stars_of(joiner, 0u) == 1 && stars_of(joiner, 2u) == 0;
           }),
           "and reach everyone");

    // Full: the room holds three.
    Session fourth;
    (void)fourth.join({"127.0.0.1", port, "pw", "test 1", "FOURTH", galaxy::CoopCharacter::Mario});
    std::vector<SessionEvent> fourth_events;
    expect(wait_for([&] {
               for (auto& e : fourth.take_events()) fourth_events.push_back(e);
               return has_event(fourth_events, Kind::JoinFailed);
           }),
           "a player past the room's size is refused");

    // The host changes the room.
    galaxy::RuntimeCoopSettings room = host_session.room();
    room.cheats = false;
    host_session.set_room(room);
    expect(wait_for([&] { return !joiner.room().cheats; }), "the host's room changes reach the players");

    third.leave();
    expect(wait_for([&] { return host_session.players().size() == 2u && joiner.players().size() == 2u; }),
           "a player leaving leaves everyone's list");
    Session late;
    expect(late.join({"127.0.0.1", port, "pw", "test 1", "LATE", galaxy::CoopCharacter::Mario}) &&
               wait_for([&] { return late.state() == SessionState::Joined; }),
           "a player joins later");
    expect(place_of(late, 0u) == "AstroGalaxy/0" && place_of(late, 1u) == "EggStarGalaxy/2",
           "and gets everyone's place with the room");
    expect(stars_of(late, 0u) == 1 && stars_of(late, 1u) == 2 && late.stars() == 0u,
           "and everyone's stars, starting from none itself");
    expect(name_of(late, 0u) == "HOSTY" && name_of(late, 1u) == "LUIGI NUMBER ONE FAN", "and everyone's names as now");
    late.leave();
    expect(wait_for([&] { return host_session.players().size() == 2u; }), "and leaves");
    host_session.leave();
    expect(wait_for([&] {
               for (auto& e : joiner.take_events()) joiner_events.push_back(e);
               return has_event(joiner_events, Kind::Closed);
           }),
           "the host leaving closes the room for the others");
    expect(joiner.state() == SessionState::Idle, "who are back to no session");
}

// The join's checks and the room's mods: another disc is refused; a joiner without the room's mods downloads them
// from the host, and with them is let in.
void test_session_mods() {
    using namespace galaxy::coop;
    using Kind = SessionEvent::Kind;
    TempFolder temp("session_mods");
    const fs::path host_mod = temp.path / "host" / "Big Mod";
    write_file(host_mod / "files" / "StageData" / "Big.arc", pattern(700000u, 3u));
    write_file(host_mod / "riivolution" / "big.xml", bytes_of("<wiidisc/>"));
    write_file(host_mod / "empty.bin", {});
    const fs::path small_mod = temp.path / "host" / "Small";
    write_file(small_mod / "Mario.arc", pattern(3000u, 4u));
    std::vector<SharedMod> room_mods(2u);
    room_mods[0].id = "Big Mod";
    room_mods[0].folder = host_mod;
    room_mods[0].options = {{"Big option", 1u}};
    room_mods[1].id = "Small";
    room_mods[1].folder = small_mod;
    expect(describe_mod(room_mods[0]) && describe_mod(room_mods[1]), "the host's mods are read");

    DiscInfo disc{"RMGE01", 0u, 0x1234u};
    Session host_session;
    HostConfig host;
    host.port = 0u;
    host.game_version = "test 1";
    host.name = "HOSTY";
    host.disc = disc;
    host.mods = room_mods;
    expect(host_session.host(host), "a room with mods opens");
    const std::uint16_t port = host_session.port();

    const auto join_events = [](Session& session, Kind until, std::vector<SessionEvent>& events,
                                std::chrono::milliseconds limit = 5000ms) {
        return wait_for(
            [&] {
                for (auto& e : session.take_events()) events.push_back(e);
                return has_event(events, until) || has_event(events, Kind::JoinFailed);
            },
            limit);
    };

    // Another disc.
    {
        Session other;
        JoinConfig config{"127.0.0.1", port, "", "test 1", "PAL", galaxy::CoopCharacter::Mario};
        config.disc = {"RMGP01", 0u, 0x9999u};
        config.mods = room_mods;
        expect(other.join(config), "a player with another disc joins");
        std::vector<SessionEvent> events;
        expect(join_events(other, Kind::Joined, events) && has_event(events, Kind::JoinFailed) &&
                   events.back().text.find("ANOTHER DISC (RMGE01 REVISION 0)") != std::string::npos,
               "and is refused for it");
    }
    // The same release, another image.
    {
        Session other;
        JoinConfig config{"127.0.0.1", port, "", "test 1", "BAD DUMP", galaxy::CoopCharacter::Mario};
        config.disc = {"RMGE01", 0u, 0x9999u};
        config.mods = room_mods;
        expect(other.join(config), "a player with a changed disc joins");
        std::vector<SessionEvent> events;
        expect(join_events(other, Kind::Joined, events) && has_event(events, Kind::JoinFailed) &&
                   events.back().text.find("ISN'T THE SAME") != std::string::npos,
               "and is refused for it");
    }

    // No mods: the room's download, then the session ends for Nebula to start again with them.
    const fs::path cache = temp.path / "joiner" / "coop_mods";
    const fs::path own_small = temp.path / "joiner" / "mods" / "Small";
    fs::create_directories(own_small.parent_path());
    fs::copy(small_mod, own_small, fs::copy_options::recursive);
    std::vector<SharedMod> ready;
    {
        Session joiner;
        JoinConfig config{"127.0.0.1", port, "", "test 1", "NEW", galaxy::CoopCharacter::Luigi};
        config.disc = disc;
        config.mods_cache = cache;
        // The player's own copy of Small.
        config.find_mod = [&](const SharedMod& mod) -> std::optional<fs::path> {
            SharedMod own = mod;
            own.folder = temp.path / "joiner" / "mods" / mod.id;
            if (describe_mod(own) && own.hash == mod.hash) return own.folder;
            return std::nullopt;
        };
        expect(joiner.join(config), "a player without the room's mods joins");
        std::vector<SessionEvent> events;
        bool progressed = false;
        expect(wait_for(
                   [&] {
                       const DownloadProgress progress = joiner.download_progress();
                       if (progress.active && progress.total == room_mods[0].size() && progress.mod == "Big Mod" &&
                           joiner.join_step() == JoinStep::DownloadingMods) {
                           progressed = true;
                       }
                       for (auto& e : joiner.take_events()) events.push_back(e);
                       return has_event(events, Kind::ModsReady) || has_event(events, Kind::JoinFailed);
                   },
                   15000ms),
               "the room's mods arrive");
        expect(has_event(events, Kind::DownloadingMods) && has_event(events, Kind::ModsReady), "as a download");
        expect(progressed, "whose progress shows");
        expect(joiner.state() == SessionState::Idle && host_session.players().size() == 1u,
               "the joiner was never a player");
        std::optional<std::vector<SharedMod>> got = joiner.take_room_mods();
        expect(got && same_mods(*got, room_mods), "the room's mods, in its order");
        if (got && got->size() == 2u) {
            expect((*got)[1].folder == own_small, "a mod the player has is theirs");
            expect((*got)[0].folder.parent_path() == cache &&
                       read_file((*got)[0].folder / "files" / "StageData" / "Big.arc") == pattern(700000u, 3u) &&
                       fs::is_regular_file((*got)[0].folder / "empty.bin"),
                   "a downloaded one is in the cache, whole");
            ready = *got;
        }
    }
    // With them (as Nebula started again): let in.
    {
        Session joiner;
        JoinConfig config{"127.0.0.1", port, "", "test 1", "NEW", galaxy::CoopCharacter::Luigi};
        config.disc = disc;
        config.mods = ready;
        config.mods_cache = cache;
        expect(joiner.join(config), "the player joins again with the room's mods");
        std::vector<SessionEvent> events;
        expect(join_events(joiner, Kind::Joined, events) && has_event(events, Kind::Joined), "and is let in");
        joiner.leave();
    }
    // With the room's mods in another order: the cache has them, so they're ready at once.
    {
        Session joiner;
        JoinConfig config{"127.0.0.1", port, "", "test 1", "SWAP", galaxy::CoopCharacter::Mario};
        config.disc = disc;
        config.mods = {ready[1], ready[0]};
        config.mods_cache = cache;
        config.find_mod = [&](const SharedMod& mod) -> std::optional<fs::path> {
            return mod.id == "Small" ? std::optional<fs::path>(own_small) : std::nullopt;
        };
        expect(joiner.join(config), "a player with the room's mods in another order joins");
        std::vector<SessionEvent> events;
        expect(join_events(joiner, Kind::ModsReady, events) && has_event(events, Kind::ModsReady) &&
                   !has_event(events, Kind::DownloadingMods),
               "and needs no download to play them the room's way");
    }
    host_session.leave();
}

}  // namespace

int main() {
    test_bytes();
    test_link_perfect_line();
    test_link_lossy_line();
    test_link_long_message();
    test_link_gives_up();
    test_addresses();
    test_transport_lobby();
    test_session();
    test_coop_mods();
    test_session_mods();
    if (s_failures == 0) std::printf("net tests passed\n");
    return s_failures == 0 ? 0 : 1;
}
