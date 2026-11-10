#include "galaxy/portable.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/run_options.h"
#include "../../tools/test_driver/switches.h"
#include "galaxy/atomic_file.h"
#include "galaxy/input_bindings.h"
#include "galaxy/launch_args.h"
#include "galaxy/platform_paths.h"
#include "galaxy/game_instance.h"
#include "galaxy/c_locale_number.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#pragma comment(lib, "Shell32.lib")
#endif

namespace galaxy {

namespace {

RuntimeSettings s_settings{};
std::uint64_t s_input_mode_generation = 1u;
std::shared_mutex s_settings_lock;

// Printable ASCII only, with no surrounding spaces, at most `limit` bytes: the file stays one line per key and the
// game's font draws every character.
std::string sanitize_coop_text(std::string_view text, std::size_t limit = kCoopTextLimit) {
    std::string out;
    for (const char ch : text) {
        if (ch >= 0x20 && ch < 0x7F && out.size() < limit) {
            out.push_back(ch);
        }
    }
    const std::size_t begin = out.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    return out.substr(begin, out.find_last_not_of(' ') - begin + 1u);
}

// The most entries a kept co-op list (coop_bans, coop_moderators) holds, and an entry's longest (an IPv6 address).
constexpr std::size_t kCoopListLimit = 256u;
constexpr std::size_t kCoopListEntryLimit = 64u;

// A kept co-op list: addresses (hex digits, '.' and ':'), each once.
std::vector<std::string> sanitize_coop_list(const std::vector<std::string>& list) {
    std::vector<std::string> out;
    for (const std::string& entry : list) {
        if (out.size() >= kCoopListLimit) break;
        const bool address = !entry.empty() && entry.size() <= kCoopListEntryLimit &&
                             std::all_of(entry.begin(), entry.end(), [](char ch) {
                                 return std::isxdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == ':';
                             });
        if (address && std::find(out.begin(), out.end(), entry) == out.end()) out.push_back(entry);
    }
    return out;
}

// The co-op pop-ups' keys in runtime_settings.ini (coop_popup_<name>=0 turns one off), each with its flag.
std::array<std::pair<const char*, bool*>, 6> coop_popup_keys(RuntimeCoopPopups& popups) {
    return {{{"joins", &popups.joins},
             {"leaves", &popups.leaves},
             {"observatory", &popups.observatory},
             {"galaxies", &popups.galaxies},
             {"deaths", &popups.deaths},
             {"stars", &popups.stars}}};
}

// "a,b,c": its entries.
std::vector<std::string> split_coop_list(std::string_view text) {
    std::vector<std::string> out;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string entry(text.substr(0, comma));
        std::erase(entry, ' ');
        out.push_back(std::move(entry));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1u);
    }
    return out;
}

// A port, 1 to 65535, in decimal digits only.
bool parse_port(std::string_view text, std::uint16_t& port) {
    if (text.empty() || text.size() > 5u) {
        return false;
    }
    unsigned value = 0u;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10u + static_cast<unsigned>(ch - '0');
    }
    if (value == 0u || value > 65535u) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

