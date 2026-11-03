// Player settings in runtime_settings.ini: input mode, audio gains, text speed, language, Discord status, online co-op.
#pragma once

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace galaxy {

// Auto: keyboard, mouse and every controller are live together and the pointer follows whichever moved
// last. KeyboardMouse and Controller keep only that source.
enum class RuntimeInputMode : std::uint32_t {
    Auto = 0,
    KeyboardMouse = 1,
    Controller = 2,
    RealWiimote = 3,
};

struct RuntimeAudioSettings {
    float master = 1.0f;
    float music = 1.0f;
    float sfx = 1.0f;
    float voice = 1.0f;
    float ambience = 1.0f;
    // In-engine cutscene sounds (the game's DEMO sound category) and the movies' audio.
    float cutscene = 1.0f;
    // What the game plays on the Wii Remote speaker, mixed into the output; off unless chosen.
    float wiimote_speaker = 0.0f;

    friend bool operator==(const RuntimeAudioSettings&, const RuntimeAudioSettings&) = default;
};

// The style of the game's button icons: the controller in use (a Wii Remote's for the keyboard and controllers it does
// not know), or one style always.
enum class ButtonIcons : std::uint8_t { Auto, Game, Xbox, Nintendo, PlayStation, Keyboard };

// The game's languages, by the game's language codes; the disc's release picks each one's
// folder (UsEnglish on North America's disc, EuEnglish on Europe's, JpJapanese on Japan's). Auto: the system's language
// where the disc has it, else English, else the disc's first (game_language_resolved).
enum class GameLanguage : std::uint8_t {
    Japanese = 0,
    English = 1,
    German = 2,
    French = 3,
    Spanish = 4,
    Italian = 5,
    Dutch = 6,
    Auto = 0xFF,
};
// Every language a release's disc holds, in the order the settings step through them.
inline constexpr GameLanguage kGameLanguages[] = {GameLanguage::English, GameLanguage::German,  GameLanguage::French,
                                                  GameLanguage::Spanish, GameLanguage::Italian, GameLanguage::Dutch,
                                                  GameLanguage::Japanese};

// Online co-op (docs/COOP_PLAN.md), not playable yet: the coop_* keys, written only when they differ from these
// defaults. The port is the UDP port a host listens on and a join without one connects to.
inline constexpr std::uint16_t kCoopDefaultPort = 7777u;
inline constexpr unsigned kCoopMaxPlayersLimit = 16u;
// The longest player name and password, in bytes (printable ASCII; longer values are cut).
inline constexpr std::size_t kCoopTextLimit = 32u;
// The longest remembered join address ("host:port", "[ipv6]:port"), in bytes.
inline constexpr std::size_t kCoopAddressLimit = 64u;

// How players touch one another, sm64coopdx's options (docs/COOP_PLAN.md, "Player collisions"). Non-solid: they pass
// through. Solid: they push apart, bounce off each other's heads and knock one another back with attacks, with no
// damage. Friendly fire: the same, and attacks cost health.
enum class CoopPlayerInteraction : std::uint8_t { NonSolid, Solid, FriendlyFire };
// Friendly fire's rules: sm64coopdx's original ones, or its revamped ones (which attacks beat which, the knockback
// each gives).
enum class CoopPvpMode : std::uint8_t { Classic, Revamped };
// The player's character (Options' PLAYER page): the player's own, whatever save they play in.
enum class CoopCharacter : std::uint8_t { Mario, Luigi };
// Knockback strength, 0 to 255; the menu's choices are weak, normal and too much (sm64coopdx's values).
inline constexpr unsigned kCoopKnockbackWeak = 10u, kCoopKnockbackNormal = 25u, kCoopKnockbackTooMuch = 60u;

struct RuntimeCoopSettings {
    std::uint16_t port = kCoopDefaultPort;
    unsigned max_players = 8u;  // 2 to kCoopMaxPlayersLimit, the host included
    std::string player_name;    // empty: PLAYER
    CoopCharacter character = CoopCharacter::Mario;
    std::string password;       // a host's: joining needs it; a joiner's: sent to the host. Empty: none
    std::string join_address;   // the Join page's last address, "host[:port]" (empty: none yet)
    // The host's are the session's: they reach every player as they join.
    CoopPlayerInteraction player_interaction = CoopPlayerInteraction::Solid;
    unsigned knockback_strength = kCoopKnockbackNormal;
    CoopPvpMode pvp_mode = CoopPvpMode::Classic;
    // The room's cheats (the Online tab's Cheats): off, they do nothing for anyone in it.
    bool cheats = false;
    // The room's built-in Personal Star Counter mod (the Host page's Mods, the Online tab): each player's stars on the
    // HUD and in the player list. Off by default.
    bool star_counter = false;

    friend bool operator==(const RuntimeCoopSettings&, const RuntimeCoopSettings&) = default;
};

// A co-op address, "host:port", "host" (the default port) or "[ipv6]:port". False when malformed.
[[nodiscard]] bool parse_coop_endpoint(std::string_view text, std::string& host, std::uint16_t& port);
// "nonsolid", "solid" or "friendly_fire"; "classic" or "revamped" (runtime_settings.ini).
[[nodiscard]] const char* coop_player_interaction_key(CoopPlayerInteraction interaction) noexcept;
[[nodiscard]] const char* coop_pvp_mode_key(CoopPvpMode mode) noexcept;
// "mario" or "luigi" (runtime_settings.ini's coop_character).
[[nodiscard]] const char* coop_character_key(CoopCharacter character) noexcept;

struct RuntimeSettings {
#if defined(__ANDROID__)
    RuntimeInputMode input_mode = RuntimeInputMode::Auto;  // the touch controls, and a pad when one is in hand
#else
    RuntimeInputMode input_mode = RuntimeInputMode::KeyboardMouse;
#endif
    RuntimeAudioSettings audio{};
    // Steps of dialogue text revealed per frame: 1 the game's pace, 2 or 3; 4 each page at once (instant).
    unsigned text_speed = 1u;
    // Takes effect at the next start.
    GameLanguage language = GameLanguage::Auto;
    // Discord shows what the player is doing (galaxy, star, Power Stars) while Discord runs.
    bool discord_status = true;
    ButtonIcons button_icons = ButtonIcons::Auto;
    // Co-star mode: with two controllers attached, the second plays player 2.
    bool co_star = false;
    RuntimeCoopSettings coop{};

    friend bool operator==(const RuntimeSettings&, const RuntimeSettings&) = default;
};

// The languages on the game's disc as it is read (its language folders, the mods' among them): the language setting
// offers only these, and a language whose folder is missing starts as Auto does. Every language until it is set.
void set_disc_languages(std::span<const std::string> folders) noexcept;
[[nodiscard]] bool game_language_on_disc(GameLanguage language) noexcept;
// The language `setting` plays in on this disc: itself where the disc has it; else (and for Auto) the system's
// language where the disc has it, else English, else the first of kGameLanguages the disc has.
[[nodiscard]] GameLanguage game_language_resolved(GameLanguage setting) noexcept;

// The language the game runs in this session: the setting when the game first asks (at boot), then
// fixed, so a change made while playing never mixes two languages.
[[nodiscard]] GameLanguage game_language_for_boot() noexcept;
// Whether the game has asked yet, and if so, its language.
[[nodiscard]] bool running_game_language(GameLanguage& language) noexcept;
// "auto", "japanese", "english", "german", "french", "spanish", "italian" or "dutch" (runtime_settings.ini).
[[nodiscard]] const char* game_language_key(GameLanguage language) noexcept;
// "auto", "wii", "xbox", "nintendo" or "playstation" (runtime_settings.ini).
[[nodiscard]] const char* button_icons_key(ButtonIcons icons) noexcept;

// The generation changes only when the input mode changes, so device queues can reject reports acquired
// under an older source.
struct RuntimeInputModeState {
    RuntimeInputMode mode = RuntimeInputMode::KeyboardMouse;
    std::uint64_t generation = 1u;
};

// The chosen input mode has nothing to play with: Controller with no controller, Wii Remote with no remote, Auto with
// nothing at all. Keyboard and mouse never reports it.
[[nodiscard]] constexpr bool input_missing(RuntimeInputMode mode, bool controller, bool wii_remote,
                                           bool keyboard_or_mouse) noexcept {
    switch (mode) {
    case RuntimeInputMode::Controller: return !controller;
    case RuntimeInputMode::RealWiimote: return !wii_remote;
    case RuntimeInputMode::Auto: return !controller && !wii_remote && !keyboard_or_mouse;
    case RuntimeInputMode::KeyboardMouse: break;
    }
    return false;
}

[[nodiscard]] RuntimeSettings get_runtime_settings();
[[nodiscard]] RuntimeInputModeState get_runtime_input_mode_state();
void set_runtime_settings(const RuntimeSettings& settings);
// Missing files keep the defaults. Invalid explicit paths and read failures throw before any setting is
// published. Gains use the C numeric locale and complete finite tokens. A malformed value keeps its prior
// or default gain.
void load_runtime_settings_from_file();
// Returns false on an invalid explicit path or a failed write; a bad override never redirects the save to
// the normal settings file.
[[nodiscard]] bool save_runtime_settings_to_file();
// The folder of the settings file (GALAXY_RUNTIME_SETTINGS_PATH's, else Nebula's data folder); empty when unknown.
[[nodiscard]] std::filesystem::path runtime_settings_folder() noexcept;

[[nodiscard]] const char* runtime_input_mode_name(RuntimeInputMode mode);

}  // namespace galaxy
