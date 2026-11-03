#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

// Nebula's achievements. The game checks them against its save data (NebulaAchievements) and asks the host to unlock
// them. Online co-op keeps them with the save (set_file): one unlocked in a save is every player's who plays it.
namespace galaxy::achievements {

enum class Group { Story, Challenge, Extra };

struct Info {
    const char* id;
    Group group;
    const char* name;  // English, for the host's log
};

// The catalogue, grouped: story, challenges, extras.
[[nodiscard]] std::span<const Info> list() noexcept;
[[nodiscard]] const Info* find(std::string_view id) noexcept;
// True when `id` is in the catalogue and was not unlocked yet; it is then unlocked, and written to the file in the
// background (several unlocked together, once).
bool unlock(std::string_view id);
// The same, unlocked at `when` (seconds since 1970, UTC) rather than now: another player's unlock in the co-op save,
// on the day it was unlocked there.
bool unlock_at(std::string_view id, std::int64_t when);
[[nodiscard]] bool unlocked(std::string_view id);
// When it was unlocked, in seconds since 1970 (UTC); 0 when locked or unlocked at a time that cannot be read.
[[nodiscard]] std::int64_t unlocked_time(std::string_view id);
// The day it was unlocked, in the player's time zone: year * 10000 + month * 100 + day; 0 when locked; 1 when
// unlocked on a day that cannot be read.
[[nodiscard]] int unlocked_date(std::string_view id);

// Writes what is not written yet, now (before the process ends).
void flush();

// Writes what is waiting, then sets the file (empty: achievements.ini in the settings folder) and drops the cache: the
// played co-op save's own.
void set_file(const std::filesystem::path& path);

// Tests: writes what is waiting, then sets the file (empty: achievements.ini in the settings folder) and drops the
// cache.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::achievements
