// Player settings in runtime_settings.ini: input mode, audio gains, text speed, language, Discord status, online co-op.
#pragma once

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <string_view>

namespace galaxy {

// Auto: keyboard, mouse and every controller are live together and the pointer follows whichever moved
// last. KeyboardMouse and Controller keep only that source.
enum class RuntimeInputMode : std::uint32_t {
    Auto = 0,
    KeyboardMouse = 1,
    Controller = 2,
    RealWiimote = 3,  // a real Wii Remote, pointing at the sensor bar
    Phone = 4,  // the on-screen controls alone (phones and tablets)
    WiimoteMotion = 5,  // a real Wii Remote, pointing by its MotionPlus (docs/WII_REMOTE.md)
};

// Either real Wii Remote mode: the remote alone is read.
[[nodiscard]] constexpr bool is_wii_remote_mode(RuntimeInputMode mode) noexcept {
    return mode == RuntimeInputMode::RealWiimote || mode == RuntimeInputMode::WiimoteMotion;
}

// The game's sound modes. Surround is matrix-encoded on two speakers and has its own speakers where the output has them.
enum class SoundOutput : std::uint8_t { Mono, Stereo, Surround };

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
    SoundOutput sound_output = SoundOutput::Stereo;

    friend bool operator==(const RuntimeAudioSettings&, const RuntimeAudioSettings&) = default;
};

// The style of the game's button icons: the controller in use (a Wii Remote's for the keyboard and controllers it does
// not know), or one style always.
enum class ButtonIcons : std::uint8_t { Auto, Game, Xbox, Nintendo, PlayStation, Keyboard };

// The game's languages, by the game's language codes; the game's release picks each one's folder (UsEnglish for North
// America, EuEnglish for Europe, JpJapanese for Japan). Auto: the system's language where the game has it, else
// English, else the game's first (game_language_resolved).
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
// Every language a release holds, in the order the settings step through them.
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
// The NebulaNet games use unless coop_nebulanet names another (galaxy/coop_nebulanet.h).
inline constexpr const char* kCoopNebulaNetDefault = "https://nebulanet.mackery.com";

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
    std::string room_name;      // the Host page's ROOM NAME, as NebulaNet lists the room; empty: the player's name
    CoopCharacter character = CoopCharacter::Mario;
    std::string password;       // a host's: joining needs it; a joiner's: sent to the host. Empty: none
    std::string join_address;   // the Join page's last room code or address, "host[:port]" (empty: none yet)
    // The Host page's NETWORK: NebulaNet, where friends join by the room's code with no port forwarded
    // (galaxy/coop_nebulanet.h), or Direct, by the host's address and port.
    bool nebulanet = true;
    // Another NebulaNet than the game's (coop_nebulanet=https://...), for testing one's own; empty: the game's.
    std::string nebulanet_server;
    // The host's are the session's: they reach every player as they join.
    CoopPlayerInteraction player_interaction = CoopPlayerInteraction::Solid;
    unsigned knockback_strength = kCoopKnockbackNormal;
    CoopPvpMode pvp_mode = CoopPvpMode::Classic;
    // Bubble on death (sm64coopdx's): a player who dies with another alive in the same part of the galaxy floats in a
    // bubble to them instead of missing, for a life.
    bool bubble_death = true;
    // The room's cheats (the Online tab's Cheats): off, they do nothing for anyone in it.
    bool cheats = false;
    // The room's built-in Personal Star Counter mod (the Host page's Mods, the Online tab): each player's stars on the
    // HUD and in the player list. Off by default.
    bool star_counter = false;

    friend bool operator==(const RuntimeCoopSettings&, const RuntimeCoopSettings&) = default;
};

// Which of online co-op's pop-ups this player sees (Options' PLAYER page, galaxy/coop_popups.h): sm64coopdx's
// notices of other players in the screen's top-right corner. Each is on until turned off.
struct RuntimeCoopPopups {
    bool joins = true;        // a player joined the room
    bool leaves = true;       // a player left it
    bool observatory = true;  // a player went to the Comet Observatory
    bool galaxies = true;     // a player went to a galaxy's star, or came to or left this player's
    bool deaths = true;       // a player in this player's galaxy died
    bool stars = true;        // a player got a Power Star new to the save

    friend bool operator==(const RuntimeCoopPopups&, const RuntimeCoopPopups&) = default;
};

