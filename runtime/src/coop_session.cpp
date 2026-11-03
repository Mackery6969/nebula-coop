#include "galaxy/coop_session.h"

#include "galaxy/coop_menu.h"
#include "galaxy/net/bytes.h"
#include "galaxy/net/direct_transport.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <utility>

namespace galaxy::coop {

namespace {

using net::ByteReader;
using net::ByteWriter;
using net::Channel;
using net::PeerId;
using Clock = std::chrono::steady_clock;

// The session's messages: a type, then its body.
enum Message : std::uint8_t {
    kJoinRequest = 1,   // joiner: the session protocol, the game's version, the player's name and character, the
                        // disc, the mods it plays
    kJoinAccept = 2,    // host: the joiner's number, the room, the save, everyone in the room
    kJoinRefuse = 3,    // host: why
    kPlayerInfo = 4,    // host: a player who joined (or changed)
    kRoomSettings = 5,  // host: the room's settings changed
    kPlayerPlace = 6,   // anyone, to everyone: where the sender plays now (its stage and scenario)
    kJoinMods = 7,      // host: the joiner plays other mods; the room's, with their files
    kModRequest = 8,    // joiner: a piece of a room mod's file: the mod, the file, where, how long
    kModPiece = 9,      // host: the mod, the file, where, then the bytes
    kPlayerStars = 10,  // host, to everyone: a player and the Power Stars they got in this room
    kPlayerName = 11,   // joiner, to the host: its new name (the host tells everyone with kPlayerInfo)
};

// A joiner that doesn't ask to be let in this soon is removed; a join that isn't let in this soon fails.
constexpr auto kJoinRequestTimeout = std::chrono::seconds(15);
constexpr auto kJoinTimeout = std::chrono::seconds(25);
// A refused joiner stays this long so the refusal reaches it before the host removes it.
constexpr auto kRefuseLinger = std::chrono::seconds(1);
constexpr auto kTick = std::chrono::milliseconds(2);
constexpr auto kIdleTick = std::chrono::milliseconds(20);
constexpr std::size_t kMaxSaveFiles = 64u;
// A joiner downloading the room's mods asks for this much at a time, this many pieces ahead; the host forgets one that
// asks for nothing this long.
constexpr std::uint32_t kPieceBytes = 60u << 10;
constexpr unsigned kModPiecesAhead = 4u;
constexpr auto kDownloadIdle = std::chrono::seconds(30);
// A stage's name: the game's are at most 47 characters (NebulaCoopPlayer).
constexpr std::size_t kStageNameLimit = 47u;
// A player's star count travels in 16 bits.
constexpr unsigned kStarsLimit = 0xffffu;

// A stage's name as the game names them (letters, digits and underscores, cut): nothing else reaches the game.
std::string clean_stage(std::string_view stage) {
    std::string out;
    for (const char c : stage) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!plain || out.size() >= kStageNameLimit) return {};
        out += c;
    }
    return out;
}

void write_room(ByteWriter& out, const RuntimeCoopSettings& room) {
    out.u8(static_cast<std::uint8_t>(room.player_interaction))
        .u8(static_cast<std::uint8_t>(std::min(room.knockback_strength, 255u)))
        .u8(static_cast<std::uint8_t>(room.pvp_mode))
        .u8(room.cheats ? 1u : 0u)
        .u8(static_cast<std::uint8_t>(room.max_players))
        .u8(room.star_counter ? 1u : 0u);
}

// Into `room`, keeping what isn't the room's (the player's name and character, the address).
void read_room(ByteReader& in, RuntimeCoopSettings& room) {
    const unsigned interaction = in.u8();
    room.player_interaction = static_cast<CoopPlayerInteraction>(std::min(interaction, 2u));
    room.knockback_strength = in.u8();
    room.pvp_mode = static_cast<CoopPvpMode>(std::min<unsigned>(in.u8(), 1u));
    room.cheats = in.u8() != 0u;
    room.max_players = std::clamp<unsigned>(in.u8(), 2u, kCoopMaxPlayersLimit);
    room.star_counter = in.u8() != 0u;
}

void write_player(ByteWriter& out, const SessionPlayer& player) {
    out.u64(player.id)
        .u8(static_cast<std::uint8_t>(player.index))
        .str(player.name)
        .u8(static_cast<std::uint8_t>(player.character))
        .u8(player.host ? 1u : 0u)
        .str(player.stage)
        .u8(static_cast<std::uint8_t>(std::clamp(player.scenario, 0, 255)))
        .u16(static_cast<std::uint16_t>(std::min(player.stars, kStarsLimit)));
}