RuntimeSettings sanitize_settings(RuntimeSettings settings) {
    switch (settings.input_mode) {
    case RuntimeInputMode::Auto:
    case RuntimeInputMode::KeyboardMouse:
    case RuntimeInputMode::Controller:
    case RuntimeInputMode::RealWiimote:
    case RuntimeInputMode::Phone:
    case RuntimeInputMode::WiimoteMotion:
        break;
    default:
        settings.input_mode = RuntimeInputMode::KeyboardMouse;
        break;
    }
    settings.menu_text_size = settings.menu_text_size == 0u ? 0u : std::clamp(settings.menu_text_size, 70u, 200u);
    const auto sanitize_gain = [](float gain) {
        return std::isfinite(gain) ? std::clamp(gain, 0.0f, 4.0f) : 1.0f;
    };
    settings.audio.master = sanitize_gain(settings.audio.master);
    settings.audio.music = sanitize_gain(settings.audio.music);
    settings.audio.sfx = sanitize_gain(settings.audio.sfx);
    settings.audio.voice = sanitize_gain(settings.audio.voice);
    settings.audio.ambience = sanitize_gain(settings.audio.ambience);
    settings.audio.cutscene = sanitize_gain(settings.audio.cutscene);
    settings.audio.wiimote_speaker = std::isfinite(settings.audio.wiimote_speaker)
        ? std::clamp(settings.audio.wiimote_speaker, 0.0f, 4.0f)
        : 0.0f;
    settings.text_speed = std::clamp(settings.text_speed, 1u, 4u);
    if (std::find(std::begin(kGameLanguages), std::end(kGameLanguages), settings.language) == std::end(kGameLanguages)) {
        settings.language = GameLanguage::Auto;
    }
    if (settings.coop.port == 0u) {
        settings.coop.port = kCoopDefaultPort;
    }
    settings.coop.max_players = std::clamp(settings.coop.max_players, 2u, kCoopMaxPlayersLimit);
    if (settings.coop.player_interaction > CoopPlayerInteraction::FriendlyFire) {
        settings.coop.player_interaction = CoopPlayerInteraction::Solid;
    }
    settings.coop.knockback_strength = std::min(settings.coop.knockback_strength, 255u);
    if (settings.coop.pvp_mode > CoopPvpMode::Revamped) {
        settings.coop.pvp_mode = CoopPvpMode::Classic;
    }
    settings.coop.player_name = sanitize_coop_text(settings.coop.player_name);
    settings.coop.password = sanitize_coop_text(settings.coop.password);
    if (settings.coop.character > CoopCharacter::Luigi) {
        settings.coop.character = CoopCharacter::Mario;
    }
    settings.coop.join_address = sanitize_coop_text(settings.coop.join_address, kCoopAddressLimit);
    // Only a web address: it is only ever connected to, never shown.
    std::string& server = settings.coop.nebulanet_server;
    server = sanitize_coop_text(server, kCoopAddressLimit);
    if (!server.starts_with("https://") && !server.starts_with("http://")) server.clear();
    settings.coop_bans = sanitize_coop_list(settings.coop_bans);
    settings.coop_moderators = sanitize_coop_list(settings.coop_moderators);
    return settings;
}

float parse_gain(const char* value, float fallback) {
    if (galaxy::c_numeric_locale() == nullptr) {
        throw std::runtime_error("cannot create runtime settings numeric locale");
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = galaxy::strtof_c(value, &end);
    if (end == value || errno == ERANGE || !std::isfinite(parsed)) {
        return fallback;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        ++end;
    }
    if (*end != '\0') {
        return fallback;
    }
    return std::clamp(parsed, 0.0f, 4.0f);
}

RuntimeInputMode parse_input_mode(
    const char* value,
    RuntimeInputMode fallback) {
    std::string text(value);
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    if (text == "auto") {
        return RuntimeInputMode::Auto;
    }
    if (text == "controller" || text == "xinput" || text == "gamepad" ||
        text == "switch" || text == "switch_pro") {
        return RuntimeInputMode::Controller;
    }
    if (text == "keyboard" || text == "keyboard_mouse" ||
        text == "keyboard-mouse" || text == "kbm" || text == "mouse") {
        return RuntimeInputMode::KeyboardMouse;
    }
    if (text == "phone") {
        return RuntimeInputMode::Phone;
    }
    if (text == "wiimote" || text == "wii_remote" || text == "wii-remote" ||
        text == "real_wiimote" || text == "real-wiimote") {
        return RuntimeInputMode::RealWiimote;
    }
    if (text == "wiimote_motion" || text == "wiimote-motion") {
        return RuntimeInputMode::WiimoteMotion;
    }
    return fallback;
}

std::string_view trim(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1u);
}

