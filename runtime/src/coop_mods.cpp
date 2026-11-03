#include "galaxy/coop_mods.h"

#include "galaxy/atomic_file.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>

namespace galaxy::coop {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr std::uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr std::uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr std::uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

std::uint64_t read64(const std::uint8_t* p) {
    std::uint64_t v = 0u;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

std::uint32_t read32(const std::uint8_t* p) {
    std::uint32_t v = 0u;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}

std::uint64_t round(std::uint64_t acc, std::uint64_t lane) {
    return std::rotl(acc + lane * kPrime2, 31) * kPrime1;
}

std::string utf8_of(const fs::path& path) {
    const std::u8string u = path.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path path_of(std::string_view utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// The system's notes beside files, which differ between copies of the same mod.
bool system_file(std::string_view name) {
    if (name.starts_with('.')) return true;
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return lower == "thumbs.db" || lower == "desktop.ini";
}

std::string hex(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// hash_mod_files' memory: a folder's hash while its files (paths, sizes, times) stay the same.
struct HashMemo {
    std::uint64_t files = 0u;
    std::uint64_t hash = 0u;
};
std::mutex s_memo_lock;
std::map<std::string, HashMemo> s_memo;

}  // namespace

ContentHash::ContentHash(std::uint64_t seed)
    : v_{seed + kPrime1 + kPrime2, seed + kPrime2, seed, seed - kPrime1}, buffer_{}, seed_(seed) {}

void ContentHash::update(std::span<const std::uint8_t> data) {
    const std::uint8_t* p = data.data();
    std::size_t n = data.size();
    total_ += n;
    if (buffered_ != 0u) {
        const std::size_t take = std::min(n, sizeof(buffer_) - buffered_);
        std::memcpy(buffer_ + buffered_, p, take);
        buffered_ += take;
        p += take;
        n -= take;
        if (buffered_ < sizeof(buffer_)) return;
        for (int i = 0; i < 4; ++i) v_[i] = round(v_[i], read64(buffer_ + 8 * i));
        buffered_ = 0u;
    }
    for (; n >= 32u; p += 32u, n -= 32u) {
        for (int i = 0; i < 4; ++i) v_[i] = round(v_[i], read64(p + 8 * i));
    }
    if (n != 0u) {
        std::memcpy(buffer_, p, n);
        buffered_ = n;
    }
}

void ContentHash::update(std::string_view text) {
    update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

void ContentHash::update_u64(std::uint64_t value) {
    std::uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
    update(bytes);
}

std::uint64_t ContentHash::finish() const {
    std::uint64_t acc = 0u;
    if (total_ >= 32u) {
        acc = std::rotl(v_[0], 1) + std::rotl(v_[1], 7) + std::rotl(v_[2], 12) + std::rotl(v_[3], 18);
        for (const std::uint64_t v : v_) acc = (acc ^ round(0u, v)) * kPrime1 + kPrime4;
    } else {
        acc = seed_ + kPrime5;
    }
    acc += total_;
    const std::uint8_t* p = buffer_;
    const std::uint8_t* const end = buffer_ + buffered_;
    for (; p + 8u <= end; p += 8u) acc = std::rotl(acc ^ round(0u, read64(p)), 27) * kPrime1 + kPrime4;
    if (p + 4u <= end) {
        acc = std::rotl(acc ^ (static_cast<std::uint64_t>(read32(p)) * kPrime1), 23) * kPrime2 + kPrime3;
        p += 4u;
    }
    for (; p < end; ++p) acc = std::rotl(acc ^ (static_cast<std::uint64_t>(*p) * kPrime5), 11) * kPrime1;
    acc ^= acc >> 33;
    acc *= kPrime2;
    acc ^= acc >> 29;
    acc *= kPrime3;
    acc ^= acc >> 32;
    return acc;
}

std::string disc_text(const DiscInfo& disc) {
    return (disc.game_id.empty() ? std::string("UNKNOWN") : disc.game_id) + " REVISION " +
           std::to_string(disc.revision);
}

DiscInfo describe_disc(std::span<const std::uint8_t> boot, std::span<const std::uint8_t> dol,
                       std::span<const std::uint8_t> fst) {
    DiscInfo disc;
    for (std::size_t i = 0; i < 6u && i < boot.size(); ++i) {
        const char c = static_cast<char>(boot[i]);
        disc.game_id += c >= 0x20 && c < 0x7f ? c : '?';
    }
    disc.revision = boot.size() > 7u ? boot[7] : 0u;
    ContentHash hash;
    hash.update_u64(dol.size());
    hash.update(dol);
    hash.update_u64(fst.size());
    hash.update(fst);
    disc.hash = hash.finish();
    return disc;
}

std::uint64_t SharedMod::size() const {
    std::uint64_t total = 0u;
    for (const ModFile& file : files) total += file.size;
    return total;
}

bool SharedMod::plays_like(const SharedMod& other) const {
    return id == other.id && hash == other.hash && own_text == other.own_text && options == other.options;
}

bool same_mods(std::span<const SharedMod> a, std::span<const SharedMod> b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) { return x.plays_like(y); });
}

std::string mods_text(std::span<const SharedMod> mods) {
    std::string out;
    for (const SharedMod& mod : mods) out += (out.empty() ? "" : ", ") + mod.id;
    return out.empty() ? std::string("NONE") : out;
}

bool safe_mod_path(std::string_view path) {
    if (path.empty() || path.size() > kMaxModPath) return false;
    std::size_t start = 0u;
    while (start <= path.size()) {
        const std::size_t slash = std::min(path.find('/', start), path.size());
        const std::string_view name = path.substr(start, slash - start);
        if (name.empty() || name == "." || name == "..") return false;
        for (const char c : name) {
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
        }
        start = slash + 1u;
    }
    return true;
}

std::optional<std::vector<ModFile>> list_mod_files(const fs::path& folder) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) return std::nullopt;
    std::vector<ModFile> files;
    std::uint64_t total = 0u;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    if (error) return std::nullopt;
    for (; it != end; it.increment(error)) {
        if (error) return std::nullopt;
        const fs::path name = it->path().filename();
        if (system_file(utf8_of(name))) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error)) continue;
        ModFile file;
        file.path = utf8_of(it->path().lexically_relative(folder));
        file.size = it->file_size(error);
        if (error || !safe_mod_path(file.path)) continue;
        total += file.size;
        files.push_back(std::move(file));
        if (files.size() > kMaxModFiles || total > kMaxModBytes) return std::nullopt;
    }
    std::sort(files.begin(), files.end(), [](const ModFile& a, const ModFile& b) { return a.path < b.path; });
    return files;
}