SessionPlayer read_player(ByteReader& in) {
    SessionPlayer player;
    player.id = in.u64();
    player.index = in.u8();
    player.name = in.str(kPlayerNameLimit);
    player.character = in.u8() == 1u ? CoopCharacter::Luigi : CoopCharacter::Mario;
    player.host = in.u8() != 0u;
    player.stage = clean_stage(in.str(256u));
    player.scenario = in.u8();
    player.stars = in.u16();
    return player;
}

// A name as the others see it: printable, cut, never empty.
std::string clean_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (c >= 0x20 && c < 0x7f && out.size() < kPlayerNameLimit) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? std::string("PLAYER") : out;
}

}  // namespace

struct Session::State {
    std::unique_ptr<net::Transport> transport;
    SessionState state = SessionState::Idle;
    bool host = false;
    HostConfig host_config;
    JoinConfig join_config;
    RuntimeCoopSettings room;
    std::vector<SessionPlayer> players;
    // A host's members not let in yet (since when), and refused ones to remove (when).
    std::map<PeerId, Clock::time_point> waiting;
    std::map<PeerId, Clock::time_point> refused;
    Clock::time_point join_started{};
    bool join_asked = false;  // a joiner reached the lobby and asked the host to let it in
    std::vector<SessionEvent> events;
    std::optional<RoomSave> save;
    std::map<std::uint8_t, Handler> handlers;
    // A host's joiners getting the room's mods (since their last request), and the file it last read for them.
    std::map<PeerId, Clock::time_point> downloading;
    std::ifstream serving;
    std::filesystem::path serving_path;

    // A joiner's download of the room's mods: the room's list, the ones to get (by their place in it), the next piece
    // to ask for and the next to arrive (a mod of `needed`, its file, where in it).
    struct Cursor {
        std::size_t mod = 0u;
        std::size_t file = 0u;
        std::uint64_t offset = 0u;
    };
    struct Download {
        std::vector<SharedMod> room;
        std::vector<std::size_t> needed;
        Cursor ask;
        Cursor got;
        unsigned asked = 0u;
        std::ofstream out;
        bool out_open = false;
        std::filesystem::path folder;  // where the mod arriving downloads
        std::uint64_t done = 0u;
        std::uint64_t total = 0u;
    };
    std::optional<Download> download;
    std::optional<std::vector<SharedMod>> room_mods;
    // Where this player plays (set_place), for its entry in a room opened or joined later.
    std::string stage;
    int scenario = 0;

    SessionPlayer* player(PeerId id) {
        const auto it = std::find_if(players.begin(), players.end(), [id](const auto& p) { return p.id == id; });
        return it == players.end() ? nullptr : &*it;
    }

    void event(SessionEvent::Kind kind, std::string name = {}, std::string text = {}) {
        events.push_back({kind, std::move(name), std::move(text)});
    }

    bool send(PeerId to, std::uint8_t type, const ByteWriter& body, Channel channel = Channel::Ordered) {
        ByteWriter out;
        out.u8(type).raw(body.bytes());
        return transport->send_to(to, channel, out.bytes());
    }

    // The room ends for this player: back to idle, `kind` with `why`.
    void end(SessionEvent::Kind kind, std::string why) {
        if (transport->in_lobby() || state != SessionState::Idle) transport->lobby_leave();
        state = SessionState::Idle;
        host = false;
        players.clear();
        waiting.clear();
        refused.clear();
        downloading.clear();
        serving.close();
        serving_path.clear();
        download.reset();
        event(kind, {}, std::move(why));
    }

    void refuse(PeerId peer, std::string why) {
        ByteWriter body;
        body.str(why);
        send(peer, kJoinRefuse, body);
        waiting.erase(peer);
        refused[peer] = Clock::now();
    }