void apply_kv(RuntimeSettings& settings, const char* key, const char* value) {
    if (std::strcmp(key, "input_mode") == 0) {
        settings.input_mode = parse_input_mode(value, settings.input_mode);
    } else if (std::strcmp(key, "audio_master_gain") == 0) {
        settings.audio.master = parse_gain(value, settings.audio.master);
    } else if (std::strcmp(key, "audio_music_gain") == 0) {
        settings.audio.music = parse_gain(value, settings.audio.music);
    } else if (std::strcmp(key, "audio_sfx_gain") == 0) {
        settings.audio.sfx = parse_gain(value, settings.audio.sfx);
    } else if (std::strcmp(key, "audio_voice_gain") == 0) {
        settings.audio.voice = parse_gain(value, settings.audio.voice);
    } else if (std::strcmp(key, "audio_ambience_gain") == 0) {
        settings.audio.ambience = parse_gain(value, settings.audio.ambience);
    } else if (std::strcmp(key, "audio_cutscene_gain") == 0) {
        settings.audio.cutscene = parse_gain(value, settings.audio.cutscene);
    } else if (std::strcmp(key, "co_star") == 0) {
        settings.co_star = std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0;
    } else if (std::strcmp(key, "audio_wiimote_speaker_gain") == 0) {
        settings.audio.wiimote_speaker = parse_gain(value, settings.audio.wiimote_speaker);
    } else if (std::strcmp(key, "text_speed") == 0) {
        if (value[0] >= '1' && value[0] <= '4' && value[1] == '\0') {
            settings.text_speed = static_cast<unsigned>(value[0] - '0');
        }
    } else if (std::strcmp(key, "discord_status") == 0) {
        settings.discord_status = value[0] != '0';
    } else if (std::strcmp(key, "focus_loss") == 0) {
        for (unsigned i = 0; i <= static_cast<unsigned>(FocusLoss::Mute); ++i) {
            if (::galaxy::text_iequal(value, focus_loss_key(static_cast<FocusLoss>(i)))) {
                settings.focus_loss = static_cast<FocusLoss>(i);
            }
        }
    } else if (std::strcmp(key, "menu_text_size") == 0) {
        settings.menu_text_size = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    } else if (std::strcmp(key, "audio_sound_output") == 0) {
        for (unsigned i = 0; i <= static_cast<unsigned>(SoundOutput::Surround); ++i) {
            if (::galaxy::text_iequal(value, sound_output_key(static_cast<SoundOutput>(i)))) {
                settings.audio.sound_output = static_cast<SoundOutput>(i);
            }
        }
    } else if (std::strcmp(key, "button_icons") == 0) {
        for (unsigned i = 0; i <= static_cast<unsigned>(ButtonIcons::Keyboard); ++i) {
            if (::galaxy::text_iequal(value, button_icons_key(static_cast<ButtonIcons>(i)))) {
                settings.button_icons = static_cast<ButtonIcons>(i);
            }
        }
        // Files of earlier builds name the game's own icons "wii"; the next save writes "game".
        if (::galaxy::text_iequal(value, "wii")) settings.button_icons = ButtonIcons::Game;
    } else if (std::strcmp(key, "language") == 0) {
        if (::galaxy::text_iequal(value, game_language_key(GameLanguage::Auto))) settings.language = GameLanguage::Auto;
        for (const GameLanguage language : kGameLanguages) {
            if (::galaxy::text_iequal(value, game_language_key(language))) settings.language = language;
        }
    } else if (std::strcmp(key, "coop_port") == 0) {
        std::uint16_t port = 0u;
        if (parse_port(value, port)) settings.coop.port = port;
    } else if (std::strcmp(key, "coop_max_players") == 0) {
        char* end = nullptr;
        const unsigned long count = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && count <= kCoopMaxPlayersLimit) {
            settings.coop.max_players = static_cast<unsigned>(count);
        }
    } else if (std::strcmp(key, "coop_player_name") == 0) {
        settings.coop.player_name = value;
    } else if (std::strcmp(key, "coop_password") == 0) {
        settings.coop.password = value;
    } else if (std::strcmp(key, "coop_character") == 0) {
        for (const CoopCharacter character : {CoopCharacter::Mario, CoopCharacter::Luigi}) {
            if (::galaxy::text_iequal(value, coop_character_key(character))) settings.coop.character = character;
        }
    } else if (std::strcmp(key, "coop_join_address") == 0) {
        settings.coop.join_address = value;
    } else if (std::strcmp(key, "coop_network") == 0) {
        if (::galaxy::text_iequal(value, "nebulanet")) settings.coop.nebulanet = true;
        if (::galaxy::text_iequal(value, "direct")) settings.coop.nebulanet = false;
    } else if (std::strcmp(key, "coop_nebulanet") == 0) {
        settings.coop.nebulanet_server = value;
    } else if (std::strcmp(key, "coop_player_interaction") == 0) {
        for (const CoopPlayerInteraction interaction :
             {CoopPlayerInteraction::NonSolid, CoopPlayerInteraction::Solid, CoopPlayerInteraction::FriendlyFire}) {
            if (::galaxy::text_iequal(value, coop_player_interaction_key(interaction))) {
                settings.coop.player_interaction = interaction;
            }
        }
    } else if (std::strcmp(key, "coop_knockback_strength") == 0) {
        char* end = nullptr;
        const unsigned long strength = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && strength <= 255u) {
            settings.coop.knockback_strength = static_cast<unsigned>(strength);
        }
    } else if (std::strcmp(key, "coop_bubble_death") == 0) {
        settings.coop.bubble_death = std::strcmp(value, "0") != 0;
    } else if (std::strcmp(key, "coop_cheats") == 0) {
        settings.coop.cheats = std::strcmp(value, "1") == 0;
    } else if (std::strcmp(key, "coop_star_counter") == 0) {
        settings.coop.star_counter = std::strcmp(value, "1") == 0;
    } else if (std::strcmp(key, "coop_pvp_mode") == 0) {
        for (const CoopPvpMode mode : {CoopPvpMode::Classic, CoopPvpMode::Revamped}) {
            if (::galaxy::text_iequal(value, coop_pvp_mode_key(mode))) settings.coop.pvp_mode = mode;
        }
    } else if (std::strncmp(key, "coop_popup_", 11) == 0) {
        for (const auto& [name, flag] : coop_popup_keys(settings.coop_popups)) {
            if (std::strcmp(key + 11, name) == 0) *flag = std::strcmp(value, "0") != 0;
        }
    } else if (std::strcmp(key, "coop_bans") == 0) {
        settings.coop_bans = split_coop_list(value);
    } else if (std::strcmp(key, "coop_moderators") == 0) {
        settings.coop_moderators = split_coop_list(value);
    }
}

