// Online co-op's saves (docs/COOP_PLAN.md, "Main menu"): as many as the player wants, each a folder under coop_saves/
// holding its own copy of the game's save data (the NAND's title/.../data, which the host points there while it
// plays), its achievements (achievements.ini) and save.ini (its name, the user file it plays, when it was last played).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::coop {

struct SaveInfo {
    std::string id;    // its folder's name
    std::string name;  // shown in the menu
    unsigned file = 1u;  // the game's user file it plays, 1 to 6 (a save made from an old file keeps its number)
    std::int64_t last_played = 0;  // seconds since 1970; 0: never
};

class SaveStore {
public:
    // `root`: the coop_saves folder (made when the first save is).
    explicit SaveStore(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    // The saves, the last played first, then the newest.
    [[nodiscard]] std::vector<SaveInfo> list() const;
    [[nodiscard]] std::optional<SaveInfo> find(std::string_view id) const;
    [[nodiscard]] std::filesystem::path folder(std::string_view id) const;

    // A new save's id (an empty folder but its save.ini), named "SAVE <n>" when `name` is empty; empty on failure.
    [[nodiscard]] std::string create(std::string_view name = {}, unsigned file = 1u);
    // The save's folder and all it holds.
    bool erase(std::string_view id);
    bool mark_played(std::string_view id, std::int64_t now);
    // The save's name (cut to printable ASCII, 32 bytes at most); false when it is empty then or can't be written.
    bool rename(std::string_view id, std::string_view name);

    // A save to play as the next start reaches the main menu (Host restarting Nebula for the room's mods): kept in
    // the folder until taken once.
    bool set_next_start(std::string_view id);
    [[nodiscard]] std::string take_next_start();
    [[nodiscard]] bool has_next_start() const;

    // Once per install: one save per user file the old saves have (bit i of `files`: file i + 1), each a copy of
    // `nand_data` and of `achievements` (the old achievements.ini, shared by every file then) when it exists. True
    // when it ran now.
    bool convert(const std::filesystem::path& nand_data, unsigned files, const std::filesystem::path& achievements);
    [[nodiscard]] bool converted() const;

private:
    bool write_info(const SaveInfo& info) const;

    std::filesystem::path root_;
};

}  // namespace galaxy::coop