    void host_receive_join(PeerId from, ByteReader& in) {
        if (player(from) != nullptr || refused.count(from) != 0u) return;
        if (downloading.count(from) != 0u) return;
        const std::uint16_t protocol = in.u16();
        const std::string version = in.str(128u);
        const std::string name = clean_name(in.str(256u));
        const auto character = in.u8() == 1u ? CoopCharacter::Luigi : CoopCharacter::Mario;
        if (!in.ok() || protocol != kSessionProtocol) {
            refuse(from, "THE HOST PLAYS ANOTHER VERSION OF ONLINE PLAY");
            return;
        }
        if (version != host_config.game_version) {
            refuse(from, "THE HOST PLAYS ANOTHER VERSION OF NEBULA (" + host_config.game_version + ")");
            return;
        }
        DiscInfo disc;
        disc.game_id = in.str(16u);
        disc.revision = in.u8();
        disc.hash = in.u64();
        const std::optional<std::vector<SharedMod>> mods = read_mods(in, false);
        if (!in.ok() || !mods) {
            refuse(from, "THE HOST COULD NOT READ YOUR GAME'S MODS");
            return;
        }
        if (disc != host_config.disc) {
            refuse(from, disc.game_id != host_config.disc.game_id || disc.revision != host_config.disc.revision
                             ? "THE HOST PLAYS ANOTHER DISC (" + disc_text(host_config.disc) + ")"
                             : "YOUR DISC ISN'T THE SAME AS THE HOST'S " + disc_text(host_config.disc) +
                                   ": DUMP IT AGAIN");
            return;
        }
        if (players.size() >= room.max_players) {
            refuse(from, "THE ROOM IS FULL");
            return;
        }
        if (!same_mods(*mods, host_config.mods)) {
            // Other mods: the room's list, for the joiner to get the ones it lacks and come back with them.
            ByteWriter list;
            write_mods(list, host_config.mods, true);
            send(from, kJoinMods, list);
            waiting.erase(from);
            downloading[from] = Clock::now();
            return;
        }
        // The lowest free number.
        unsigned index = 1u;
        while (std::any_of(players.begin(), players.end(), [index](const auto& p) { return p.index == index; })) {
            ++index;
        }
        SessionPlayer joined{from, index, name, character, false, false, {}, 0, 0u};
        waiting.erase(from);

        RoomSave room_save;
        if (host_config.save) room_save = host_config.save();
        std::size_t total = 0u;
        for (const SaveFile& file : room_save.files) total += file.bytes.size();
        if (total > kMaxSaveBytes || room_save.files.size() > kMaxSaveFiles) room_save.files.clear();

        // Everyone already in hears of the new player, then the new player gets the room.
        send_info(joined);
        players.push_back(joined);
        std::sort(players.begin(), players.end(), [](const auto& a, const auto& b) { return a.index < b.index; });

        ByteWriter accept;
        accept.u8(static_cast<std::uint8_t>(index));
        write_room(accept, room);
        accept.u8(static_cast<std::uint8_t>(room_save.file)).u16(static_cast<std::uint16_t>(room_save.files.size()));
        for (const SaveFile& file : room_save.files) accept.str(file.name).blob(file.bytes);
        accept.u8(static_cast<std::uint8_t>(players.size()));
        for (const SessionPlayer& player : players) write_player(accept, player);
        send(from, kJoinAccept, accept);
        event(SessionEvent::Kind::PlayerJoined, name);
    }

