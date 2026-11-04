// SPDX-License-Identifier: GPL-3.0-only
// Which of the games started from one data folder this one is. The first plays as always. Each further one (a player
// hosting and joining on one computer to test co-op, as sm64coopdx lets them) is game 2, 3, ... with a folder of its
// own, <data>/instances/<n>, for its settings, graphics settings, logs and saves, started from copies of the first's
// (native/host/host_main.cpp, claim_game_instance). The shader cache and the installed mods stay shared.
#pragma once

#include <filesystem>
#include <utility>

namespace galaxy {

namespace detail {
struct GameInstance {
    unsigned number = 1u;
    std::filesystem::path folder;
};
inline GameInstance& game_instance_state() noexcept {
    static GameInstance state;
    return state;
}
}  // namespace detail

// Set once, at the start of main, before any other thread reads it.
inline void set_game_instance(unsigned number, std::filesystem::path folder) {
    detail::game_instance_state() = {number, std::move(folder)};
}

// 1 for the first game (or the only one).
[[nodiscard]] inline unsigned game_instance() noexcept {
    return detail::game_instance_state().number;
}

// The folder a game after the first keeps its own files in; empty for the first.
[[nodiscard]] inline const std::filesystem::path& game_instance_folder() noexcept {
    return detail::game_instance_state().folder;
}

}  // namespace galaxy