// A co-op address, "host:port", "host" (the default port) or "[ipv6]:port". False when malformed.
[[nodiscard]] bool parse_coop_endpoint(std::string_view text, std::string& host, std::uint16_t& port);
// "nonsolid", "solid" or "friendly_fire"; "classic" or "revamped" (runtime_settings.ini).
[[nodiscard]] const char* coop_player_interaction_key(CoopPlayerInteraction interaction) noexcept;
[[nodiscard]] const char* coop_pvp_mode_key(CoopPvpMode mode) noexcept;
// "mario" or "luigi" (runtime_settings.ini's coop_character).
[[nodiscard]] const char* coop_character_key(CoopCharacter character) noexcept;

// What the game does while its window is not in front (another program is; Windows): goes on, pauses through its own pause menu
// (the pause button's path, so its timers stop), or goes on with its sound silent. A minimised window holds the game in every
// case, as it always has.
enum class FocusLoss : std::uint8_t { KeepRunning = 0, Pause = 1, Mute = 2 };

struct RuntimeSettings {
    // Auto: whatever is attached and in use.
    RuntimeInputMode input_mode = RuntimeInputMode::Auto;
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
    // Who is banned from, and who is a moderator of, every co-op room this player hosts (sm64coopdx's ban and
    // moderator lists; /permban and /moderator add to them): the players' IP addresses. Not the Host page's: the
    // room's commands keep them.
    std::vector<std::string> coop_bans;
    std::vector<std::string> coop_moderators;
    // The co-op pop-ups this player sees (the PLAYER page's). Not in RuntimeCoopSettings, which the main menu's room
    // overwrites (keep_coop_settings).
    RuntimeCoopPopups coop_popups{};
    // The menus' text and everything around it, in percent; 0: as large as the screen's size calls for.
    unsigned menu_text_size = 0u;
    // What the game does when its window loses focus (runtime_settings.ini: focus_loss=keep|pause|mute).
    FocusLoss focus_loss = FocusLoss::KeepRunning;

    friend bool operator==(const RuntimeSettings&, const RuntimeSettings&) = default;
};

// The languages the game's data holds as it is read (its language folders, the mods' among them): the language setting
// offers only these, and a language whose folder is missing starts as Auto does. Every language until it is set.
void set_game_languages(std::span<const std::string> folders) noexcept;
[[nodiscard]] bool game_language_available(GameLanguage language) noexcept;
// The language `setting` plays in with this data: itself where the data has it; else (and for Auto) the system's
// language where the data has it, else English, else the first of kGameLanguages the data has.
[[nodiscard]] GameLanguage game_language_resolved(GameLanguage setting) noexcept;

// The language the game runs in this session: the setting when the game first asks (at boot), then
// fixed, so a change made while playing never mixes two languages.
[[nodiscard]] GameLanguage game_language_for_boot() noexcept;
// Whether the game has asked yet, and if so, its language.
[[nodiscard]] bool running_game_language(GameLanguage& language) noexcept;
// "auto", "japanese", "english", "german", "french", "spanish", "italian" or "dutch" (runtime_settings.ini).
[[nodiscard]] const char* game_language_key(GameLanguage language) noexcept;
// "auto", "game", "xbox", "nintendo", "playstation" or "keyboard" (runtime_settings.ini; "wii" is read as "game").
[[nodiscard]] const char* button_icons_key(ButtonIcons icons) noexcept;
[[nodiscard]] const char* sound_output_key(SoundOutput output) noexcept;
// "keep", "pause" or "mute" (runtime_settings.ini).
[[nodiscard]] const char* focus_loss_key(FocusLoss focus_loss) noexcept;

// The host sets this while the window is in the background with FocusLoss::Mute: the game's sound is silent (master gain 0,
// ramped as any gain change) until it is cleared. Any thread; not a setting, never saved. The mixer reads background_muted()
// where it reads the master gain (native_audio.cpp).
void set_background_muted(bool muted) noexcept;
[[nodiscard]] bool background_muted() noexcept;

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
    case RuntimeInputMode::RealWiimote:
    case RuntimeInputMode::WiimoteMotion: return !wii_remote;
    case RuntimeInputMode::Auto: return !controller && !wii_remote && !keyboard_or_mouse;
    case RuntimeInputMode::Phone:
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
// The folder of the settings file (--settings, else Nebula's data folder); empty when unknown.
[[nodiscard]] std::filesystem::path runtime_settings_folder() noexcept;

[[nodiscard]] const char* runtime_input_mode_name(RuntimeInputMode mode);

}  // namespace galaxy