    // A piece of a room mod's file for a joiner getting them.
    void host_send_piece(PeerId from, ByteReader& in) {
        const auto it = downloading.find(from);
        if (it == downloading.end()) return;
        const std::size_t mod = in.u16();
        const std::size_t file = in.u32();
        const std::uint64_t offset = in.u64();
        const std::uint32_t length = std::min(in.u32(), kPieceBytes);
        if (!in.ok() || mod >= host_config.mods.size() || file >= host_config.mods[mod].files.size()) return;
        it->second = Clock::now();
        const SharedMod& shared = host_config.mods[mod];
        const ModFile& entry = shared.files[file];
        std::vector<std::uint8_t> bytes;
        if (offset < entry.size) {
            const std::filesystem::path path = mod_file_path(shared.folder, entry.path);
            if (path != serving_path || !serving.is_open()) {
                serving.close();
                serving.clear();
                serving.open(path, std::ios::binary);
                serving_path = path;
            }
            bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(length, entry.size - offset)));
            serving.clear();
            serving.seekg(static_cast<std::streamoff>(offset));
            serving.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            // A file that can't be read gives nothing, which the joiner takes as the host failing it.
            if (!serving || static_cast<std::size_t>(serving.gcount()) != bytes.size()) bytes.clear();
        }
        ByteWriter piece;
        piece.u16(static_cast<std::uint16_t>(mod)).u32(static_cast<std::uint32_t>(file)).u64(offset).blob(bytes);
        send(from, kModPiece, piece);
    }

    // The room's mods a joiner lacks: their download starts (or, with none to get, the joiner is ready to play them).
    void join_receive_mods(ByteReader& in) {
        if (state != SessionState::Connecting || download) return;
        std::optional<std::vector<SharedMod>> list = read_mods(in, true);
        if (!list) {
            end(SessionEvent::Kind::JoinFailed, "THE HOST'S MODS COULD NOT BE READ");
            return;
        }
        Download next;
        next.room = std::move(*list);
        const ModCache cache(join_config.mods_cache);
        for (std::size_t i = 0; i < next.room.size(); ++i) {
            SharedMod& mod = next.room[i];
            std::optional<std::filesystem::path> have = cache.find(mod);
            if (!have && join_config.find_mod) have = join_config.find_mod(mod);
            if (have) {
                mod.folder = *have;
                continue;
            }
            next.needed.push_back(i);
            next.total += mod.size();
        }
        download = std::move(next);
        if (download->needed.empty()) {
            mods_ready();
            return;
        }
        if (join_config.mods_cache.empty()) {
            end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS HAVE NOWHERE TO DOWNLOAD");
            return;
        }
        std::string ids;
        for (const std::size_t i : download->needed) ids += (ids.empty() ? "" : ", ") + download->room[i].id;
        event(SessionEvent::Kind::DownloadingMods, {}, ids);
        join_started = Clock::now();
        if (!begin_mod()) return;
        settle_download();
        if (download) ask_pieces();
    }

    // The mod arriving next downloads into the cache's folder for it. False (the join failed) when it can't.
    bool begin_mod() {
        const SharedMod& mod = download->room[download->needed[download->got.mod]];
        download->folder = ModCache(join_config.mods_cache).begin(mod);
        if (download->folder.empty()) {
            end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS COULD NOT BE SAVED");
            return false;
        }
        return true;
    }

    // Asks for pieces until enough are on their way.
    void ask_pieces() {
        Download& d = *download;
        while (d.asked < kModPiecesAhead && d.ask.mod < d.needed.size()) {
            const std::size_t index = d.needed[d.ask.mod];
            const SharedMod& mod = d.room[index];
            if (d.ask.file >= mod.files.size()) {
                ++d.ask.mod;
                d.ask.file = 0u;
                d.ask.offset = 0u;
                continue;
            }
            const std::uint64_t size = mod.files[d.ask.file].size;
            if (d.ask.offset >= size) {
                ++d.ask.file;
                d.ask.offset = 0u;
                continue;
            }
            const auto length = static_cast<std::uint32_t>(std::min<std::uint64_t>(kPieceBytes, size - d.ask.offset));
            ByteWriter request;
            request.u16(static_cast<std::uint16_t>(index))
                .u32(static_cast<std::uint32_t>(d.ask.file))
                .u64(d.ask.offset)
                .u32(length);
            send(transport->owner(), kModRequest, request);
            d.ask.offset += length;
            ++d.asked;
        }
    }

    // Past the files the pieces finished (and empty ones, made as they are), and the mods: each checked into the
    // cache as it ends. The last one makes the joiner ready.
    void settle_download() {
        while (download) {
            Download& d = *download;
            const std::size_t index = d.needed[d.got.mod];
            SharedMod& mod = d.room[index];
            if (d.got.file < mod.files.size()) {
                const ModFile& file = mod.files[d.got.file];
                if (d.got.offset < file.size) return;
                if (!d.out_open && !open_piece_file()) return;
                d.out.close();
                d.out_open = false;
                if (!d.out) {
                    end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS COULD NOT BE SAVED");
                    return;
                }
                ++d.got.file;
                d.got.offset = 0u;
                continue;
            }
            const std::optional<std::filesystem::path> folder = ModCache(join_config.mods_cache).finish(mod);
            if (!folder) {
                end(SessionEvent::Kind::JoinFailed, "THE MOD " + mod.id + " DID NOT ARRIVE WHOLE");
                return;
            }
            mod.folder = *folder;
            ++d.got.mod;
            d.got.file = 0u;
            d.got.offset = 0u;
            if (d.got.mod == d.needed.size()) {
                mods_ready();
                return;
            }
            if (!begin_mod()) return;
        }
    }

    // The file the next piece goes into, made (empty) in the mod's download folder.
    bool open_piece_file() {
        Download& d = *download;
        const ModFile& file = d.room[d.needed[d.got.mod]].files[d.got.file];
        const std::filesystem::path path = mod_file_path(d.folder, file.path);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        d.out.clear();
        d.out.open(path, std::ios::binary | std::ios::trunc);
        d.out_open = static_cast<bool>(d.out);
        if (!d.out_open) end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS COULD NOT BE SAVED");
        return d.out_open;
    }

    void join_receive_piece(ByteReader& in) {
        if (state != SessionState::Connecting || !download || download->got.mod >= download->needed.size()) return;
        Download& d = *download;
        const std::size_t mod = in.u16();
        const std::size_t file = in.u32();
        const std::uint64_t offset = in.u64();
        const std::vector<std::uint8_t> bytes = in.blob(kPieceBytes);
        const std::size_t index = d.needed[d.got.mod];
        if (!in.ok() || mod != index || file != d.got.file || offset != d.got.offset) {
            end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS ARRIVED OUT OF ORDER");
            return;
        }
        const ModFile& entry = d.room[index].files[file];
        if (bytes.empty() || bytes.size() > entry.size - offset) {
            end(SessionEvent::Kind::JoinFailed, "THE HOST COULD NOT SEND THE MOD " + d.room[index].id);
            return;
        }
        if (!d.out_open && !open_piece_file()) return;
        d.out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!d.out) {
            end(SessionEvent::Kind::JoinFailed, "THE ROOM'S MODS COULD NOT BE SAVED");
            return;
        }
        d.got.offset += bytes.size();
        d.done += bytes.size();
        if (d.asked > 0u) --d.asked;
        join_started = Clock::now();
        settle_download();
        if (download) ask_pieces();
    }

    // Every room mod is here: the session ends, for Nebula to start again with them.
    void mods_ready() {
        std::vector<SharedMod> mods = std::move(download->room);
        const std::string ids = mods_text(mods);
        download.reset();
        room_mods = std::move(mods);
        end(SessionEvent::Kind::ModsReady, ids);
    }

    void join_receive_accept(ByteReader& in) {
        if (state != SessionState::Connecting) return;
        (void)in.u8();  // this player's number, which the list below has too
        RuntimeCoopSettings got = room;
        read_room(in, got);
        RoomSave room_save;
        room_save.file = std::clamp<unsigned>(in.u8(), 1u, 6u);
        const unsigned files = in.u16();
        for (unsigned i = 0; i < files && in.ok() && i < kMaxSaveFiles; ++i) {
            SaveFile file;
            file.name = in.str(128u);
            file.bytes = in.blob(kMaxSaveBytes);
            room_save.files.push_back(std::move(file));
        }
        const unsigned count = in.u8();
        std::vector<SessionPlayer> list;
        for (unsigned i = 0; i < count && in.ok(); ++i) list.push_back(read_player(in));
        if (!in.ok()) {
            end(SessionEvent::Kind::JoinFailed, "THE HOST'S ANSWER COULD NOT BE READ");
            return;
        }
        for (SessionPlayer& player : list) {
            player.local = player.id == transport->self();
            if (player.local) {
                player.stage = stage;
                player.scenario = scenario;
                player.stars = 0u;
            }
        }
        players = std::move(list);
        room = got;
        save = std::move(room_save);
        state = SessionState::Joined;
        send_place();
        event(SessionEvent::Kind::Joined);
    }

    // This player's place to everyone in the room (a host's members still joining get it with the room).
    void send_place() {
        ByteWriter body;
        body.str(stage).u8(static_cast<std::uint8_t>(std::clamp(scenario, 0, 255)));
        for (const SessionPlayer& other : players) {
            if (!other.local) send(other.id, kPlayerPlace, body);
        }
    }

    void receive_place(PeerId from, ByteReader& in) {
        std::string got = clean_stage(in.str(256u));
        const int got_scenario = in.u8();
        SessionPlayer* sender = player(from);
        if (!in.ok() || sender == nullptr || sender->local) return;
        sender->stage = std::move(got);
        sender->scenario = got_scenario;
    }

    // A host's: a player's star count to everyone in the room, as it changes.
    void send_stars(const SessionPlayer& counted) {
        ByteWriter body;
        body.u64(counted.id).u16(static_cast<std::uint16_t>(std::min(counted.stars, kStarsLimit)));
        for (const SessionPlayer& other : players) {
            if (!other.local) send(other.id, kPlayerStars, body);
        }
    }

    // A joiner's, from the host (only the host counts stars).
    void receive_stars(ByteReader& in) {
        const PeerId id = in.u64();
        const unsigned got = in.u16();
        SessionPlayer* counted = player(id);
        if (!in.ok() || counted == nullptr) return;
        counted->stars = got;
    }

    // A player's info (its name above all) to everyone else in the room: a renamed joiner gets its own back, its name
    // as the host cleaned it.
    void send_info(const SessionPlayer& changed) {
        ByteWriter info;
        write_player(info, changed);
        for (const SessionPlayer& other : players) {
            if (!other.local) send(other.id, kPlayerInfo, info);
        }
    }

    // A host's: a joiner renamed itself.
    void host_receive_name(PeerId from, ByteReader& in) {
        const std::string got = clean_name(in.str(256u));
        SessionPlayer* renamed = player(from);
        if (!in.ok() || renamed == nullptr || renamed->local || renamed->name == got) return;
        renamed->name = got;
        send_info(*renamed);
    }

    void receive(PeerId from, Channel channel, std::span<const std::uint8_t> data) {
        ByteReader in(data);
        const std::uint8_t type = in.u8();
        if (!in.ok()) return;
        if (type == kPlayerPlace) {
            // From any player let in (passed on by the host), once this one is in.
            if (state == SessionState::Hosting || state == SessionState::Joined) receive_place(from, in);
            return;
        }
        if (type >= kFirstFeatureMessage) {
            // Only from players let in, and only once this one is in.
            const SessionPlayer* sender = player(from);
            if (sender == nullptr || state == SessionState::Connecting) return;
            const auto it = handlers.find(type);
            if (it != handlers.end() && it->second) {
                const SessionPlayer copy = *sender;
                it->second(copy, in.rest());
            }
            return;
        }
        (void)channel;
        if (host) {
            if (type == kJoinRequest) host_receive_join(from, in);
            if (type == kModRequest) host_send_piece(from, in);
            if (type == kPlayerName && state == SessionState::Hosting) host_receive_name(from, in);
            return;
        }
        if (from != transport->owner()) return;
        switch (type) {
        case kJoinAccept: join_receive_accept(in); return;
        case kJoinMods: join_receive_mods(in); return;
        case kModPiece: join_receive_piece(in); return;
        case kPlayerStars:
            if (state == SessionState::Joined) receive_stars(in);
            return;
        case kJoinRefuse: {
            const std::string why = in.str(256u);
            end(SessionEvent::Kind::JoinFailed, why.empty() ? std::string("THE HOST REFUSED") : why);
            return;
        }
        case kPlayerInfo: {
            SessionPlayer joined = read_player(in);
            if (!in.ok() || state != SessionState::Joined) return;
            joined.local = joined.id == transport->self();
            if (SessionPlayer* known = player(joined.id)) {
                // Its place comes from the player itself (kPlayerPlace) and its stars as they change
                // (kPlayerStars), never older with its info.
                joined.stage = known->stage;
                joined.scenario = known->scenario;
                joined.stars = known->stars;
                *known = joined;
                return;
            }
            players.push_back(joined);
            std::sort(players.begin(), players.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
            event(SessionEvent::Kind::PlayerJoined, joined.name);
            return;
        }
        case kRoomSettings: {
            RuntimeCoopSettings got = room;
            read_room(in, got);
            if (!in.ok()) return;
            room = got;
            event(SessionEvent::Kind::RoomChanged);
            return;
        }
        default: return;
        }
    }

    void peer_left(PeerId peer, bool intentional) {
        waiting.erase(peer);
        refused.erase(peer);
        downloading.erase(peer);
        const auto it = std::find_if(players.begin(), players.end(), [peer](const auto& p) { return p.id == peer; });
        if (it == players.end()) return;
        const std::string name = it->name;
        players.erase(it);
        event(SessionEvent::Kind::PlayerLeft, name, intentional ? std::string() : std::string("STOPPED ANSWERING"));
    }

    void error(net::NetError error, PeerId) {
        if (error == net::NetError::PeerFailed && transport->in_lobby()) return;  // a member; peer_left follows
        std::string why = net::net_error_text(error);
        for (char& c : why) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        if (state == SessionState::Connecting) {
            end(SessionEvent::Kind::JoinFailed, std::move(why));
        } else if (state != SessionState::Idle) {
            end(SessionEvent::Kind::Closed, std::move(why));
        }
    }

    void tick() {
        if (state == SessionState::Idle) return;
        transport->update();
        if (state == SessionState::Idle) return;
        const Clock::time_point now = Clock::now();
        if (state == SessionState::Connecting && now - join_started > kJoinTimeout) {
            end(SessionEvent::Kind::JoinFailed, "THE HOST DID NOT LET YOU IN");
            return;
        }
        if (!host) return;
        std::vector<PeerId> remove;
        for (const auto& [peer, since] : waiting) {
            if (now - since > kJoinRequestTimeout) remove.push_back(peer);
        }
        for (const auto& [peer, since] : refused) {
            if (now - since > kRefuseLinger) remove.push_back(peer);
        }
        for (const auto& [peer, since] : downloading) {
            if (now - since > kDownloadIdle) remove.push_back(peer);
        }
        for (const PeerId peer : remove) {
            waiting.erase(peer);
            refused.erase(peer);
            downloading.erase(peer);
            transport->unpeer(peer);
        }
    }
};

