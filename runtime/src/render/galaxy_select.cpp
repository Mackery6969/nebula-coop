// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/galaxy_select.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

namespace galaxy::render {
namespace {

std::atomic<std::uint64_t> g_travel{0u};  // stage + 1 in the low 32 bits, star in the high
std::atomic<bool> g_grand_stars{false};
std::atomic<bool> g_leave_game{false};
std::atomic<bool> g_coop_in_game{false};
std::mutex g_room_mutex;
CoopRoomStatus g_room;
std::atomic<bool> g_text_entry{false};
std::mutex g_text_mutex;
std::vector<char32_t> g_text;  // typed, not taken yet
std::atomic<bool> g_chat_available{false};
std::atomic<bool> g_chat_open{false};
// The key that opened the chat, as a character: the window may also send it as typed, which the line doesn't take.
char32_t g_chat_swallow = 0u;
std::chrono::steady_clock::time_point g_chat_opened{};
std::atomic<bool> g_names_wanted{false};
std::atomic<bool> g_names_known{false};
std::mutex g_names_mutex;
std::vector<std::string> g_galaxy_names;
std::vector<std::vector<std::string>> g_star_names;

}  // namespace

void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept {
    if (stage >= kGalaxyStageCount || star == 0u || star > kGalaxyStages[stage].stars) {
        return;
    }
    g_travel.store((static_cast<std::uint64_t>(star) << 32u) | (stage + 1u), std::memory_order_release);
}

void request_grand_stars() noexcept {
    g_grand_stars.store(true, std::memory_order_release);
}

bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept {
    const std::uint64_t travel = g_travel.exchange(0u, std::memory_order_acq_rel);
    if (travel == 0u) {
        return false;
    }
    stage = static_cast<std::size_t>((travel & 0xFFFFFFFFu) - 1u);
    star = static_cast<std::uint32_t>(travel >> 32u);
    return stage < kGalaxyStageCount;
}

bool take_grand_stars() noexcept {
    return g_grand_stars.exchange(false, std::memory_order_acq_rel);
}

void request_leave_game() noexcept {
    g_leave_game.store(true, std::memory_order_release);
}

void set_coop_room_status(const CoopRoomStatus& status) {
    const std::lock_guard<std::mutex> lock(g_room_mutex);
    g_room = status;
}

CoopRoomStatus coop_room_status() {
    const std::lock_guard<std::mutex> lock(g_room_mutex);
    return g_room;
}

void set_coop_in_game(bool in_game) noexcept {
    g_coop_in_game.store(in_game, std::memory_order_release);
}

bool coop_in_game() noexcept {
    return g_coop_in_game.load(std::memory_order_acquire);
}

void set_coop_text_entry(bool on) noexcept {
    if (g_text_entry.exchange(on, std::memory_order_acq_rel) && !on) {
        const std::lock_guard<std::mutex> lock(g_text_mutex);
        g_text.clear();
    }
}

bool coop_text_entry() noexcept {
    return g_text_entry.load(std::memory_order_acquire);
}

void push_coop_text(char32_t key) {
    if (!coop_text_entry()) return;
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    if (const char32_t swallow = std::exchange(g_chat_swallow, 0u); swallow != 0u) {
        // Its character comes right after the key, or not at all (the SDL host starts its typing a frame later).
        const bool fresh = std::chrono::steady_clock::now() - g_chat_opened < std::chrono::milliseconds(250);
        if (fresh && (key == swallow || key == swallow - 0x20u)) return;
    }
    if (g_text.size() < 256u) g_text.push_back(key);
}

std::vector<char32_t> take_coop_text_input() {
    const std::lock_guard<std::mutex> lock(g_text_mutex);
    return std::exchange(g_text, {});
}

void set_coop_chat_available(bool available) noexcept {
    g_chat_available.store(available, std::memory_order_release);
}

bool coop_chat_key(std::uint32_t virtual_key) noexcept {
    if (virtual_key != 'T' || !g_chat_available.load(std::memory_order_acquire) || coop_text_entry()) return false;
    {
        const std::lock_guard<std::mutex> lock(g_text_mutex);
        g_text.clear();
        g_chat_swallow = U't';
        g_chat_opened = std::chrono::steady_clock::now();
    }
    set_coop_text_entry(true);
    g_chat_open.store(true, std::memory_order_release);
    return true;
}

bool take_coop_chat_open() noexcept {
    return g_chat_open.exchange(false, std::memory_order_acq_rel);
}

bool take_leave_game() noexcept {
    return g_leave_game.exchange(false, std::memory_order_acq_rel);
}

void want_galaxy_select_names() noexcept {
    if (!g_names_known.load(std::memory_order_acquire)) {
        g_names_wanted.store(true, std::memory_order_release);
    }
}

bool take_galaxy_select_names_wanted() noexcept {
    return g_names_wanted.exchange(false, std::memory_order_acq_rel) && !g_names_known.load(std::memory_order_acquire);
}

void set_galaxy_select_name(std::size_t stage, std::uint32_t star, std::string name) {
    if (stage >= kGalaxyStageCount || star > kGalaxyStages[stage].stars) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    g_galaxy_names.resize(kGalaxyStageCount);
    g_star_names.resize(kGalaxyStageCount);
    if (star == 0u) {
        g_galaxy_names[stage] = std::move(name);
    } else {
        g_star_names[stage].resize(kGalaxyStages[stage].stars);
        g_star_names[stage][star - 1u] = std::move(name);
    }
    g_names_known.store(true, std::memory_order_release);
}

std::string galaxy_select_name(std::size_t stage) {
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    return stage < g_galaxy_names.size() ? g_galaxy_names[stage] : std::string();
}

std::string galaxy_select_star_name(std::size_t stage, std::uint32_t star) {
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    if (stage >= g_star_names.size() || star == 0u || star > g_star_names[stage].size()) {
        return {};
    }
    return g_star_names[stage][star - 1u];
}

}  // namespace galaxy::render