std::filesystem::path settings_path() {
    const std::filesystem::path& named = launch_args().settings;
    if (const std::filesystem::path& instance = game_instance_folder(); named.empty() && !instance.empty()) {
        return instance / "runtime_settings.ini";  // a second game's own (galaxy/game_instance.h)
    }
    const std::filesystem::path folder = named.empty() ? nebula_data_directory() : named;
    return folder.empty() ? folder : folder / "runtime_settings.ini";
}

// A file of an older controls version carries that version's defaults: those bindings move to the
// current defaults. Choices that differ from the old defaults stay.
void migrate_settings_file(input::ControlsConfig& controls, unsigned version) {
    if (version >= input::kControlsVersion) {
        return;
    }
    const unsigned changed = input::migrate_legacy_controls(controls, version);
    std::fprintf(stderr, "[settings] controls version %u -> %u: %u defaults updated\n", version, input::kControlsVersion, changed);
}

// A settings file the game cannot read (an overlong line, a NUL byte) is renamed to "<name>.unreadable", replacing an
// older one, so the start goes on with defaults and the player's file stays on disk.
void set_aside_unreadable_settings(const std::filesystem::path& path) {
    std::filesystem::path aside = path;
    aside += ".unreadable";
    std::error_code error;
    std::filesystem::remove(aside, error);
    error.clear();
    std::filesystem::rename(path, aside, error);
    if (error) {
        std::fprintf(stderr, "[settings] the settings file is unreadable and could not be set aside (%s); using defaults\n",
                     error.message().c_str());
    } else {
        std::fprintf(stderr, "[settings] the settings file has an overlong line or a NUL byte; kept as .unreadable, using defaults\n");
    }
}

void parse_settings_file(RuntimeSettings& settings, input::ControlsConfig& controls) {
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        return;
    }
    unsigned controls_version = 0u;

    FILE* file = open_path(path, "rb");
    const int open_error = file != nullptr ? 0 : errno;
    std::unique_ptr<FILE, decltype(&std::fclose)> owned_file(file, &std::fclose);
    if (open_error != 0 || file == nullptr) {
        if (open_error == ENOENT) {
            return;
        }
        throw std::runtime_error("cannot open runtime settings file");
    }

    std::string line;
    bool first_line = true;
    bool unreadable = false;
    for (;;) {
        line.clear();
        int character = 0;
        while ((character = std::fgetc(file)) != EOF && character != '\n') {
            if (character == 0 || line.size() >= 4096u) {
                unreadable = true;
                break;
            }
            line.push_back(static_cast<char>(character));
        }
        if (unreadable) {
            break;
        }
        if (std::ferror(file) != 0) {
            throw std::runtime_error("cannot read runtime settings file");
        }
        if (character == EOF && line.empty()) {
            break;
        }
        std::string_view record(line);
        if (first_line && record.starts_with("\xEF\xBB\xBF")) {
            record.remove_prefix(3u);
        }
        first_line = false;
        const std::size_t equals = record.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(record.substr(0u, equals));
        if (key.empty() || key.front() == '#' || key.front() == ';') {
            continue;
        }
        const std::string key_text(key);
        const std::string value_text(trim(record.substr(equals + 1u)));
        if (key_text == "controls_version") {
            controls_version = static_cast<unsigned>(std::strtoul(value_text.c_str(), nullptr, 10));
        } else if (!input::apply_controls_setting(controls, key_text, value_text)) {
            apply_kv(settings, key_text.c_str(), value_text.c_str());
        }
    }
    if (unreadable) {
        owned_file.reset();
        set_aside_unreadable_settings(path);
        settings = RuntimeSettings{};
        controls = input::default_controls_config();
        return;
    }