Session::Session(std::unique_ptr<net::Transport> transport) : state_(std::make_unique<State>()) {
    state_->transport = transport ? std::move(transport) : std::make_unique<net::DirectTransport>();
    net::TransportCallbacks callbacks;
    State* s = state_.get();
    callbacks.on_lobby_joined = [s](PeerId self, PeerId owner, std::uint64_t) {
        if (s->host) {
            s->state = SessionState::Hosting;
            s->players = {SessionPlayer{self, 0u, clean_name(s->host_config.name), s->host_config.character, true,
                                        true, s->stage, s->scenario, 0u}};
            s->event(SessionEvent::Kind::Hosting, {}, std::to_string(s->transport->port()));
            return;
        }
        ByteWriter body;
        body.u16(kSessionProtocol)
            .str(s->join_config.game_version)
            .str(clean_name(s->join_config.name))
            .u8(static_cast<std::uint8_t>(s->join_config.character))
            .str(s->join_config.disc.game_id)
            .u8(static_cast<std::uint8_t>(s->join_config.disc.revision))
            .u64(s->join_config.disc.hash);
        write_mods(body, s->join_config.mods, false);
        s->send(owner, kJoinRequest, body);
        s->join_asked = true;
    };
    callbacks.on_peer_joined = [s](PeerId peer) {
        if (s->host) s->waiting[peer] = Clock::now();
    };
    callbacks.on_peer_left = [s](PeerId peer, bool intentional) { s->peer_left(peer, intentional); };
    callbacks.on_receive = [s](PeerId from, Channel channel, std::span<const std::uint8_t> data) {
        s->receive(from, channel, data);
    };
    callbacks.on_error = [s](net::NetError error, PeerId tag) { s->error(error, tag); };
    state_->transport->set_callbacks(std::move(callbacks));
}

