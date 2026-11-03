#include "galaxy/coop_saves.h"

#include "galaxy/atomic_file.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace galaxy::coop {

namespace {

namespace fs = std::filesystem;

constexpr const char* kInfoFile = "save.ini";
// Written once the old saves were made into co-op saves (SaveStore::convert).
constexpr const char* kConvertedFile = ".converted";
// The save the next start plays (SaveStore::set_next_start).
constexpr const char* kNextStartFile = ".next_start";
constexpr std::size_t kNameLimit = 32u;

// A save's id: "save" and a number, nothing that leaves the folder.
bool valid_id(std::string_view id) {
    if (id.size() < 5u || id.size() > 16u || id.substr(0, 4) != "save") return false;
    return std::all_of(id.begin() + 4, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string clean_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (c >= 0x20 && c < 0x7f && out.size() < kNameLimit) out += c;
    }
    return out;
}

std::optional<SaveInfo> read_info(const fs::path& folder, std::string id) {
    std::ifstream in(folder / kInfoFile);
    if (!in) return std::nullopt;
    SaveInfo info;
    info.id = std::move(id);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1u);
        if (key == "name") {
            info.name = clean_name(value);
        } else if (key == "file") {
            const unsigned long file = std::strtoul(value.c_str(), nullptr, 10);
            info.file = file >= 1u && file <= 6u ? static_cast<unsigned>(file) : 1u;
        } else if (key == "last_played") {
            info.last_played = std::strtoll(value.c_str(), nullptr, 10);
        }
    }
    if (info.name.empty()) info.name = "SAVE";
    return info;
}

}  // namespace

SaveStore::SaveStore(std::filesystem::path root) : root_(std::move(root)) {}

std::vector<SaveInfo> SaveStore::list() const {
    std::vector<SaveInfo> saves;
    std::error_code error;
    for (fs::directory_iterator it(root_, error), end; !error && it != end; it.increment(error)) {
        const std::string id = it->path().filename().string();
        if (!valid_id(id) || !it->is_directory(error)) continue;
        if (std::optional<SaveInfo> info = read_info(it->path(), id)) saves.push_back(std::move(*info));
    }
    const auto number = [](const SaveInfo& save) { return std::strtoul(save.id.c_str() + 4, nullptr, 10); };
    std::sort(saves.begin(), saves.end(), [&number](const SaveInfo& a, const SaveInfo& b) {
        if (a.last_played != b.last_played) return a.last_played > b.last_played;
        return number(a) > number(b);
    });
    return saves;
}

std::optional<SaveInfo> SaveStore::find(std::string_view id) const {
    if (!valid_id(id)) return std::nullopt;
    return read_info(folder(id), std::string(id));
}

std::filesystem::path SaveStore::folder(std::string_view id) const {
    return root_ / std::string(id);
}

bool SaveStore::write_info(const SaveInfo& info) const {
    std::ostringstream out;
    out << "name=" << info.name << '\n' << "file=" << info.file << '\n' << "last_played=" << info.last_played << '\n';
    return io::write_text_file_atomically(folder(info.id) / kInfoFile, out.str());
}

std::string SaveStore::create(std::string_view name, unsigned file) {
    std::error_code error;
    fs::create_directories(root_, error);
    // The next number no folder has: an erased save's isn't given again while a later one remains.
    unsigned long next = 1u;
    for (fs::directory_iterator it(root_, error), end; !error && it != end; it.increment(error)) {
        const std::string id = it->path().filename().string();
        if (valid_id(id)) next = std::max(next, std::strtoul(id.c_str() + 4, nullptr, 10) + 1u);
    }
    SaveInfo info;
    info.id = "save" + std::to_string(next);
    info.name = clean_name(name);
    if (info.name.empty()) info.name = "SAVE " + std::to_string(next);
    info.file = file >= 1u && file <= 6u ? file : 1u;
    if (!fs::create_directory(folder(info.id), error) || !write_info(info)) {
        fs::remove_all(folder(info.id), error);
        return {};
    }
    return info.id;
}

bool SaveStore::erase(std::string_view id) {
    if (!valid_id(id)) return false;
    std::error_code error;
    return fs::remove_all(folder(id), error) != static_cast<std::uintmax_t>(-1) && !error;
}

bool SaveStore::mark_played(std::string_view id, std::int64_t now) {
    std::optional<SaveInfo> info = find(id);
    if (!info) return false;
    info->last_played = now;
    return write_info(*info);
}

bool SaveStore::rename(std::string_view id, std::string_view name) {
    std::optional<SaveInfo> info = find(id);
    const std::string cleaned = clean_name(name);
    if (!info || cleaned.find_first_not_of(' ') == std::string::npos) return false;
    info->name = cleaned.substr(cleaned.find_first_not_of(' '));
    info->name.erase(info->name.find_last_not_of(' ') + 1u);
    return write_info(*info);
}

bool SaveStore::set_next_start(std::string_view id) {
    if (!valid_id(id)) return false;
    return io::write_text_file_atomically(root_ / kNextStartFile, std::string(id) + "\n");
}

std::string SaveStore::take_next_start() {
    std::error_code error;
    std::string id;
    {
        std::ifstream in(root_ / kNextStartFile);
        if (!in) return {};
        std::getline(in, id);
    }
    fs::remove(root_ / kNextStartFile, error);
    if (!id.empty() && id.back() == '\r') id.pop_back();
    return find(id) ? id : std::string();
}

bool SaveStore::has_next_start() const {
    std::error_code error;
    return fs::exists(root_ / kNextStartFile, error);
}

bool SaveStore::convert(const std::filesystem::path& nand_data, unsigned files,
                        const std::filesystem::path& achievements) {
    if (converted()) return false;
    std::error_code error;
    fs::create_directories(root_, error);
    // Not half made: on a failure the saves made so far go, and the next start tries again from nothing.
    std::vector<std::string> made;
    const auto fail = [this, &made] {
        for (const std::string& id : made) erase(id);
        return false;
    };
    for (unsigned file = 1u; file <= 6u; ++file) {
        if ((files & (1u << (file - 1u))) == 0u) continue;
        const std::string id = create("FILE " + std::to_string(file), file);
        if (id.empty()) return fail();
        made.push_back(id);
        fs::copy(nand_data, folder(id), fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
        if (error) return fail();
        if (!achievements.empty() && fs::is_regular_file(achievements, error)) {
            fs::copy_file(achievements, folder(id) / "achievements.ini", fs::copy_options::overwrite_existing, error);
        }
    }
    return io::write_text_file_atomically(root_ / kConvertedFile, "1\n");
}

bool SaveStore::converted() const {
    std::error_code error;
    return fs::exists(root_ / kConvertedFile, error);
}

}  // namespace galaxy::coop