#if defined(__ANDROID__)
    if (controls_version < input::kControlsVersion && settings.input_mode == RuntimeInputMode::KeyboardMouse) {
        settings.input_mode = RuntimeInputMode::Auto;
    }
#endif
    migrate_settings_file(controls, controls_version);
}

#if !defined(NEBULA_DEV)
void apply_env_overrides(RuntimeSettings&) {}
#else
#include "../../tools/test_driver/runtime_settings_env.inc"
#endif

}  // namespace

std::filesystem::path runtime_settings_folder() noexcept {
    try {
        return settings_path().parent_path();
    } catch (...) {
        return {};
    }
}

RuntimeSettings get_runtime_settings() {
    s_settings_lock.lock_shared();
    const RuntimeSettings copy = s_settings;
    s_settings_lock.unlock_shared();
    return copy;
}

namespace {
// kNotAsked until the game first asks for its language, then that language's number.
constexpr std::uint8_t kNotAsked = 0xFFu;
std::atomic<std::uint8_t> s_running_language{kNotAsked};
// A bit for each GameLanguage the game's data holds (bit `value`); all until set_game_languages.
std::atomic<std::uint32_t> s_game_languages{~0u};

// The language a language folder holds, whatever its release ("UsEnglish", "EuEnglish": English); Auto for none.
GameLanguage folder_language(std::string_view folder) noexcept {
    static constexpr std::pair<std::string_view, GameLanguage> kNames[] = {
        {"Japanese", GameLanguage::Japanese}, {"English", GameLanguage::English}, {"German", GameLanguage::German},
        {"French", GameLanguage::French},     {"Spanish", GameLanguage::Spanish}, {"Italian", GameLanguage::Italian},
        {"Dutch", GameLanguage::Dutch}};
    if (folder.size() < 3u) return GameLanguage::Auto;
    for (const auto& [name, language] : kNames) {
        if (folder.substr(2) == name) return language;
    }
    return GameLanguage::Auto;
}

// The system's language as a game language (Auto: none of the game's).
GameLanguage system_language() noexcept {
#if defined(_WIN32)
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_JAPANESE: return GameLanguage::Japanese;
    case LANG_ENGLISH: return GameLanguage::English;
    case LANG_GERMAN: return GameLanguage::German;
    case LANG_FRENCH: return GameLanguage::French;
    case LANG_SPANISH: return GameLanguage::Spanish;
    case LANG_ITALIAN: return GameLanguage::Italian;
    case LANG_DUTCH: return GameLanguage::Dutch;
    default: return GameLanguage::Auto;
    }
#else
    // POSIX locales ("de_DE.UTF-8"): LC_ALL, LC_MESSAGES, then LANG.
    const char* locale = nullptr;
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        locale = std::getenv(name);
        if (locale != nullptr && locale[0] != '\0') break;
    }
    if (locale == nullptr) return GameLanguage::Auto;
    const std::string_view code(locale, std::min<std::size_t>(std::strlen(locale), 2u));
    static constexpr std::pair<std::string_view, GameLanguage> kCodes[] = {
        {"ja", GameLanguage::Japanese}, {"en", GameLanguage::English}, {"de", GameLanguage::German},
        {"fr", GameLanguage::French},   {"es", GameLanguage::Spanish}, {"it", GameLanguage::Italian},
        {"nl", GameLanguage::Dutch}};
    for (const auto& [prefix, language] : kCodes) {
        if (code == prefix) return language;
    }
    return GameLanguage::Auto;