Session::~Session() {
    leave();
    stop_thread();
}

void Session::start_thread() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void Session::stop_thread() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void Session::run() {
    while (running_.load(std::memory_order_relaxed)) {
        bool idle = false;
        {
            const std::lock_guard<std::recursive_mutex> lock(lock_);
            state_->tick();
            idle = state_->state == SessionState::Idle;
        }
        std::this_thread::sleep_for(idle ? kIdleTick : kTick);
    }
}

bool Session::host(HostConfig config) {
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (state_->state != SessionState::Idle) state_->end(SessionEvent::Kind::Closed, {});
        state_->events.clear();
        state_->host = true;
        state_->host_config = std::move(config);
        state_->room = state_->host_config.room;
        state_->room.max_players = std::clamp(state_->room.max_players, 2u, kCoopMaxPlayersLimit);
        net::LobbyConfig lobby;
        lobby.port = state_->host_config.port;
        lobby.game = "Nebula";
        lobby.version = state_->host_config.game_version;
        lobby.host_name = state_->host_config.name;
        lobby.password = state_->host_config.room.password;
        lobby.max_members = state_->room.max_players;
        if (!state_->transport->lobby_create(lobby)) {
            state_->host = false;
            state_->state = SessionState::Idle;
            return false;
        }
    }
    start_thread();
    return true;
}

