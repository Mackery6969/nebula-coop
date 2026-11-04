#include "galaxy/coop_popups.h"

#include <algorithm>
#include <utility>

namespace galaxy::coop {

namespace {

// Two places are the same one: the same galaxy and star, or both in the Observatory (whatever its scenario).
bool same_place(const std::string& stage_a, int scenario_a, const std::string& stage_b, int scenario_b) {
    if (stage_a.empty() || stage_b.empty()) return false;
    if (observatory_stage(stage_a) && observatory_stage(stage_b)) return true;
    return stage_a == stage_b && scenario_a == scenario_b;
}

}  // namespace

bool observatory_stage(std::string_view stage) {
    return stage == "AstroGalaxy" || stage == "AstroDome" || stage == "LibraryRoom";
}

bool popup_wanted(Popup::Kind kind, const RuntimeCoopPopups& options) {
    switch (kind) {
    case Popup::Kind::Joined: return options.joins;
    case Popup::Kind::Left: return options.leaves;
    case Popup::Kind::Observatory: return options.observatory;
    case Popup::Kind::Galaxy:
    case Popup::Kind::EnteredHere:
    case Popup::Kind::LeftHere: return options.galaxies;
    case Popup::Kind::Died: return options.deaths;
    case Popup::Kind::Star: return options.stars;
    }
    return false;
}

void Popups::set_options(const RuntimeCoopPopups& options) {
    const std::lock_guard<std::mutex> lock(lock_);
    options_ = options;
}

void Popups::update(const std::vector<SessionPlayer>& players, std::uint64_t now_ms) {
    const std::lock_guard<std::mutex> lock(lock_);
    if (players.empty()) {
        in_room_ = false;
        known_.clear();
        dead_.clear();
        return;
    }
    // The room as this player found it: everyone already there, quietly.
    const bool fresh = !in_room_;
    in_room_ = true;

    std::string my_stage;
    int my_scenario = 0;
    for (const SessionPlayer& player : players) {
        if (!player.local) continue;
        my_stage = player.stage;
        my_scenario = player.scenario;
    }
    const auto popup = [&](Popup::Kind kind, const SessionPlayer& player) {
        Popup made;
        made.kind = kind;
        made.name = player.name;
        made.character = player.character;
        made.stage = player.stage;
        made.scenario = player.scenario;
        made.at_ms = now_ms;
        add(std::move(made));
    };

    // Who left: known before, gone now.
    for (auto it = known_.begin(); it != known_.end();) {
        const bool here = std::any_of(players.begin(), players.end(),
                                      [&](const SessionPlayer& player) { return player.id == it->first; });
        if (here) {
            ++it;
            continue;
        }
        SessionPlayer gone;
        gone.name = it->second.name;
        gone.character = it->second.character;
        popup(Popup::Kind::Left, gone);
        dead_.erase(it->first);
        it = known_.erase(it);
    }

    for (const SessionPlayer& player : players) {
        if (player.local) continue;
        const auto found = known_.find(player.id);
        if (found == known_.end()) {
            if (!fresh) popup(Popup::Kind::Joined, player);
            // Where they are as they come in: a player who just joined has no place yet, so its first one shows.
            known_[player.id] = {player.name, player.character, player.stage, player.scenario, player.stars};
            continue;
        }
        Known& known = found->second;
        known.name = player.name;
        known.character = player.character;

        if (!player.stage.empty() && !same_place(known.stage, known.scenario, player.stage, player.scenario)) {
            // As sm64coopdx's: leaving this player's galaxy says so; otherwise where they went.
            if (!my_stage.empty() && !observatory_stage(my_stage) &&
                same_place(known.stage, known.scenario, my_stage, my_scenario)) {
                popup(Popup::Kind::LeftHere, player);
            } else if (observatory_stage(player.stage)) {
                popup(Popup::Kind::Observatory, player);
            } else if (same_place(player.stage, player.scenario, my_stage, my_scenario)) {
                popup(Popup::Kind::EnteredHere, player);
            } else {
                popup(Popup::Kind::Galaxy, player);
            }
        }
        if (!player.stage.empty()) {
            known.stage = player.stage;
            known.scenario = player.scenario;
        }

        // The host's count of their stars new to the save went up: one pop-up, however many came at once.
        if (player.stars > known.stars) popup(Popup::Kind::Star, player);
        known.stars = player.stars;
    }
}

void Popups::update_instance(std::span<const InstancePlayer> here, std::uint64_t now_ms) {
    const std::lock_guard<std::mutex> lock(lock_);
    std::map<net::PeerId, bool> now;
    for (const InstancePlayer& player : here) {
        now[player.id] = player.dead;
        const auto before = dead_.find(player.id);
        if (before == dead_.end() || before->second || !player.dead) continue;
        Popup made;
        made.kind = Popup::Kind::Died;
        made.name = player.name;
        made.character = player.character;
        made.at_ms = now_ms;
        add(std::move(made));
    }
    dead_ = std::move(now);
}

std::optional<Popup> Popups::take(std::uint64_t now_ms) {
    const std::lock_guard<std::mutex> lock(lock_);
    while (!waiting_.empty()) {
        Popup popup = std::move(waiting_.front());
        waiting_.pop_front();
        if (now_ms <= popup.at_ms || now_ms - popup.at_ms <= kPopupWaitMs) return popup;
    }
    return std::nullopt;
}

void Popups::clear() {
    const std::lock_guard<std::mutex> lock(lock_);
    in_room_ = false;
    known_.clear();
    dead_.clear();
    waiting_.clear();
}

void Popups::add(Popup popup) {
    if (!popup_wanted(popup.kind, options_)) return;
    waiting_.push_back(std::move(popup));
    while (waiting_.size() > kPopupWaitLimit) waiting_.pop_front();
}

}  // namespace galaxy::coop
