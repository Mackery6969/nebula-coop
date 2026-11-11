// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ADVANCED page's galaxy select: its stage list, the page's requests (taken by the game each scene frame,
// svc_galaxy_travel) and the names the game gives it in its own words.

#include "galaxy/runtime_settings.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::render {

struct GalaxyStage {
    const char* id;       // the stage's name in the game's data
    std::uint32_t stars;  // scenarios that can be entered (1..stars)
};

// By dome (Terrace, Fountain, Kitchen, Bedroom, Engine Room, Garden), then the rest. `stars` leaves out the hidden
// scenarios (ScenarioData IsHidden, numbered after the others): the game sizes its per-scenario data without them
// (ScenarioData::getScenarioNum), so entering one directly indexes past those arrays.
inline constexpr GalaxyStage kGalaxyStages[] = {
    // The Comet Observatory; the game's name lookup does not know it, so it is shown as kObservatoryName.
    {"AstroGalaxy", 1u},
    {"EggStarGalaxy", 5u}, {"HoneyBeeKingdomGalaxy", 5u}, {"SurfingLv1Galaxy", 1u},
    {"FlipPanelExGalaxy", 1u}, {"TriLegLv1Galaxy", 1u},
    {"StarDustGalaxy", 5u}, {"BattleShipGalaxy", 5u}, {"TamakoroExLv1Galaxy", 1u},
    {"BreakDownPlanetGalaxy", 1u}, {"KoopaBattleVs1Galaxy", 1u},
    {"HeavenlyBeachGalaxy", 5u}, {"PhantomGalaxy", 5u}, {"CubeBubbleExLv1Galaxy", 1u},
    {"OceanFloaterLandGalaxy", 1u}, {"KoopaJrShipLv1Galaxy", 1u},
    {"CosmosGardenGalaxy", 5u}, {"IceVolcanoGalaxy", 5u}, {"SandClockGalaxy", 5u},
    {"HoneyBeeExGalaxy", 1u}, {"KoopaBattleVs2Galaxy", 1u},
    {"ReverseKingdomGalaxy", 5u}, {"OceanRingGalaxy", 5u}, {"FactoryGalaxy", 5u},
    {"SkullSharkGalaxy", 1u}, {"FloaterOtaKingGalaxy", 1u},
    {"OceanPhantomCaveGalaxy", 5u}, {"CannonFleetGalaxy", 5u}, {"HellProminenceGalaxy", 5u},
    {"DarkRoomGalaxy", 1u}, {"KoopaBattleVs3Galaxy", 1u},
    {"HeavensDoorGalaxy", 2u}, {"BeltConveyerExGalaxy", 1u}, {"CocoonExGalaxy", 1u},
    {"TearDropGalaxy", 1u}, {"FishTunnelGalaxy", 1u}, {"TransformationExGalaxy", 1u},
    {"SnowCapsuleGalaxy", 1u}, {"TeresaMario2DGalaxy", 1u}, {"CubeBubbleExLv2Galaxy", 1u},
    {"TamakoroExLv2Galaxy", 1u}, {"SurfingLv2Galaxy", 1u},
};
inline constexpr std::size_t kGalaxyStageCount = sizeof(kGalaxyStages) / sizeof(kGalaxyStages[0]);
inline constexpr const char* kObservatoryId = "AstroGalaxy";
inline constexpr const char* kObservatoryName = "COMET OBSERVATORY";

void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept;
void request_grand_stars() noexcept;
// The room ended under a joiner (its host quit): the game starts again at the main menu.
void request_leave_game() noexcept;
// A save is being played (Host left the main menu): the Online page works only then.
void set_coop_in_game(bool in_game) noexcept;
[[nodiscard]] bool coop_in_game() noexcept;
// The room as online play has it, for the Online page: how many play in it, and for a player who joined, the host's
// settings (shown, not changed: the host's are the room's).
struct CoopRoomStatus {
    unsigned players = 1u;
    bool joined = false;
    RuntimeCoopSettings room{};
    std::string code;  // the room's NebulaNet code, when it has one (the Online tab shows it)
};
void set_coop_room_status(const CoopRoomStatus& status);
[[nodiscard]] CoopRoomStatus coop_room_status();
// The main menu's text rows (galaxy/coop_menu.h): while one is edited, the window's keys type into it (characters and
// galaxy::coop::TextKey keys) instead of reaching the game; the host takes what was typed each frame. Any thread.
void set_coop_text_entry(bool on) noexcept;
[[nodiscard]] bool coop_text_entry() noexcept;
void push_coop_text(char32_t key);
[[nodiscard]] std::vector<char32_t> take_coop_text_input();
// Online co-op's chat (galaxy/coop_chat.h): while the host allows it (in a room's game, no menu over it), T pressed in
// the window opens a line, which then takes the window's typing as a text row does. The window asks coop_chat_key for
// each key pressed (a Windows virtual-key code): true when it opened the line, the key then reaching nothing else.
// The host takes the opening once (take_coop_chat_open). Any thread.
void set_coop_chat_available(bool available) noexcept;
[[nodiscard]] bool coop_chat_key(std::uint32_t virtual_key) noexcept;
[[nodiscard]] bool take_coop_chat_open() noexcept;
// The game, once per scene frame: a travel the page asked for (true, once), and the Grand Stars.
[[nodiscard]] bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept;
[[nodiscard]] bool take_grand_stars() noexcept;
[[nodiscard]] bool take_leave_game() noexcept;
// The page asks for the names while it has none; the game is asked once (true), then gives them.
void want_galaxy_select_names() noexcept;
[[nodiscard]] bool take_galaxy_select_names_wanted() noexcept;
// A name from the game (star 0: the galaxy's).
void set_galaxy_select_name(std::size_t stage, std::uint32_t star, std::string name);
// The stage and star names; empty when not known.
[[nodiscard]] std::string galaxy_select_name(std::size_t stage);
[[nodiscard]] std::string galaxy_select_star_name(std::size_t stage, std::uint32_t star);

}  // namespace galaxy::render