#endif
}
}  // namespace

void set_game_languages(std::span<const std::string> folders) noexcept {
    std::uint32_t bits = 0u;
    for (const std::string& folder : folders) {
        if (const GameLanguage language = folder_language(folder); language != GameLanguage::Auto) {
            bits |= 1u << static_cast<unsigned>(language);
        }
    }
    // No language folder the game knows (not one it plays): English, as the game's own fallback.
    s_game_languages.store(bits != 0u ? bits : 1u << static_cast<unsigned>(GameLanguage::English),
                           std::memory_order_release);
}

bool game_language_available(GameLanguage language) noexcept {
    return language != GameLanguage::Auto &&
           (s_game_languages.load(std::memory_order_acquire) >> static_cast<unsigned>(language) & 1u) != 0u;
}

GameLanguage game_language_resolved(GameLanguage setting) noexcept {
    if (game_language_available(setting)) return setting;
    if (const GameLanguage system = system_language(); game_language_available(system)) return system;
    if (game_language_available(GameLanguage::English)) return GameLanguage::English;
    for (const GameLanguage language : kGameLanguages) {
        if (game_language_available(language)) return language;
    }
    return GameLanguage::English;
}

GameLanguage game_language_for_boot() noexcept {
    std::uint8_t running = s_running_language.load(std::memory_order_acquire);
    if (running == kNotAsked) {
        const auto chosen = static_cast<std::uint8_t>(game_language_resolved(get_runtime_settings().language));
        // An exchange that fails with the value still kNotAsked has chosen nothing: it is not an answer.
        std::uint8_t expected = kNotAsked;
        while (!s_running_language.compare_exchange_weak(expected, chosen, std::memory_order_acq_rel) &&
               expected == kNotAsked) {
        }
        running = expected != kNotAsked ? expected : chosen;
    }
    return static_cast<GameLanguage>(running);
}

bool running_game_language(GameLanguage& language) noexcept {
    const std::uint8_t running = s_running_language.load(std::memory_order_acquire);
    if (running == kNotAsked) return false;
    language = static_cast<GameLanguage>(running);
    return true;
}

const char* sound_output_key(SoundOutput output) noexcept {
    switch (output) {
    case SoundOutput::Mono: return "mono";
    case SoundOutput::Surround: return "surround";
    default: return "stereo";
    }
}

const char* button_icons_key(ButtonIcons icons) noexcept {
    static constexpr const char* kKeys[] = {"auto", "game", "xbox", "nintendo", "playstation", "keyboard"};
    return kKeys[std::min<std::size_t>(static_cast<std::size_t>(icons), std::size(kKeys) - 1u)];
}

const char* focus_loss_key(FocusLoss focus_loss) noexcept {
    static constexpr const char* kKeys[] = {"keep", "pause", "mute"};
    return kKeys[std::min<std::size_t>(static_cast<std::size_t>(focus_loss), std::size(kKeys) - 1u)];
}

namespace {
std::atomic<bool> s_background_muted{false};
}  // namespace

void set_background_muted(bool muted) noexcept {
    s_background_muted.store(muted, std::memory_order_relaxed);
}

bool background_muted() noexcept {
    return s_background_muted.load(std::memory_order_relaxed);
}

const char* game_language_key(GameLanguage language) noexcept {
    switch (language) {
    case GameLanguage::Japanese: return "japanese";
    case GameLanguage::German: return "german";
    case GameLanguage::French: return "french";
    case GameLanguage::Spanish: return "spanish";
    case GameLanguage::Italian: return "italian";
    case GameLanguage::Dutch: return "dutch";
    case GameLanguage::Auto: return "auto";
    case GameLanguage::English: break;
    }
    return "english";
}

RuntimeInputModeState get_runtime_input_mode_state() {
    s_settings_lock.lock_shared();
    const RuntimeInputModeState copy{
        s_settings.input_mode,
        s_input_mode_generation,
    };
    s_settings_lock.unlock_shared();
    return copy;
}