std::optional<std::uint64_t> hash_mod_files(const fs::path& folder, std::span<const ModFile> files) {
    // What the memory is kept by: the files as listed and when they last changed.
    ContentHash state;
    std::error_code error;
    for (const ModFile& file : files) {
        state.update(file.path);
        state.update_u64(file.size);
        const auto time = fs::last_write_time(folder / path_of(file.path), error);
        if (error) return std::nullopt;
        state.update_u64(static_cast<std::uint64_t>(time.time_since_epoch().count()));
    }
    const std::string key = utf8_of(fs::absolute(folder, error).lexically_normal());
    const std::uint64_t files_state = state.finish();
    {
        const std::lock_guard<std::mutex> lock(s_memo_lock);
        const auto it = s_memo.find(key);
        if (it != s_memo.end() && it->second.files == files_state) return it->second.hash;
    }
    ContentHash hash;
    std::vector<std::uint8_t> block(1u << 20);
    for (const ModFile& file : files) {
        hash.update(file.path);
        hash.update_u64(file.size);
        std::ifstream in(folder / path_of(file.path), std::ios::binary);
        if (!in) return std::nullopt;
        std::uint64_t left = file.size;
        while (left != 0u) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(left, block.size()));
            in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(want));
            if (static_cast<std::size_t>(in.gcount()) != want) return std::nullopt;
            hash.update(std::span<const std::uint8_t>(block.data(), want));
            left -= want;
        }
    }
    const std::uint64_t result = hash.finish();
    const std::lock_guard<std::mutex> lock(s_memo_lock);
    s_memo[key] = {files_state, result};
    return result;
}

bool describe_mod(SharedMod& mod) {
    std::optional<std::vector<ModFile>> files = list_mod_files(mod.folder);
    if (!files) return false;
    const std::optional<std::uint64_t> hash = hash_mod_files(mod.folder, *files);
    if (!hash) return false;
    mod.files = std::move(*files);
    mod.hash = *hash;
    return true;
}

fs::path mod_file_path(const fs::path& folder, std::string_view path) {
    return folder / path_of(path);
}

void write_mods(net::ByteWriter& out, std::span<const SharedMod> mods, bool files) {
    out.u16(static_cast<std::uint16_t>(std::min(mods.size(), kMaxRoomMods)));
    for (std::size_t m = 0; m < mods.size() && m < kMaxRoomMods; ++m) {
        const SharedMod& mod = mods[m];
        out.str(mod.id).u64(mod.hash).u8(mod.own_text ? 1u : 0u).u16(static_cast<std::uint16_t>(mod.options.size()));
        for (const auto& [key, choice] : mod.options) out.str(key).u16(static_cast<std::uint16_t>(choice));
        if (!files) continue;
        out.u32(static_cast<std::uint32_t>(mod.files.size()));
        for (const ModFile& file : mod.files) out.str(file.path).u64(file.size);
    }
}