bool Session::join(JoinConfig config) {
    {
        const std::lock_guard<std::recursive_mutex> lock(lock_);
        if (state_->state != SessionState::Idle) state_->end(SessionEvent::Kind::Closed, {});
        state_->events.clear();
        state_->host = false;
        state_->join_config = std::move(config);
        state_->room = RuntimeCoopSettings{};
        state_->save.reset();
        state_->room_mods.reset();
        state_->state = SessionState::Connecting;
        state_->join_started = Clock::now();
        state_->join_asked = false;
        net::JoinConfig lobby{state_->join_config.address, state_->join_config.port, state_->join_config.password};
        if (!state_->transport->lobby_join(lobby)) {
            // on_error already told why (JoinFailed).
            state_->state = SessionState::Idle;
            return false;
        }
    }
    start_thread();
    return true;
}

void Session::leave() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state == SessionState::Idle) return;
    state_->end(SessionEvent::Kind::Closed, {});
}

SessionState Session::state() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return state_->state;
}

bool Session::is_host() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return state_->host && state_->state == SessionState::Hosting;
}

std::vector<SessionPlayer> Session::players() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    std::vector<SessionPlayer> players = state_->players;
    for (SessionPlayer& player : players) {
        if (!player.local) player.ping_ms = static_cast<unsigned>(state_->transport->ping(player.id).count());
    }
    return players;
}