void set_runtime_settings(const RuntimeSettings& settings) {
    const RuntimeSettings sanitized = sanitize_settings(settings);
    s_settings_lock.lock();
    if (s_settings.input_mode != sanitized.input_mode) {
        if (s_input_mode_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
            s_settings_lock.unlock();
            throw std::overflow_error(
                "runtime input-mode generation overflow");
        }
        ++s_input_mode_generation;
    }
    s_settings = sanitized;
    s_settings_lock.unlock();
}

namespace {

// A running session never changes its input source because the file changed: a real Wii Remote, or a
// controller that is not connected, would fail it. The source the file names (Nebula Settings may have saved
// a new one) is written back by the game's saves unless the game changed its own source since the file was
// last read or written. Guarded by s_settings_lock.
RuntimeInputMode s_file_input_mode = RuntimeInputMode::KeyboardMouse;
RuntimeInputMode s_input_mode_at_sync = RuntimeInputMode::KeyboardMouse;

void record_input_mode_sync(RuntimeInputMode file_mode, RuntimeInputMode live_mode) {
    s_settings_lock.lock();
    s_file_input_mode = file_mode;
    s_input_mode_at_sync = live_mode;
    s_settings_lock.unlock();
}

}  // namespace

void load_runtime_settings_from_file() {
    RuntimeSettings settings{};
    input::ControlsConfig controls = input::default_controls_config();
    parse_settings_file(settings, controls);
    const RuntimeInputMode file_mode = sanitize_settings(settings).input_mode;
    apply_env_overrides(settings);
    set_runtime_settings(settings);
    input::set_controls_config(controls);
    record_input_mode_sync(file_mode, get_runtime_settings().input_mode);
}