std::optional<std::vector<SharedMod>> read_mods(net::ByteReader& in, bool files) {
    const unsigned count = in.u16();
    if (count > kMaxRoomMods) return std::nullopt;
    std::vector<SharedMod> mods;
    std::uint64_t total = 0u;
    for (unsigned m = 0; m < count && in.ok(); ++m) {
        SharedMod mod;
        mod.id = in.str(256u);
        mod.hash = in.u64();
        mod.own_text = in.u8() != 0u;
        const unsigned options = in.u16();
        for (unsigned o = 0; o < options && in.ok(); ++o) {
            std::string key = in.str(1024u);
            const unsigned choice = in.u16();
            mod.options.emplace_back(std::move(key), choice);
        }
        // A mod's id is a folder's name here too.
        if (!safe_mod_path(mod.id) || mod.id.find('/') != std::string::npos) return std::nullopt;
        if (files) {
            const std::uint32_t file_count = in.u32();
            if (file_count > kMaxModFiles) return std::nullopt;
            std::uint64_t size = 0u;
            for (std::uint32_t f = 0; f < file_count && in.ok(); ++f) {
                ModFile file;
                file.path = in.str(kMaxModPath + 1u);
                file.size = in.u64();
                if (!safe_mod_path(file.path)) return std::nullopt;
                size += file.size;
                mod.files.push_back(std::move(file));
            }
            if (size > kMaxModBytes) return std::nullopt;
            total += size;
        }
        mods.push_back(std::move(mod));
    }
    if (!in.ok() || total > kMaxRoomModBytes) return std::nullopt;
    return mods;
}

fs::path ModCache::folder(const SharedMod& mod) const {
    return root_ / hex(mod.hash);
}

fs::path ModCache::part(const SharedMod& mod) const {
    return root_ / (".part-" + hex(mod.hash));
}

std::optional<fs::path> ModCache::find(const SharedMod& mod) const {
    if (root_.empty()) return std::nullopt;
    const fs::path at = folder(mod);
    const std::optional<std::vector<ModFile>> files = list_mod_files(at);
    if (!files || *files != mod.files) return std::nullopt;
    const std::optional<std::uint64_t> hash = hash_mod_files(at, *files);
    if (!hash || *hash != mod.hash) return std::nullopt;
    return at;
}

fs::path ModCache::begin(const SharedMod& mod) const {
    if (root_.empty()) return {};
    const fs::path at = part(mod);
    std::error_code error;
    fs::remove_all(at, error);
    fs::create_directories(at, error);
    return error ? fs::path{} : at;
}

std::optional<fs::path> ModCache::finish(const SharedMod& mod) const {
    const fs::path from = part(mod);
    const fs::path to = folder(mod);
    std::error_code error;
    const std::optional<std::vector<ModFile>> files = list_mod_files(from);
    const std::optional<std::uint64_t> hash = files ? hash_mod_files(from, *files) : std::nullopt;
    if (!files || *files != mod.files || !hash || *hash != mod.hash) {
        fs::remove_all(from, error);
        return std::nullopt;
    }
    fs::remove_all(to, error);
    fs::rename(from, to, error);
    if (error) {
        fs::remove_all(from, error);
        return std::nullopt;
    }
    return to;
}

bool write_next_join(const fs::path& file, const NextJoin& join) {
    net::ByteWriter out;
    out.u32(0x4e4a4f4eu).str(join.address);
    write_mods(out, join.mods, false);
    for (const SharedMod& mod : join.mods) out.str(utf8_of(mod.folder));
    const std::vector<std::uint8_t>& bytes = out.bytes();
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    return io::write_file_atomically(
        file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::optional<NextJoin> take_next_join(const fs::path& file) {
    std::error_code error;
    if (!fs::is_regular_file(file, error)) return std::nullopt;
    std::vector<std::uint8_t> bytes;
    {
        std::ifstream in(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    fs::remove(file, error);
    net::ByteReader in(bytes);
    if (in.u32() != 0x4e4a4f4eu) return std::nullopt;
    NextJoin join;
    join.address = in.str(512u);
    std::optional<std::vector<SharedMod>> mods = read_mods(in, false);
    if (!mods) return std::nullopt;
    for (SharedMod& mod : *mods) mod.folder = path_of(in.str());
    if (!in.ok()) return std::nullopt;
    join.mods = std::move(*mods);
    return join;
}

}  // namespace galaxy::coop