RuntimeCoopSettings Session::room() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return state_->room;
}

std::uint16_t Session::port() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return state_->transport->in_lobby() && state_->host ? state_->transport->port() : 0u;
}

void Session::set_room(const RuntimeCoopSettings& room) {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Hosting) return;
    const unsigned max_players = state_->room.max_players;  // the lobby's size is set as it opens
    RuntimeCoopSettings next = state_->room;
    next.player_interaction = room.player_interaction;
    next.knockback_strength = room.knockback_strength;
    next.pvp_mode = room.pvp_mode;
    next.cheats = room.cheats;
    next.star_counter = room.star_counter;
    next.max_players = max_players;
    if (next == state_->room) return;
    state_->room = next;
    ByteWriter body;
    write_room(body, next);
    for (const SessionPlayer& player : state_->players) {
        if (!player.local) state_->send(player.id, kRoomSettings, body);
    }
}

void Session::set_place(const std::string& stage, int scenario) {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    const std::string clean = clean_stage(stage);
    scenario = std::clamp(scenario, 0, 255);
    if (clean == state_->stage && scenario == state_->scenario) return;
    state_->stage = clean;
    state_->scenario = scenario;
    if (state_->state != SessionState::Hosting && state_->state != SessionState::Joined) return;
    for (SessionPlayer& player : state_->players) {
        if (player.local) {
            player.stage = clean;
            player.scenario = scenario;
        }
    }
    state_->send_place();
}

void Session::set_name(const std::string& name) {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Hosting && state_->state != SessionState::Joined) return;
    const std::string clean = clean_name(name);
    auto& list = state_->players;
    const auto self = std::find_if(list.begin(), list.end(), [](const auto& p) { return p.local; });
    if (self == list.end() || self->name == clean) return;
    self->name = clean;
    if (state_->state == SessionState::Hosting) {
        state_->send_info(*self);
        return;
    }
    ByteWriter body;
    body.str(clean);
    state_->send(state_->transport->owner(), kPlayerName, body);
}

void Session::credit_star(net::PeerId player) {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Hosting) return;
    SessionPlayer* counted = state_->player(player);
    if (counted == nullptr) return;
    if (counted->stars < kStarsLimit) ++counted->stars;
    state_->send_stars(*counted);
}

unsigned Session::stars() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Hosting && state_->state != SessionState::Joined) return 0u;
    for (const SessionPlayer& player : state_->players) {
        if (player.local) return player.stars;
    }
    return 0u;
}

std::vector<SessionEvent> Session::take_events() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return std::exchange(state_->events, {});
}

std::optional<RoomSave> Session::take_room_save() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return std::exchange(state_->save, std::nullopt);
}

JoinStep Session::join_step() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Connecting) return JoinStep::None;
    if (state_->download && !state_->download->needed.empty()) return JoinStep::DownloadingMods;
    return state_->join_asked ? JoinStep::Asking : JoinStep::Reaching;
}

DownloadProgress Session::download_progress() const {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    DownloadProgress progress;
    const std::optional<State::Download>& download = state_->download;
    if (!download || download->needed.empty()) return progress;
    progress.active = true;
    progress.done = download->done;
    progress.total = download->total;
    const std::size_t at = std::min(download->got.mod, download->needed.size() - 1u);
    progress.mod = download->room[download->needed[at]].id;
    return progress;
}

std::optional<std::vector<SharedMod>> Session::take_room_mods() {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    return std::exchange(state_->room_mods, std::nullopt);
}

bool Session::send(std::uint8_t type, net::Channel channel, std::span<const std::uint8_t> data, net::PeerId to) {
    if (type < kFirstFeatureMessage) return false;
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_->state != SessionState::Hosting && state_->state != SessionState::Joined) return false;
    ByteWriter out;
    out.u8(type).raw(data);
    if (to != net::kEveryone) return state_->transport->send_to(to, channel, out.bytes());
    // Everyone let in (a host's members still joining don't get the room's messages yet).
    if (!state_->host) return state_->transport->send_to(net::kEveryone, channel, out.bytes());
    bool sent = true;
    for (const SessionPlayer& player : state_->players) {
        if (!player.local) sent = state_->transport->send_to(player.id, channel, out.bytes()) && sent;
    }
    return sent;
}

void Session::on_message(std::uint8_t type, Handler handler) {
    const std::lock_guard<std::recursive_mutex> lock(lock_);
    state_->handlers[type] = std::move(handler);
}

}  // namespace galaxy::coop