bool save_runtime_settings_to_file() {
    try {
        const RuntimeSettings settings = get_runtime_settings();
        const std::filesystem::path path = settings_path();
        if (path.empty()) {
            return false;
        }
        s_settings_lock.lock_shared();
        const RuntimeInputMode input_mode =
            settings.input_mode == s_input_mode_at_sync ? s_file_input_mode : settings.input_mode;
        s_settings_lock.unlock_shared();

        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents << "input_mode=" << runtime_input_mode_name(input_mode)
                 << '\n'
                 << "controls_version=" << input::kControlsVersion << '\n'
                 << std::setprecision(std::numeric_limits<float>::max_digits10)
                 << "audio_master_gain=" << settings.audio.master << '\n'
                 << "audio_music_gain=" << settings.audio.music << '\n'
                 << "audio_sfx_gain=" << settings.audio.sfx << '\n'
                 << "audio_voice_gain=" << settings.audio.voice << '\n'
                 << "audio_ambience_gain=" << settings.audio.ambience << '\n'
                 << "audio_cutscene_gain=" << settings.audio.cutscene << '\n'
                 << "audio_wiimote_speaker_gain=" << settings.audio.wiimote_speaker << '\n'
                 << "audio_sound_output=" << sound_output_key(settings.audio.sound_output) << '\n'
                 << "text_speed=" << settings.text_speed << '\n'
                 << "language=" << game_language_key(settings.language) << '\n'
                 << "discord_status=" << (settings.discord_status ? 1 : 0) << '\n'
                 << "button_icons=" << button_icons_key(settings.button_icons) << '\n'
                 << "menu_text_size=" << settings.menu_text_size << '\n'
                 << "focus_loss=" << focus_loss_key(settings.focus_loss) << '\n'
                 << "co_star=" << (settings.co_star ? 1 : 0) << '\n';
        // Co-op isn't playable yet: its keys appear once someone sets them.
        const RuntimeCoopSettings defaults{};
        if (settings.coop.port != defaults.port) contents << "coop_port=" << settings.coop.port << '\n';
        if (settings.coop.max_players != defaults.max_players) {
            contents << "coop_max_players=" << settings.coop.max_players << '\n';
        }
        if (!settings.coop.player_name.empty()) contents << "coop_player_name=" << settings.coop.player_name << '\n';
        if (!settings.coop.password.empty()) contents << "coop_password=" << settings.coop.password << '\n';
        if (settings.coop.character != defaults.character) {
            contents << "coop_character=" << coop_character_key(settings.coop.character) << '\n';
        }
        if (!settings.coop.join_address.empty()) {
            contents << "coop_join_address=" << settings.coop.join_address << '\n';
        }
        if (settings.coop.nebulanet != defaults.nebulanet) contents << "coop_network=direct\n";
        if (!settings.coop.nebulanet_server.empty()) {
            contents << "coop_nebulanet=" << settings.coop.nebulanet_server << '\n';
        }
        if (settings.coop.player_interaction != defaults.player_interaction) {
            contents << "coop_player_interaction=" << coop_player_interaction_key(settings.coop.player_interaction)
                     << '\n';
        }
        if (settings.coop.knockback_strength != defaults.knockback_strength) {
            contents << "coop_knockback_strength=" << settings.coop.knockback_strength << '\n';
        }
        if (settings.coop.pvp_mode != defaults.pvp_mode) {
            contents << "coop_pvp_mode=" << coop_pvp_mode_key(settings.coop.pvp_mode) << '\n';
        }
        if (settings.coop.bubble_death != defaults.bubble_death) {
            contents << "coop_bubble_death=" << (settings.coop.bubble_death ? 1 : 0) << '\n';
        }
        if (settings.coop.cheats != defaults.cheats) contents << "coop_cheats=" << (settings.coop.cheats ? 1 : 0) << '\n';
        if (settings.coop.star_counter != defaults.star_counter) {
            contents << "coop_star_counter=" << (settings.coop.star_counter ? 1 : 0) << '\n';
        }
        // The pop-ups this player turned off (each is on by default).
        RuntimeCoopPopups popups = settings.coop_popups;
        for (const auto& [name, flag] : coop_popup_keys(popups)) {
            if (!*flag) contents << "coop_popup_" << name << "=0" << '\n';
        }
        for (const auto& [key, list] : {std::pair{"coop_bans", &settings.coop_bans},
                                        std::pair{"coop_moderators", &settings.coop_moderators}}) {
            if (list->empty()) continue;
            contents << key << '=';
            for (std::size_t i = 0; i < list->size(); ++i) contents << (i == 0u ? "" : ",") << (*list)[i];
            contents << '\n';
        }
        contents << input::format_controls_settings(input::get_controls_config());
        if (!contents) {
            return false;
        }
        if (!io::write_file_atomically(path, contents.str())) {
            return false;
        }
        record_input_mode_sync(input_mode, settings.input_mode);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_coop_endpoint(std::string_view text, std::string& host, std::uint16_t& port) {
    text = trim(text);
    std::string_view name;
    std::uint16_t number = kCoopDefaultPort;
    if (!text.empty() && text.front() == '[') {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        name = text.substr(1u, close - 1u);
        const std::string_view rest = text.substr(close + 1u);
        if (!rest.empty() && (rest.front() != ':' || !parse_port(rest.substr(1u), number))) {
            return false;
        }
    } else {
        const std::size_t colon = text.rfind(':');
        if (colon != std::string_view::npos && text.find(':') != colon) {
            name = text;  // a bare IPv6 address: the default port
        } else if (colon != std::string_view::npos) {
            name = text.substr(0u, colon);
            if (!parse_port(text.substr(colon + 1u), number)) {
                return false;
            }
        } else {
            name = text;
        }
    }
    if (name.empty() || name.find_first_of(" \t[]/") != std::string_view::npos) {
        return false;
    }
    host.assign(name);
    port = number;
    return true;
}

const char* coop_player_interaction_key(CoopPlayerInteraction interaction) noexcept {
    switch (interaction) {
    case CoopPlayerInteraction::NonSolid: return "nonsolid";
    case CoopPlayerInteraction::FriendlyFire: return "friendly_fire";
    case CoopPlayerInteraction::Solid: break;
    }
    return "solid";
}

const char* coop_pvp_mode_key(CoopPvpMode mode) noexcept {
    return mode == CoopPvpMode::Revamped ? "revamped" : "classic";
}

const char* coop_character_key(CoopCharacter character) noexcept {
    return character == CoopCharacter::Luigi ? "luigi" : "mario";
}

const char* runtime_input_mode_name(RuntimeInputMode mode) {
    switch (mode) {
    case RuntimeInputMode::KeyboardMouse:
        return "keyboard_mouse";
    case RuntimeInputMode::Controller:
        return "controller";
    case RuntimeInputMode::RealWiimote:
        return "real_wiimote";
    case RuntimeInputMode::WiimoteMotion:
        return "wiimote_motion";
    case RuntimeInputMode::Phone:
        return "phone";
    case RuntimeInputMode::Auto:
    default:
        return "auto";
    }
}

}  // namespace galaxy
