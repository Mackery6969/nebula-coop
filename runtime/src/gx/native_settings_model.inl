// SPDX-License-Identifier: GPL-3.0-only
// Nebula's in-game settings menu, part one: its state, the HOME Menu, the pages' drafts and the keys (inside the
// window file's anonymous namespace).
// host_window_win32.cpp and platform/sdl/host_window_sdl.cpp include it, the first in its place among the window's
// own code; neither builds alone.


#if defined(GALAXY_WITH_STREAMLINE)
constexpr bool kStreamlineBuilt = true;
#else
constexpr bool kStreamlineBuilt = false;
#endif

// DLSS offered before it is installed (defined below).
bool dlss_offered();
bool dlss_installed_now();
bool frame_generation_offered();
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live);
bool dlss_prompt_key(WPARAM key, bool was_down);
bool mods_restart_key(WPARAM key, bool was_down);
bool mods_page_key(WPARAM key, bool accept, bool was_down);
void end_settings_text(bool keep);
unsigned mods_draft_changes();
void commit_mods_draft();
void forget_mods_scan();
void revert_mods_draft();
void dlss_dismiss_prompt();
void poll_dlss_download();
std::string dlss_footer(std::uint64_t now_ms, bool& error);
NativeSettingsStatus dlss_status(std::uint64_t now_ms);
constexpr int kDlssDownloadMegabytes = 31;  // NVIDIA's DLSS files, as downloaded

// Settings pages built from rows (DISPLAY, GRAPHICS, GAME, PLAYER, SYSTEM, ONLINE, MODS: settings_page): a label,
// the value for the draft, how left/right change the draft (none: the row is fixed) and a help line; a
// heading starts a section and is never selected. CONTROLS and AUDIO keep their own rows.
struct SettingsRow {
    std::string label;
    std::string value;
    std::function<void(RenderConfig&, int)> change;
    std::string help;
    bool heading = false;
    std::string key;  // names the row across rebuilds (a mod's buttons)
    std::uint8_t names = 0u;  // the mod's own words, shown as they are: 1 its label, 2 its value
    // A row of buttons (nothing to change): their words, and what pressing the one at an index does. Left and right
    // move between them; A, Space or a click presses one (Right too when there is only one).
    std::vector<std::string> buttons;
    std::function<void(RenderConfig&, unsigned)> press;
    std::uint8_t list = 0u;  // the MODS page's lists: 1 off, 2 on (a mod, or the list's title)
    bool held = false;       // a mod picked up to move
    // A setting along a range (a slider): its steps from the first value and where it is.
    std::uint16_t slider_steps = 0u;
    std::uint16_t slider_at = 0u;
    bool text = false;  // a text (the player's name): pressed, it is edited in place
};

// A row of buttons; its value is their words with '|' between them (NEBULA_SETTINGS_ROW_BUTTONS).
SettingsRow button_row(std::string label, std::vector<std::string> buttons, std::function<void(RenderConfig&, unsigned)> press,
                       std::string help) {
    SettingsRow row{std::move(label), {}, nullptr, std::move(help)};
    for (const std::string& word : buttons) {
        row.value += (row.value.empty() ? "" : "|") + word;
    }
    row.buttons = std::move(buttons);
    row.press = std::move(press);
    return row;
}
std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps);
bool settings_page_tab(unsigned tab) { return tab != 2u && tab != 3u; }

// The temporal upscaler this configuration runs (2 FSR 3, 3 DLSS), 0 none.
unsigned temporal_upscaler(const RenderConfig& render) {
    if (render.upscaler == 2u && fsr3_available()) return 2u;
    if (render.upscaler == 3u && streamline::dlss_supported()) return 3u;
    return 0u;
}

// The upscaler choices this runtime and machine offer, in menu order.
std::vector<unsigned> available_upscalers() {
    std::vector<unsigned> choices{0u, 1u};
    if (fsr3_available()) choices.push_back(2u);
    if (galaxy::gx::streamline::dlss_supported() || dlss_offered()) choices.push_back(3u);
    return choices;
}

// Output scale divided by the quality ratio (native, 1.5, 1.7, 2, 3), at least 1x.
unsigned quality_render_scale(unsigned output_scale, unsigned quality) {
    static constexpr float kRatio[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    const long scale = std::lround(static_cast<float>(output_scale) / kRatio[std::min(quality, 4u)]);
    return std::clamp(static_cast<unsigned>(std::max(scale, 1l)), 1u, std::max(output_scale, 1u));
}

// Bilinear or FSR 1 with a quality mode: the render scale follows the output like FSR 3/DLSS.
bool spatial_quality_mode(const RenderConfig& render) {
    return temporal_upscaler(render) == 0u && std::min(render.spatial_quality, 4u) >= 1u;
}

// DLSS's narrowest render scale for a quality and output scale (its mode's range starts above
// ultra performance's 3x ratio at low outputs).
unsigned dlss_min_render_scale(unsigned quality, unsigned output_scale) {
    static std::mutex mutex;
    static unsigned cached_quality = ~0u;
    static unsigned cached_output = 0u;
    static unsigned cached_scale = 1u;
    const std::lock_guard<std::mutex> lock(mutex);
    if (quality != cached_quality || output_scale != cached_output) {
        const unsigned width = dlss_min_render_width(quality, kEfbWidth * output_scale, kEfbHeight * output_scale);
        cached_quality = quality;
        cached_output = output_scale;
        cached_scale = std::max((width + kEfbWidth - 1u) / kEfbWidth, 1u);
    }
    return cached_scale;
}

// The render scale the upscaling settings give below the internal resolution `internal`.
unsigned upscaler_render_scale(const RenderConfig& render, unsigned internal) {
    const unsigned temporal = temporal_upscaler(render);
    if (temporal == 0u) {
        return spatial_quality_mode(render) ? quality_render_scale(internal, render.spatial_quality) : internal;
    }
    const unsigned quality = std::min(render.temporal_quality, 4u);
    unsigned scale = quality_render_scale(internal, quality);
    if (temporal == 3u) {
        scale = std::max(scale, dlss_min_render_scale(quality, internal));
    }
    return std::clamp(scale, 1u, std::max(internal, 1u));
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps);

std::atomic<bool> s_pointer_focused{false};
galaxy::host::HostPointerSnapshotCache s_pointer_snapshot;
std::atomic<int> s_pointer_last_cursor_poll_x{std::numeric_limits<int>::min()};
std::atomic<int> s_pointer_last_cursor_poll_y{std::numeric_limits<int>::min()};
std::atomic<bool> s_pointer_left_button{false};
std::atomic<bool> s_pointer_right_button{false};
std::atomic<bool> s_pointer_focus_valid{false};
galaxy::host::HostPointerEventRing s_pointer_transitions;
bool s_tracking_mouse_leave = false;
std::atomic<bool> s_system_menu_active{false};
std::recursive_mutex s_native_settings_mutex;

std::atomic<bool> s_native_settings_visible{false};
// The HOME Menu (home_menu_open): its list, or "are you sure?" for reset and leaving; the selected answer; the choice
// the game polls (-1 none yet). The game draws it once it reads home_menu_view; draw_home_menu is the fallback. The
// window, simulation and render threads change it under s_home_menu_mutex.
std::mutex s_home_menu_mutex;
std::atomic<bool> s_home_menu_visible{false};
std::atomic<unsigned> s_home_menu_row{0};
std::atomic<int> s_home_menu_asking{0};  // the choice asked about: 0 none (the list), 1 leave, 2 reset
std::atomic<int> s_home_menu_choice{-1};
std::atomic<bool> s_home_menu_drawn_by_game{false};
constexpr unsigned kHomeMenuRows = 3;
// The list top to bottom and the choice each makes: return to the game, reset, leave (the console's Wii Menu).
constexpr const char* kHomeMenuRowText[kHomeMenuRows] = {"RETURN TO GAME", "RESET", "QUIT TO DESKTOP"};
constexpr int kHomeMenuRowChoice[kHomeMenuRows] = {0, 2, 1};
// "Are you sure?": no (selected first), then yes, as the game's own questions place them.
constexpr unsigned kHomeMenuAnswers = 2;
constexpr const char* kHomeMenuAnswerText[kHomeMenuAnswers] = {"NO", "YES"};

unsigned home_menu_answer_count() {
    return s_home_menu_asking.load(std::memory_order_relaxed) != 0 ? kHomeMenuAnswers : kHomeMenuRows;
}

void close_home_menu_with(int choice) {
    s_home_menu_choice.store(choice, std::memory_order_release);
    s_home_menu_visible.store(false, std::memory_order_release);
}

// Back (Escape, B, no): from the question to its row in the list, from the list to the game.
void back_home_menu() {
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    if (asking == 0) {
        close_home_menu_with(0);
        return;
    }
    s_home_menu_asking.store(0, std::memory_order_relaxed);
    for (unsigned row = 0; row < kHomeMenuRows; ++row) {
        if (kHomeMenuRowChoice[row] == asking) {
            s_home_menu_row.store(row, std::memory_order_relaxed);
        }
    }
}

// An answer chosen (Enter, A, a click): returning to the game closes at once; reset and leaving ask first.
void choose_home_menu_answer(unsigned answer) {
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    if (asking != 0) {
        if (answer == 1u) {
            close_home_menu_with(asking);
        } else {
            back_home_menu();
        }
        return;
    }
    const int choice = kHomeMenuRowChoice[answer % kHomeMenuRows];
    if (choice == 0) {
        close_home_menu_with(0);
        return;
    }
    s_home_menu_asking.store(choice, std::memory_order_relaxed);
    s_home_menu_row.store(0u, std::memory_order_relaxed);
}

// Keys while the HOME Menu is open (controllers arrive as keys too: the D-pad as the arrows, A as kAcceptKey, Y as
// Return, B and Menu as Escape). Every key is the menu's: the game and the settings menu wait.
bool handle_home_menu_key(WPARAM key, bool was_down) {
    if (was_down && key != VK_UP && key != VK_DOWN && key != VK_LEFT && key != VK_RIGHT) {
        return true;
    }
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    const unsigned count = home_menu_answer_count();
    const unsigned row = s_home_menu_row.load(std::memory_order_relaxed) % count;
    switch (key) {
    case VK_UP:
    case VK_LEFT:
        s_home_menu_row.store((row + count - 1u) % count, std::memory_order_relaxed);
        break;
    case VK_DOWN:
    case VK_RIGHT:
        s_home_menu_row.store((row + 1u) % count, std::memory_order_relaxed);
        break;
    case VK_RETURN:
    case VK_SPACE:
    case galaxy::input::settings_pad::kKeyAccept:
        choose_home_menu_answer(row);
        break;
    case VK_ESCAPE:
    case VK_BACK:
        back_home_menu();
        break;
    case VK_HOME:
        close_home_menu_with(0);
        break;
    default:
        break;
    }
    return true;
}
std::atomic<bool> s_native_settings_dirty{false};
std::atomic<bool> s_native_settings_save_error{false};
std::atomic<unsigned> s_native_settings_tab{0};
std::atomic<unsigned> s_native_settings_row{0};
// The selected row's selected button, for the page and row it was chosen on (page << 16 | row); selecting a row
// starts it at its first.
std::atomic<unsigned> s_native_settings_button{0};
std::atomic<unsigned> s_native_settings_button_row{~0u};
// The native game draws the menu (it read native_settings_view): the overlay draws nothing of its own.
std::atomic<bool> s_settings_drawn_by_game{false};
// Tab (1) and Escape (2) down in the window (coop_keys_held).
std::atomic<unsigned> s_coop_keys_held{0u};
// Leaving with changes not applied asks first: apply them, discard them, or stay. Under s_native_settings_mutex.
bool s_settings_leave_asking = false;
// Apply saved mod changes this session did not start with: the menu asks whether to restart now.
std::atomic<bool> s_mods_restart_ask{false};
// The pages, in order.
constexpr const char* kNativeSettingsTabNames[] = {"DISPLAY", "GRAPHICS", "CONTROLS", "AUDIO", "GENERAL", "PLAYER",
                                                   "SYSTEM", "ONLINE", "MODS"};
// The PLAYER page's name as it is typed (galaxy::coop::TextEdit), into s_runtime_draft once kept; none while no text is
// edited. Under s_native_settings_mutex; s_settings_typing says whether one is, for the window's typing. A text started
// with Space drops the space typed with it (s_settings_text_swallow, until then).
std::optional<galaxy::coop::TextEdit> s_settings_text;
std::atomic<bool> s_settings_typing{false};
std::uint64_t s_settings_text_swallow = 0u;

// Settings menu draft. Display and graphics rows edit s_settings_draft, the game's settings (the audio mix, text
// speed, button icons, language, Discord status) s_runtime_draft; Enter (Y on a controller) applies them, Backspace
// (X) reverts them and closing the menu discards them. A display change (monitor, mode, resolution, refresh rate)
// stays only when it is kept within kDisplayConfirmMs. All guarded by s_native_settings_mutex.
RenderConfig s_settings_draft{};
RuntimeSettings s_runtime_draft{};
RuntimeSettings s_runtime_base{};  // the game's settings when s_runtime_draft was taken

void reset_runtime_draft() {
    s_runtime_draft = get_runtime_settings();
    s_runtime_base = s_runtime_draft;
}
std::vector<MonitorInfo> s_settings_monitors;
std::uint64_t s_display_confirm_deadline_ms = 0u;
RenderConfig s_display_confirm_previous{};
std::uint64_t s_settings_applied_ms = 0u;
constexpr std::uint64_t kDisplayConfirmMs = 15000u;

// The display driver's monitors and modes, read at start and on WM_DISPLAYCHANGE: enumerating them can take tens of
// milliseconds, which opening the settings menu (under the lock the render thread takes) must not wait on.
std::mutex s_monitor_cache_mutex;
std::vector<MonitorInfo> s_monitor_cache;

void store_monitor_cache(std::vector<MonitorInfo> monitors) {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    s_monitor_cache = std::move(monitors);
}

std::vector<MonitorInfo> cached_monitors() {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    if (s_monitor_cache.empty()) {
        // Before the renderer has started (tests), the list is read now.
        s_monitor_cache = enumerate_monitors();
    }
    return s_monitor_cache;
}

// CONTROLS page (tab 2): a draft of the bindings/tuning and of the input source, applied together with the other
// pages. The edited device (keyboard and mouse or an attached controller) selects the scheme whose table the rows
// show. A binding row waiting for an input is s_capture_target (kWiiTargetCount: none). Guarded by
// s_native_settings_mutex.
constexpr unsigned kControlsTab = 2u;
constexpr unsigned kGeneralTab = 4u;
constexpr unsigned kPlayerTab = 5u;
constexpr unsigned kSystemTab = 6u;
constexpr unsigned kOnlineTab = 7u;
constexpr unsigned kModsTab = 8u;
galaxy::input::ControlsConfig s_controls_draft = galaxy::input::default_controls_config();
RuntimeInputMode s_input_mode_draft = RuntimeInputMode::Auto;
galaxy::input::ControlScheme s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
galaxy::input::DeviceKey s_controls_device{};
galaxy::input::DeviceList s_controls_devices{};
std::uint64_t s_controls_devices_ms = 0u;
// Tests stand in for the attached controllers.
std::optional<std::vector<galaxy::input::ControllerInfo>> s_test_controllers;
std::optional<galaxy::input::DeviceKey> s_test_active_device;
std::size_t s_capture_target = galaxy::input::kWiiTargetCount;
std::uint64_t s_capture_deadline_ms = 0u;
constexpr std::uint64_t kCaptureTimeoutMs = 6000u;
std::string s_controls_message;
std::uint64_t s_controls_message_until_ms = 0u;
unsigned s_settings_first_row = 0u;
// The window thread's left/right-resolved code for the key being handled.
std::uint32_t s_handled_key_normalized = 0u;

constexpr unsigned kNativeSettingsTabCount = 9;

// The GENERAL page's languages, in the order LEFT/RIGHT steps through them.
constexpr GameLanguage kGameLanguages[] = {GameLanguage::English, GameLanguage::French, GameLanguage::Spanish};

// Each language in its own name; the UTF-8 Ç and Ñ are drawn by ui_text.
const char* game_language_name(GameLanguage language) {
    switch (language) {
    case GameLanguage::French: return "FRAN\xC3\x87" "AIS";
    case GameLanguage::Spanish: return "ESPA\xC3\x91" "OL";
    case GameLanguage::English: break;
    }
    return "ENGLISH";
}

// Rows the panel shows at once; longer pages scroll.
constexpr unsigned kNativeSettingsVisibleRows = 9u;

unsigned native_settings_first_row(unsigned first, unsigned selected, unsigned count) {
    if (count <= kNativeSettingsVisibleRows) return 0u;
    if (selected < first) first = selected;
    if (selected >= first + kNativeSettingsVisibleRows) first = selected + 1u - kNativeSettingsVisibleRows;
    return std::min(first, count - kNativeSettingsVisibleRows);
}

// Writes both settings files when something changed. Each atomic replace flushes the disk, which a busy or
// scanned disk can hold for tens of milliseconds and more.
bool write_dirty_settings_files() {
    if (!s_native_settings_dirty.exchange(false, std::memory_order_acq_rel)) {
        return true;
    }
    bool runtime_saved = false;
    bool render_saved = false;
    try {
        runtime_saved = save_runtime_settings_to_file();
    } catch (...) {
        runtime_saved = false;
    }
    try {
        render_saved = save_render_config_to_file();
    } catch (...) {
        render_saved = false;
    }
    if (!runtime_saved || !render_saved) {
        s_native_settings_dirty.store(true, std::memory_order_release);
        const bool already_reported =
            s_native_settings_save_error.exchange(
                true,
                std::memory_order_acq_rel);
        if (!already_reported) {
            std::cerr << "[settings] save failed; kept for retry\n";
        }
        return false;
    }
    s_native_settings_save_error.store(false, std::memory_order_release);
    return true;
}

// Saves run on a worker: the menu, the window thread and the render thread hold the settings lock while they
// ask for one, and the render thread takes that lock every frame the menu is open.
std::mutex s_settings_save_mutex;
std::condition_variable s_settings_save_changed;
bool s_settings_save_requested = false;  // guarded by s_settings_save_mutex
bool s_settings_save_running = false;    // guarded by s_settings_save_mutex

void settings_save_worker() {
    SetThreadDescription(GetCurrentThread(), L"Nebula Settings Save");
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(s_settings_save_mutex);
            s_settings_save_changed.wait(lock, [] { return s_settings_save_requested; });
            s_settings_save_requested = false;
            s_settings_save_running = true;
        }
        (void)write_dirty_settings_files();
        {
            const std::lock_guard<std::mutex> lock(s_settings_save_mutex);
            s_settings_save_running = false;
        }
        s_settings_save_changed.notify_all();
    }
}

bool settings_save_pending() {
    const std::lock_guard<std::mutex> lock(s_settings_save_mutex);
    return s_settings_save_requested || s_settings_save_running;
}

float step_gain(float current, int delta) {
    const float snapped = std::round(current * 20.0f) / 20.0f;
    return std::clamp(snapped + static_cast<float>(delta) * 0.05f, 0.0f, 2.0f);
}

bool display_settings_differ(const RenderConfig& a, const RenderConfig& b) {
    return a.display_mode != b.display_mode || a.monitor != b.monitor ||
        a.fullscreen_width != b.fullscreen_width ||
        a.fullscreen_height != b.fullscreen_height ||
        a.fullscreen_refresh_hz != b.fullscreen_refresh_hz ||
        (a.display_mode == kDisplayWindowed &&
         (a.window_width != b.window_width || a.window_height != b.window_height));
}

const MonitorInfo* settings_monitor(const RenderConfig& render);

// The refresh rate the menu assumes in place of the display's (tests; 0 in the game).
std::atomic<unsigned> s_refresh_override_hz{0u};

// The refresh rate the picture will be shown at with these settings.
unsigned settings_refresh_hz(const RenderConfig& render) {
    if (const unsigned hz = s_refresh_override_hz.load(std::memory_order_relaxed); hz != 0u) {
        return hz;
    }
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayExclusive && render.fullscreen_refresh_hz != 0u) {
        return render.fullscreen_refresh_hz;
    }
    return monitor != nullptr ? monitor->current.refresh_hz : 0u;
}

// GRAPHICS > FRAME RATE. INTERPOLATION OFF presents the game's own frames (FRAME RATE LIMIT 30 or 60); NATIVE adds
// Nebula's interpolated frames up to the TARGET FRAME RATE; DLSS FRAME GENERATION has NVIDIA's generated frames reach
// it. The target is max_fps (0: the display's refresh rate); frame_generation selects DLSS.
enum class Interpolation : unsigned { kOff, kNative, kFrameGeneration };

Interpolation interpolation_of(const RenderConfig& render) {
    if (render.frame_generation != 0u) {
        return Interpolation::kFrameGeneration;
    }
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    return rate != 0u && rate <= 61u ? Interpolation::kOff : Interpolation::kNative;
}

// The targets a mode offers, lowest first (0: the display's refresh rate): the standard rates
// below the display's, then its own. With a temporal upscaler native interpolation runs at
// multiples of 60, as does frame generation without dynamic multi frame generation (and at most
// the multiplier the GPU runs).
std::vector<unsigned> interpolation_targets(const RenderConfig& render, Interpolation mode) {
    std::vector<unsigned> targets;
    if (mode == Interpolation::kOff) {
        return targets;
    }
    const unsigned refresh = settings_refresh_hz(render);
    const bool generated = mode == Interpolation::kFrameGeneration;
    // NVIDIA's fixed frame generation makes whole multiples of the game's 60.
    const bool sixties = generated && streamline::frame_generation_supported() && !streamline::frame_generation_dynamic();
    const unsigned most = generated && streamline::frame_generation_supported()
        ? 60u * (streamline::frame_generation_max() + 1u) : 1000u;
    for (const unsigned rate : {120u, 144u, 165u, 180u, 240u, 360u}) {
        if (refresh != 0u && rate + 1u >= refresh) {
            break;
        }
        if ((rate == 180u && !sixties) || (sixties && rate % 60u != 0u) || rate > most) {
            continue;
        }
        targets.push_back(rate);
    }
    // Its own rate: above 60, and with the multiples of 60 only where it is one (frame generation
    // takes the largest multiple at or below it).
    const bool own = refresh == 0u || (refresh > 61u && (!sixties || generated || refresh % 60u <= 1u ||
                                                          refresh % 60u >= 59u));
    if (own) {
        targets.push_back(0u);
    }
    return targets;
}

std::vector<Interpolation> interpolation_modes(const RenderConfig& render) {
    std::vector<Interpolation> modes{Interpolation::kOff};
    if (!interpolation_targets(render, Interpolation::kNative).empty()) {
        modes.push_back(Interpolation::kNative);
    }
    if (frame_generation_offered() && !interpolation_targets(render, Interpolation::kFrameGeneration).empty()) {
        modes.push_back(Interpolation::kFrameGeneration);
    }
    return modes;
}

// Keeps the draft's target one its mode offers: the same, else the highest at or below it (the
// display's rate counting as its refresh), else the lowest offered; a mode with none turns off.
void normalize_interpolation(RenderConfig& render) {
    const Interpolation mode = interpolation_of(render);
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    if (mode == Interpolation::kOff) {
        render.max_fps = rate != 0u && rate <= 30u ? 30.0f : 60.0f;
        return;
    }
    const auto targets = interpolation_targets(render, mode);
    if (targets.empty()) {
        render.frame_generation = 0u;
        render.max_fps = 60.0f;
        return;
    }
    if (std::find(targets.begin(), targets.end(), rate) != targets.end()) {
        return;
    }
    const unsigned refresh = settings_refresh_hz(render);
    const unsigned wanted = rate == 0u ? (refresh != 0u ? refresh : 1000u) : rate;
    unsigned chosen = targets.front();
    for (const unsigned target : targets) {
        const unsigned value = target == 0u ? (refresh != 0u ? refresh : 1000u) : target;
        if (value <= wanted) {
            chosen = target;
        }
    }
    render.max_fps = static_cast<float>(chosen);
}

const MonitorInfo* settings_monitor(const RenderConfig& render) {
    return find_monitor(s_settings_monitors, widen_utf8(render.monitor.data()));
}

// The picture size the settings target: the fullscreen mode, the monitor
// (borderless) or the window.
std::pair<unsigned, unsigned> settings_output_size(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    const unsigned desktop_w = monitor ? monitor->current.width : host_window::backbuffer_width;
    const unsigned desktop_h = monitor ? monitor->current.height : host_window::backbuffer_height;
    switch (render.display_mode) {
    case kDisplayExclusive:
        return render.fullscreen_width != 0u && render.fullscreen_height != 0u
            ? std::pair{render.fullscreen_width, render.fullscreen_height}
            : std::pair{desktop_w, desktop_h};
    case kDisplayBorderless:
        return {desktop_w, desktop_h};
    default:
        return {render.window_width, render.window_height};
    }
}

// The largest internal resolution the selected display can show.
unsigned settings_scale_cap(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    unsigned width = monitor ? monitor->current.width : host_window::backbuffer_width;
    unsigned height = monitor ? monitor->current.height : host_window::backbuffer_height;
    if (render.display_mode == kDisplayExclusive && render.fullscreen_width != 0u) {
        width = render.fullscreen_width;
        height = render.fullscreen_height;
    }
    return efb_scale_covering(width, height, kMaxEfbScale);
}

// Window client sizes that fit the monitor's work area, smallest first: those
// of a fixed aspect (aspect_window_sizes), or common sizes and the monitor's
// own resolutions.
std::vector<std::pair<unsigned, unsigned>> settings_window_sizes(
    const MonitorInfo* monitor, unsigned aspect_mode) {
    static constexpr std::pair<unsigned, unsigned> kCommon[] = {
        {854u, 480u}, {1024u, 768u}, {1280u, 720u}, {1280u, 800u}, {1366u, 768u},
        {1440u, 900u}, {1600u, 900u}, {1680u, 1050u}, {1920u, 1080u}, {1920u, 1200u},
        {2560u, 1080u}, {2560u, 1440u}, {2560u, 1600u}, {3440u, 1440u}, {3840u, 1080u},
        {3840u, 2160u}, {5120u, 1440u}, {5120u, 2160u}, {5120u, 2880u}, {7680u, 4320u}};
    long max_w = 7680;
    long max_h = 4320;
    if (monitor != nullptr) {
        RECT frame{0, 0, 0, 0};
#if defined(_WIN32)
        const UINT dpi = std::max<UINT>(96u, GetDpiForSystem());
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, dpi);
#endif
        max_w = (monitor->work_area.right - monitor->work_area.left) - (frame.right - frame.left);
        max_h = (monitor->work_area.bottom - monitor->work_area.top) - (frame.bottom - frame.top);
    }
    const unsigned aspect = std::min(aspect_mode, kAspectModeCount - 1u);
    if (aspect != 0u) {
        auto sizes = aspect_window_sizes(aspect, static_cast<unsigned>(std::max(max_w, 0L)),
                                         static_cast<unsigned>(std::max(max_h, 0L)));
        if (sizes.empty()) {
            // Nothing fits: offer the smallest.
            sizes = aspect_window_sizes(aspect, std::numeric_limits<unsigned>::max(), 360u);
        }
        return sizes;
    }
    std::vector<std::pair<unsigned, unsigned>> sizes;
    const auto add = [&](unsigned w, unsigned h) {
        if (static_cast<long>(w) <= max_w && static_cast<long>(h) <= max_h &&
            std::find(sizes.begin(), sizes.end(), std::pair{w, h}) == sizes.end()) {
            sizes.emplace_back(w, h);
        }
    };
    for (const auto& [w, h] : kCommon) add(w, h);
    if (monitor != nullptr) {
        for (const auto& [w, h] : monitor_resolutions(*monitor)) add(w, h);
    }
    if (sizes.empty()) sizes.emplace_back(854u, 480u);
    std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
        return std::uint64_t{a.first} * a.second < std::uint64_t{b.first} * b.second;
    });
    return sizes;
}

// Fixed aspect, windowed: the smallest listed size at least as tall as the
// current window, or the largest one.
void snap_window_size(RenderConfig& render) {
    const auto sizes = settings_window_sizes(settings_monitor(render), render.aspect_mode);
    auto chosen = sizes.back();
    for (const auto& size : sizes) {
        if (size.second >= render.window_height) {
            chosen = size;
            break;
        }
    }
    render.window_width = chosen.first;
    render.window_height = chosen.second;
}

// Keeps the draft inside what its display allows.
void sanitize_settings_draft(RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayWindowed) {
        const auto sizes = settings_window_sizes(monitor, render.aspect_mode);
        const auto& largest = sizes.back();
        if (render.aspect_mode != 0u &&
            (render.window_width > largest.first || render.window_height > largest.second)) {
            snap_window_size(render);
        } else {
            render.window_width = std::min(render.window_width, largest.first);
            render.window_height = std::min(render.window_height, largest.second);
        }
    }
    render.efb_scale = std::clamp(render.efb_scale, 1u, settings_scale_cap(render));
}

unsigned controls_changes();

// The LIGHTING row: 0 per vertex, 1 per pixel, 2 enhanced.
unsigned lighting_mode(const RenderConfig& render) {
    return render.enhanced_lighting ? 2u : render.per_pixel_lighting ? 1u : 0u;
}

// The game's settings the menu changed since it took them (its input mode is the controls draft's), each merged into
// `live` when given: a setting changed elsewhere meanwhile keeps that value unless the menu changed it too.
unsigned runtime_draft_changes(RuntimeSettings* live) {
    const RuntimeSettings& d = s_runtime_draft;
    const RuntimeSettings& b = s_runtime_base;
    // A setting added to RuntimeSettings is drafted here too: these bindings stop compiling until it is.
    // coop_bans and coop_moderators aren't the menu's (the room's /permban and /moderator keep them): never drafted.
    [[maybe_unused]] const auto& [mode, audio, text_speed, language, discord_status, button_icons, coop, coop_bans,
                                  coop_moderators] = d;
    [[maybe_unused]] const auto& [master, music, sfx, voice, ambience, cutscene, wiimote_speaker] = d.audio;
    RuntimeSettings scratch{};
    RuntimeSettings& out = live != nullptr ? *live : scratch;
    unsigned changes = 0u;
    const auto take = [&changes](const auto& draft, const auto& base, auto& into) {
        if (draft != base) {
            into = draft;
            ++changes;
        }
    };
    take(d.audio.master, b.audio.master, out.audio.master);
    take(d.audio.music, b.audio.music, out.audio.music);
    take(d.audio.sfx, b.audio.sfx, out.audio.sfx);
    take(d.audio.voice, b.audio.voice, out.audio.voice);
    take(d.audio.ambience, b.audio.ambience, out.audio.ambience);
    take(d.audio.cutscene, b.audio.cutscene, out.audio.cutscene);
    take(d.audio.wiimote_speaker, b.audio.wiimote_speaker, out.audio.wiimote_speaker);
    take(d.text_speed, b.text_speed, out.text_speed);
    take(d.language, b.language, out.language);
    take(d.discord_status, b.discord_status, out.discord_status);
    take(d.button_icons, b.button_icons, out.button_icons);
    take(d.coop, b.coop, out.coop);
    return changes;
}

unsigned pending_settings_changes() {
    const RenderConfig live = get_render_config();
    const RenderConfig& d = s_settings_draft;
    unsigned changes = 0u;
    changes += d.display_mode != live.display_mode;
    changes += d.monitor != live.monitor;
    changes += d.display_mode == kDisplayExclusive &&
        (d.fullscreen_width != live.fullscreen_width ||
         d.fullscreen_height != live.fullscreen_height);
    changes += d.display_mode == kDisplayExclusive &&
        d.fullscreen_refresh_hz != live.fullscreen_refresh_hz;
    changes += d.display_mode == kDisplayWindowed &&
        (d.window_width != live.window_width || d.window_height != live.window_height);
    changes += d.aspect_mode != live.aspect_mode;
    changes += d.internal_follow_window != live.internal_follow_window ||
        (!d.internal_follow_window && d.efb_scale != live.efb_scale);
    changes += d.vsync_interval != live.vsync_interval;
    changes += d.max_fps != live.max_fps;
    changes += d.frame_generation != live.frame_generation;
    changes += d.aa_mode != live.aa_mode;
    changes += d.smaa_quality != live.smaa_quality;
    changes += d.msaa_samples != live.msaa_samples;
    changes += d.upscaler != live.upscaler;
    changes += d.temporal_quality != live.temporal_quality;
    changes += d.spatial_quality != live.spatial_quality;
    changes += d.sharpness != live.sharpness;
    changes += d.anisotropic_filtering != live.anisotropic_filtering;
    changes += d.enhanced_mipmaps != live.enhanced_mipmaps;
    changes += d.curved_surfaces != live.curved_surfaces;
    changes += lighting_mode(d) != lighting_mode(live);
    changes += d.ambient_occlusion != live.ambient_occlusion;
    changes += d.bloom_mode != live.bloom_mode;
    changes += d.bloom != live.bloom;
    changes += d.hud_output_resolution != live.hud_output_resolution;
    changes += d.ray_tracing_enabled != live.ray_tracing_enabled;
    changes += d.rt_quality != live.rt_quality;
    changes += d.rt_reflections != live.rt_reflections;
    changes += d.rt_water != live.rt_water;
    changes += d.rt_sun_rays != live.rt_sun_rays;
    changes += d.rt_lens_bloom != live.rt_lens_bloom;
    changes += d.texture_packs != live.texture_packs ||
        (d.texture_packs && d.texture_pack_preload != live.texture_pack_preload);
    changes += d.texture_dump != live.texture_dump;
    changes += controls_changes();
    changes += runtime_draft_changes(nullptr);
    changes += mods_draft_changes();
    return changes;
}

enum class ControlsRow {
    InputMode,
    Device,
    Binding,
    PointerSpeed,
    InvertPointerY,
    StickDeadzone,
    PointerDeadzone,
    PointerHide,
    Vibration,
    VibrationStrength,
    MotionShake,
    ShakeSensitivity,
    GyroPointer,
    GyroSensitivity,
    PairJoyCon,
    Reset,
};

struct ControlsRowInfo {
    ControlsRow kind = ControlsRow::Reset;
    galaxy::input::WiiTarget target = galaxy::input::WiiTarget::A;
};

constexpr unsigned kControlsBindingRow0 = 2u;

// The Pair Joy-Con screen (CONTROLS > PAIR JOY-CON): open while the controller worker pairs the halves whose L and R
// are held; it closes a moment after a pair is made, or with Escape or B.
bool s_joycon_pairing = false;
std::uint32_t s_joycon_paired_at_open = 0u;
std::uint64_t s_joycon_paired_ms = 0u;
std::uint64_t s_joycon_opened_ms = 0u;
constexpr std::uint64_t kJoyConPairedShowMs = 1500u;

// The Joy-Con halves as the controller worker has them. GALAXY_TEST_JOYCON=<bits>[,<bits>@<ms>...] (NEBULA_JOYCON_*
// bits, each from that many milliseconds after the Pair Joy-Con screen opens) stands in for them: screenshots and
// recordings of the screen without Joy-Con, 16 making a pair.
galaxy::host::JoyConStatus joycon_status_now() {
    struct Step {
        int bits = 0;
        std::uint64_t at_ms = 0;
    };
    static const std::vector<Step> test = [] {
        std::vector<Step> steps;
        char value[128]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_JOYCON") != 0 || length <= 1u) return steps;
        for (const char* p = value; *p != '\0';) {
            Step step;
            step.bits = std::atoi(p);
            while (*p != '\0' && *p != '@' && *p != ',') ++p;
            if (*p == '@') step.at_ms = static_cast<std::uint64_t>(std::atoll(++p));
            while (*p != '\0' && *p != ',') ++p;
            if (*p == ',') ++p;
            steps.push_back(step);
        }
        return steps;
    }();
    if (test.empty()) {
        return galaxy::host::joycon_status();
    }
    const std::uint64_t since = s_joycon_pairing ? GetTickCount64() - s_joycon_opened_ms : 0u;
    int bits = test.front().bits;
    for (const Step& step : test) {
        if (step.at_ms <= since) bits = step.bits;
    }
    galaxy::host::JoyConStatus status{};
    status.lefts = (bits & kJoyConLeftBit) != 0 ? 1u : 0u;
    status.rights = (bits & kJoyConRightBit) != 0 ? 1u : 0u;
    status.left_held = (bits & kJoyConLeftHeldBit) != 0;
    status.right_held = (bits & kJoyConRightHeldBit) != 0;
    status.paired = (bits & kJoyConPairedBit) != 0 ? 1u : 0u;
    status.left_presses = static_cast<std::uint8_t>(bits >> kJoyConLeftPressesShift & 15);
    status.right_presses = static_cast<std::uint8_t>(bits >> kJoyConRightPressesShift & 15);
    return status;
}

void open_joycon_pairing() {
    s_joycon_pairing = true;
    s_joycon_opened_ms = GetTickCount64();
    s_joycon_paired_at_open = joycon_status_now().paired;
    s_joycon_paired_ms = 0u;
    galaxy::host::set_joycon_pairing(true);
    std::cerr << "[settings] pair joy-con\n";
}

void close_joycon_pairing() {
    if (!s_joycon_pairing) return;
    s_joycon_pairing = false;
    galaxy::host::set_joycon_pairing(false);
}

std::vector<ControlsRow> controls_tail_rows() {
    using galaxy::input::scheme_has_motion;
    using galaxy::input::scheme_is_gamepad;
    std::vector<ControlsRow> rows{ControlsRow::PointerSpeed, ControlsRow::InvertPointerY};
    if (scheme_is_gamepad(s_controls_scheme)) {
        rows.push_back(ControlsRow::StickDeadzone);
        rows.push_back(ControlsRow::PointerDeadzone);
        rows.push_back(ControlsRow::PointerHide);
        rows.push_back(ControlsRow::Vibration);
        rows.push_back(ControlsRow::VibrationStrength);
    }
    if (scheme_has_motion(s_controls_scheme)) {
        rows.push_back(ControlsRow::MotionShake);
        rows.push_back(ControlsRow::ShakeSensitivity);
        rows.push_back(ControlsRow::GyroPointer);
        rows.push_back(ControlsRow::GyroSensitivity);
    }
    // Pairing Joy-Con is on every page (its place never moves as halves come and go). Windows reads them over HID.
#if defined(_WIN32)
    rows.push_back(ControlsRow::PairJoyCon);
#endif
    rows.push_back(ControlsRow::Reset);
    return rows;
}

void refresh_controls_devices(bool force);

unsigned controls_row_count() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    refresh_controls_devices(false);
    return kControlsBindingRow0 + static_cast<unsigned>(galaxy::input::kWiiTargetCount) +
           static_cast<unsigned>(controls_tail_rows().size());
}

ControlsRowInfo controls_row(unsigned row) {
    ControlsRowInfo info{};
    if (row == 0u) {
        info.kind = ControlsRow::InputMode;
    } else if (row == 1u) {
        info.kind = ControlsRow::Device;
    } else if (row - kControlsBindingRow0 < galaxy::input::kWiiTargetCount) {
        // The jump/back row follows A and B.
        using galaxy::input::WiiTarget;
        const unsigned index = row - kControlsBindingRow0;
        info.kind = ControlsRow::Binding;
        info.target = index == 2u ? WiiTarget::JumpBack
            : index > 2u ? static_cast<WiiTarget>(index - 1u)
            : static_cast<WiiTarget>(index);
    } else {
        const auto tail = controls_tail_rows();
        const std::size_t index = row - kControlsBindingRow0 - galaxy::input::kWiiTargetCount;
        info.kind = index < tail.size() ? tail[index] : ControlsRow::Reset;
    }
    return info;
}

unsigned controls_changes() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    unsigned changes = 0u;
    for (std::size_t i = 0; i < galaxy::input::kControlSchemeCount; ++i) {
        for (std::size_t t = 0; t < galaxy::input::kWiiTargetCount; ++t) {
            changes += s_controls_draft.tables[i].targets[t] != live.tables[i].targets[t];
        }
        changes += !(s_controls_draft.tuning[i] == live.tuning[i]);
    }
    changes += s_input_mode_draft != get_runtime_settings().input_mode;
    return changes;
}

void show_controls_message(const char* message) {
    s_controls_message = message;
    s_controls_message_until_ms = GetTickCount64() + 2500u;
}

bool controls_capturing() {
    return s_capture_target < galaxy::input::kWiiTargetCount;
}

void end_controls_capture() {
    if (controls_capturing()) {
        s_capture_target = galaxy::input::kWiiTargetCount;
        galaxy::host::cancel_controller_binding_capture();
    }
}

void start_controls_capture(galaxy::input::WiiTarget target) {
    s_capture_target = static_cast<std::size_t>(target);
    s_capture_deadline_ms = GetTickCount64() + kCaptureTimeoutMs;
    if (galaxy::input::scheme_is_gamepad(s_controls_scheme)) {
        galaxy::host::arm_controller_binding_capture(s_controls_scheme, s_controls_device.slot);
    }
}

void finish_controls_capture(galaxy::input::InputSource source) {
    if (!controls_capturing()) {
        return;
    }
    const auto target = static_cast<galaxy::input::WiiTarget>(s_capture_target);
    end_controls_capture();
    if (!galaxy::input::binding_source_allowed(s_controls_scheme, target, source)) {
        show_controls_message("THAT INPUT CANNOT DRIVE THIS ROW");
        return;
    }
    galaxy::input::add_binding(s_controls_draft.table(s_controls_scheme)[target], source);
    std::cerr << "[settings] bind " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
              << galaxy::input::wii_target_id(target) << " = "
              << galaxy::input::format_target_bindings(
                     s_controls_scheme, s_controls_draft.table(s_controls_scheme)[target])
              << '\n';
}

void expire_controls_capture() {
    if (controls_capturing() && GetTickCount64() >= s_capture_deadline_ms) {
        end_controls_capture();
    }
}

constexpr std::uint64_t kControlsDevicesRefreshMs = 500u;

// Rebuilds the device list from what is attached and keeps the edited device
// through hot plug (a controller of the same family takes over, else the
// keyboard and mouse).
void refresh_controls_devices(bool force) {
    const std::uint64_t now = GetTickCount64();
    if (!force && s_controls_devices.count != 0u && now - s_controls_devices_ms < kControlsDevicesRefreshMs) {
        return;
    }
    s_controls_devices_ms = now;
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    std::size_t count = 0;
    std::optional<galaxy::input::DeviceKey> active;
    if (s_test_controllers) {
        count = std::min(s_test_controllers->size(), connected.size());
        std::copy_n(s_test_controllers->begin(), count, connected.begin());
        active = s_test_active_device;
    } else {
        count = galaxy::host::connected_controllers(connected);
        active = galaxy::host::last_input_device();
    }
    galaxy::input::build_device_list(std::span<const galaxy::input::ControllerInfo>(connected.data(), count),
                                     active, s_controls_devices);
    const galaxy::input::DeviceKey edited =
        s_controls_devices.entries[galaxy::input::resolve_device(s_controls_devices, s_controls_device)].key;
    if (!(edited == s_controls_device)) {
        end_controls_capture();
        s_controls_device = edited;
        s_controls_scheme = edited.scheme;
    }
}

void reset_controls_draft() {
    end_controls_capture();
    s_controls_draft = galaxy::input::get_controls_config();
    s_input_mode_draft = get_runtime_settings().input_mode;
}

unsigned native_settings_row_count(unsigned tab) {
    tab %= kNativeSettingsTabCount;
    if (settings_page_tab(tab)) {
        return static_cast<unsigned>(
            settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps()).size());
    }
    return tab == 2u ? controls_row_count() : 7u;
}

// A heading row (never selected).
bool native_settings_heading(unsigned tab, unsigned row) {
    tab %= kNativeSettingsTabCount;
    if (!settings_page_tab(tab)) {
        return false;
    }
    const auto page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    return row < page.size() && page[row].heading;
}

bool settings_display_confirming() {
    return s_display_confirm_deadline_ms != 0u;
}

void keep_display_settings() {
    s_display_confirm_deadline_ms = 0u;
    std::cerr << "[settings] display kept\n";
}

// Returns the display to the settings it had before the last apply; the
// other settings of that apply stay.
void revert_display_settings() {
    if (!settings_display_confirming()) {
        return;
    }
    s_display_confirm_deadline_ms = 0u;
    RenderConfig render = get_render_config();
    const RenderConfig& previous = s_display_confirm_previous;
    render.display_mode = previous.display_mode;
    render.monitor = previous.monitor;
    render.fullscreen_width = previous.fullscreen_width;
    render.fullscreen_height = previous.fullscreen_height;
    render.fullscreen_refresh_hz = previous.fullscreen_refresh_hz;
    render.window_width = previous.window_width;
    render.window_height = previous.window_height;
    set_render_config(render);
    s_settings_draft.display_mode = render.display_mode;
    s_settings_draft.monitor = render.monitor;
    s_settings_draft.fullscreen_width = render.fullscreen_width;
    s_settings_draft.fullscreen_height = render.fullscreen_height;
    s_settings_draft.fullscreen_refresh_hz = render.fullscreen_refresh_hz;
    s_settings_draft.window_width = render.window_width;
    s_settings_draft.window_height = render.window_height;
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    std::cerr << "[settings] display reverted\n";
}

void expire_display_confirmation() {
    if (settings_display_confirming() && GetTickCount64() >= s_display_confirm_deadline_ms) {
        revert_display_settings();
    }
}

// Applies the CONTROLS draft. Controller input is selected only while a
// controller is attached.
void apply_controls_draft() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    if (!(s_controls_draft == live)) {
        galaxy::input::set_controls_config(s_controls_draft);
        s_controls_draft = galaxy::input::get_controls_config();
    }
    RuntimeSettings runtime = get_runtime_settings();
    if (s_input_mode_draft != runtime.input_mode) {
        if (s_input_mode_draft == RuntimeInputMode::Controller && !galaxy::host::controller_present_now()) {
            show_controls_message("NO CONTROLLER CONNECTED");
            s_input_mode_draft = runtime.input_mode;
        } else {
            runtime.input_mode = s_input_mode_draft;
            set_runtime_settings(runtime);
        }
    }
}

void apply_native_settings_draft() {
    if (pending_settings_changes() == 0u) {
        return;
    }
    apply_controls_draft();
    RenderConfig next = s_settings_draft;
    sanitize_settings_draft(next);
    const RenderConfig live = get_render_config();
    // Settings the menu does not edit keep their live values.
    RenderConfig applied = live;
    applied.display_mode = next.display_mode;
    applied.monitor = next.monitor;
    applied.fullscreen_width = next.fullscreen_width;
    applied.fullscreen_height = next.fullscreen_height;
    applied.fullscreen_refresh_hz = next.fullscreen_refresh_hz;
    applied.window_width = next.window_width;
    applied.window_height = next.window_height;
    applied.aspect_mode = next.aspect_mode;
    applied.internal_follow_window = next.internal_follow_window;
    applied.efb_scale = next.efb_scale;
    applied.vsync_interval = next.vsync_interval;
    applied.max_fps = next.max_fps;
    applied.frame_generation = next.frame_generation;
    applied.aa_mode = next.aa_mode;
    applied.smaa_quality = next.smaa_quality;
    applied.msaa_samples = next.msaa_samples;
    applied.upscaler = next.upscaler;
    const bool dlss_asked = dlss_hold_back(applied, live);
    applied.temporal_quality = next.temporal_quality;
    applied.spatial_quality = next.spatial_quality;
    applied.sharpness = next.sharpness;
    applied.anisotropic_filtering = next.anisotropic_filtering;
    applied.enhanced_mipmaps = next.enhanced_mipmaps;
    applied.curved_surfaces = next.curved_surfaces;
    applied.per_pixel_lighting = next.per_pixel_lighting;
    applied.enhanced_lighting = next.enhanced_lighting;
    applied.ambient_occlusion = next.ambient_occlusion;
    applied.bloom_mode = next.bloom_mode;
    applied.bloom = next.bloom;
    applied.hud_output_resolution = next.hud_output_resolution;
    applied.ray_tracing_enabled = next.ray_tracing_enabled;
    applied.rt_quality = next.rt_quality;
    applied.rt_reflections = next.rt_reflections;
    applied.rt_water = next.rt_water;
    applied.rt_sun_rays = next.rt_sun_rays;
    applied.rt_lens_bloom = next.rt_lens_bloom;
    applied.texture_packs = next.texture_packs;
    applied.texture_pack_preload = next.texture_pack_preload;
    applied.texture_dump = next.texture_dump;
    const bool display_changed = display_settings_differ(live, applied);
    const unsigned changes = pending_settings_changes();
    commit_mods_draft();
    // The game's settings the menu changed, over the live ones (the controls applied the input mode).
    RuntimeSettings runtime = get_runtime_settings();
    if (runtime_draft_changes(&runtime) != 0u) {
        set_runtime_settings(runtime);
    }
    if (applied.internal_follow_window) {
        // The draft's scale is the one the menu opened with; the live scale
        // may have followed the window since.
        applied.efb_scale = live.efb_scale;
    }
    set_render_config(applied);
    if (!applied.internal_follow_window) {
        stage_next_start_efb_scale(applied.efb_scale);
    } else {
        host_window::follow_window_rearm.store(true, std::memory_order_release);
    }
    s_settings_draft = get_render_config();
    reset_runtime_draft();
    if (dlss_asked) {
        s_settings_draft.upscaler = 3u;  // the row shows what the question is about
    }
    if (display_changed) {
        if (!settings_display_confirming()) {
            s_display_confirm_previous = live;
        }
        s_display_confirm_deadline_ms = GetTickCount64() + kDisplayConfirmMs;
    }
    s_settings_applied_ms = GetTickCount64();
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    std::cerr << "[settings] applied " << changes << " change"
              << (changes == 1u ? "" : "s") << '\n';
}

void revert_native_settings_draft() {
    s_settings_draft = get_render_config();
    reset_runtime_draft();
    reset_controls_draft();
    revert_mods_draft();
    std::cerr << "[settings] reverted\n";
}

// Every overlay change is logged with its new row text.
void adjust_native_setting_unlogged(int delta);

void adjust_native_setting(int delta) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    const auto text = [tab, row] {
        return native_settings_row_text(tab, row, s_settings_draft,
                                        s_runtime_draft,
                                        get_render_feature_caps());
    };
    const std::string before = text();
    adjust_native_setting_unlogged(delta);
    const std::string after = text();
    if (after != before) {
        std::cerr << "[settings] " << after << '\n';
    }
}

// What a step of a setting can change: the drafts, the settings that change at once, and the row's text (for what lives
// elsewhere, as the galaxy chosen to travel to). Two values may read the same (anti-aliasing's modes while MSAA runs).
struct SettingsStepState {
    RenderConfig render;
    RuntimeSettings runtime;
    galaxy::input::ControlsConfig controls;
    RuntimeInputMode input_mode;
    galaxy::input::DeviceKey device;
    std::string text;
    friend bool operator==(const SettingsStepState&, const SettingsStepState&) = default;
};

// A step that goes round at the ends (the pointer's arrows): on, or where it can go no further, back to the other end.
void step_native_setting_round(int direction) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    // A binding (on: wait for an input) and the reset (on: the defaults) are no lists: stepped as the keys step them.
    if (tab == kControlsTab &&
        (controls_row(row).kind == ControlsRow::Binding || controls_row(row).kind == ControlsRow::Reset ||
         controls_row(row).kind == ControlsRow::PairJoyCon)) {
        adjust_native_setting(direction);
        return;
    }
    const auto state = [tab, row] {
        return SettingsStepState{s_settings_draft, s_runtime_draft, s_controls_draft, s_input_mode_draft,
                                 s_controls_device,
                                 native_settings_row_text(tab, row, s_settings_draft, s_runtime_draft,
                                                          get_render_feature_caps())};
    };
    const SettingsStepState before = state();
    adjust_native_setting_unlogged(direction);
    if (state() == before) {
        // At its end: stepped back as far as it goes.
        for (unsigned guard = 0u; guard < 256u; ++guard) {
            const SettingsStepState was = state();
            adjust_native_setting_unlogged(-direction);
            if (state() == was) break;
        }
    }
    const SettingsStepState after = state();
    if (after != before) {
        std::cerr << "[settings] " << after.text << '\n';
    }
}

void adjust_native_setting_unlogged(int delta) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (delta == 0 || settings_display_confirming()) {
        return;
    }
    RenderConfig& draft = s_settings_draft;
    const auto step = [delta](unsigned value, unsigned count) {
        return static_cast<unsigned>(std::clamp(
            static_cast<int>(value) + delta, 0, static_cast<int>(count) - 1));
    };

    if (settings_page_tab(tab)) {
        const auto page = settings_page(tab, draft, s_runtime_draft, get_render_feature_caps());
        if (row < page.size() && page[row].change) {
            page[row].change(draft, delta);
        }
        return;
    }

    RuntimeSettings& settings = s_runtime_draft;
    if (tab == kControlsTab) {
        using galaxy::input::ControlScheme;
        galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
        const ControlsRowInfo info = controls_row(row);
        const auto step_float = [delta](float value, float increment, float lo, float hi) {
            const float snapped = std::round(value / increment) * increment;
            return std::clamp(snapped + static_cast<float>(delta) * increment, lo, hi);
        };
        switch (info.kind) {
        case ControlsRow::InputMode:
            // A physical Wii Remote is chosen in the launcher only.
            if (s_input_mode_draft == RuntimeInputMode::RealWiimote) return;
            {
                static constexpr RuntimeInputMode kModes[] = {
                    RuntimeInputMode::Auto, RuntimeInputMode::KeyboardMouse, RuntimeInputMode::Controller};
                unsigned index = 0u;
                for (unsigned i = 0; i < 3u; ++i) {
                    if (kModes[i] == s_input_mode_draft) index = i;
                }
                s_input_mode_draft = kModes[step(index, 3u)];
            }
            return;
        case ControlsRow::Device: {
            refresh_controls_devices(true);
            if (s_controls_devices.count == 0u) return;
            const unsigned current = static_cast<unsigned>(
                galaxy::input::resolve_device(s_controls_devices, s_controls_device));
            const unsigned next = step(current, static_cast<unsigned>(s_controls_devices.count));
            s_controls_device = s_controls_devices.entries[next].key;
            s_controls_scheme = s_controls_device.scheme;
            return;
        }
        case ControlsRow::Binding:
            if (delta > 0) start_controls_capture(info.target);
            return;
        case ControlsRow::PointerSpeed:
            tuning.pointer_speed = step_float(tuning.pointer_speed, 0.2f, 0.2f, 6.0f);
            return;
        case ControlsRow::InvertPointerY:
            tuning.invert_pointer_y = !tuning.invert_pointer_y;
            return;
        case ControlsRow::StickDeadzone:
            tuning.stick_deadzone = step_float(tuning.stick_deadzone, 0.02f, 0.0f, 0.5f);
            return;
        case ControlsRow::PointerDeadzone:
            tuning.pointer_deadzone = step_float(tuning.pointer_deadzone, 0.02f, 0.0f, 0.5f);
            return;
        case ControlsRow::PointerHide:
            tuning.pointer_hide_seconds = step_float(tuning.pointer_hide_seconds, 1.0f, 0.0f, 30.0f);
            return;
        case ControlsRow::Vibration:
            tuning.vibration = !tuning.vibration;
            return;
        case ControlsRow::VibrationStrength: {
            // 10..100 percent in steps of 10 (rumble.h).
            const unsigned index = std::min(9u, (std::max<unsigned>(tuning.rumble, 10u) - 5u) / 10u);
            tuning.rumble = static_cast<std::uint8_t>(10u + step(index, 10u) * 10u);
            return;
        }
        case ControlsRow::MotionShake:
            tuning.motion_shake = static_cast<galaxy::input::MotionShake>(
                step(static_cast<unsigned>(tuning.motion_shake), 3u));
            return;
        case ControlsRow::ShakeSensitivity:
            tuning.shake_sensitivity = static_cast<std::uint8_t>(
                step(tuning.shake_sensitivity - 1u, galaxy::input::kShakeSensitivitySteps) + 1u);
            return;
        case ControlsRow::GyroPointer:
            tuning.gyro_pointer = !tuning.gyro_pointer;
            return;
        case ControlsRow::GyroSensitivity:
            tuning.gyro_sensitivity = step_float(tuning.gyro_sensitivity, 0.1f, 0.2f, 5.0f);
            return;
        case ControlsRow::PairJoyCon:
            if (delta > 0) open_joycon_pairing();
            return;
        case ControlsRow::Reset:
            if (delta > 0) {
                s_controls_draft.table(s_controls_scheme) =
                    galaxy::input::default_binding_table(s_controls_scheme);
                s_controls_draft.tune(s_controls_scheme) =
                    galaxy::input::default_scheme_tuning(s_controls_scheme);
            }
            return;
        }
        return;
    }
    if (tab == 3u) {
        float* const gains[] = {&settings.audio.master, &settings.audio.music,
                                &settings.audio.sfx, &settings.audio.voice,
                                &settings.audio.ambience, &settings.audio.cutscene,
                                &settings.audio.wiimote_speaker};
        if (row >= std::size(gains)) return;
        *gains[row] = step_gain(*gains[row], delta);
    }
}

void move_native_setting_row(int delta) {
    end_controls_capture();
    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    if (count == 0u) {
        s_native_settings_row.store(0u, std::memory_order_relaxed);
        return;
    }
    int row = static_cast<int>(s_native_settings_row.load(std::memory_order_relaxed));
    for (unsigned tries = 0u; tries < count; ++tries) {
        row = (row + delta) % static_cast<int>(count);
        if (row < 0) {
            row += static_cast<int>(count);
        }
        if (!native_settings_heading(tab, static_cast<unsigned>(row))) {
            break;
        }
    }
    s_native_settings_row.store(static_cast<unsigned>(row), std::memory_order_relaxed);
}

// The selection never rests on a heading or past the page's end.
void settle_native_settings_row() {
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    unsigned row = std::min(s_native_settings_row.load(std::memory_order_relaxed), count == 0u ? 0u : count - 1u);
    while (row + 1u < count && native_settings_heading(tab, row)) {
        ++row;
    }
    s_native_settings_row.store(row, std::memory_order_relaxed);
}

void cycle_native_settings_tab(int delta) {
    end_controls_capture();
    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
    s_settings_first_row = 0u;
    int tab = static_cast<int>(
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount);
    tab = (tab + delta) % static_cast<int>(kNativeSettingsTabCount);
    if (tab < 0) {
        tab += static_cast<int>(kNativeSettingsTabCount);
    }
    s_native_settings_tab.store(
        static_cast<unsigned>(tab), std::memory_order_relaxed);
    unsigned first = 0u;
    const unsigned count = native_settings_row_count(static_cast<unsigned>(tab));
    while (first + 1u < count && native_settings_heading(static_cast<unsigned>(tab), first)) {
        ++first;
    }
    s_native_settings_row.store(first, std::memory_order_relaxed);
}

void set_native_settings_visible(bool visible) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_settings_leave_asking = false;
    if (visible && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        // A fresh draft, monitor list and controller list each time the menu opens.
        s_settings_monitors = cached_monitors();
        s_settings_draft = get_render_config();
        reset_runtime_draft();
        reset_controls_draft();
        refresh_controls_devices(true);
        // The mods as mods.ini has them now: the main menu's Host page sets which are on.
        forget_mods_scan();
        settle_native_settings_row();
    }
    if (!visible) {
        // Closing discards an unapplied draft, a name being typed, an unconfirmed display and an
        // unanswered DLSS question.
        end_settings_text(false);
        close_joycon_pairing();
        dlss_dismiss_prompt();
        revert_display_settings();
        s_settings_draft = get_render_config();
        reset_runtime_draft();
        reset_controls_draft();
        flush_native_settings_if_dirty();
    }
    if (s_native_settings_visible.exchange(visible, std::memory_order_relaxed) != visible) {
        std::cerr << (visible ? "[settings] overlay open" : "[settings] overlay closed") << std::endl;
    }
}

// Binds a mouse button while a keyboard-scheme row waits for an input.
bool native_settings_capture_mouse_button(galaxy::input::MouseButton button) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!s_native_settings_visible.load(std::memory_order_relaxed) || !controls_capturing() ||
        s_controls_scheme != galaxy::input::ControlScheme::Keyboard) {
        return false;
    }
    finish_controls_capture({galaxy::input::SourceKind::Mouse, static_cast<std::uint8_t>(button)});
    return true;
}

// Keys on the CONTROLS page that differ from the other pages: Enter/A on a
// binding row waits for an input, Backspace/X clears it, Space/Y applies.
// Returns true when the key was handled here.
bool handle_controls_key(WPARAM key, bool was_down, bool from_pad) {
    if (key == galaxy::host::kBindingCaptureKey) {
        if (const auto source = galaxy::host::take_controller_binding_capture()) {
            finish_controls_capture(*source);
        }
        return true;
    }
    expire_controls_capture();
    if (controls_capturing()) {
        if (key == VK_ESCAPE && !from_pad) {
            end_controls_capture();
        } else if (s_controls_scheme == galaxy::input::ControlScheme::Keyboard && !from_pad && !was_down) {
            const std::uint32_t vk = s_handled_key_normalized != 0u ? s_handled_key_normalized
                                                                    : static_cast<std::uint32_t>(key);
            finish_controls_capture({galaxy::input::SourceKind::Key, static_cast<std::uint8_t>(vk & 0xffu)});
        }
        return true;
    }
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    if (tab != kControlsTab) {
        return false;
    }
    const ControlsRowInfo info = controls_row(s_native_settings_row.load(std::memory_order_relaxed));
    switch (key) {
    case VK_SPACE:
        if (!was_down) apply_native_settings_draft();
        return true;
    case VK_RETURN:
        if (was_down) return true;
        if (from_pad || settings_display_confirming()) return false;  // Y applies
        if (info.kind == ControlsRow::Binding) {
            start_controls_capture(info.target);
            return true;
        }
        if (info.kind == ControlsRow::Reset || info.kind == ControlsRow::PairJoyCon) {
            adjust_native_setting(1);
            return true;
        }
        return false;
    case VK_BACK:
        if (info.kind == ControlsRow::Binding) {
            if (!was_down) {
                s_controls_draft.table(s_controls_scheme)[info.target] = {};
                std::cerr << "[settings] clear " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
                          << galaxy::input::wii_target_id(info.target) << '\n';
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

// The selected row's buttons: a page's row of buttons, or the CONTROLS page's reset.
std::size_t selected_settings_buttons() {
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (tab == kControlsTab) {
        const ControlsRow kind = controls_row(row).kind;
        return kind == ControlsRow::Reset || kind == ControlsRow::PairJoyCon ? 1u : 0u;
    }
    if (!settings_page_tab(tab)) {
        return 0u;
    }
    const std::vector<SettingsRow> page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    return row < page.size() ? page[row].buttons.size() : 0u;
}

unsigned selected_row_key() {
    return (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount) << 16u |
           s_native_settings_row.load(std::memory_order_relaxed);
}

unsigned selected_settings_button() {
    return s_native_settings_button_row.load(std::memory_order_relaxed) == selected_row_key()
               ? s_native_settings_button.load(std::memory_order_relaxed)
               : 0u;
}

void select_settings_button(unsigned button) {
    s_native_settings_button.store(button, std::memory_order_relaxed);
    s_native_settings_button_row.store(selected_row_key(), std::memory_order_relaxed);
}

// Presses the selected row's button `button`, logged with the row's label and the button's word.
void press_settings_button(unsigned button) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    select_settings_button(button);
    if (tab == kControlsTab) {
        adjust_native_setting(1);
        return;
    }
    if (!settings_page_tab(tab) || settings_display_confirming()) {
        return;
    }
    const std::vector<SettingsRow> page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    if (row >= page.size() || button >= page[row].buttons.size() || !page[row].press) {
        return;
    }
    std::cerr << "[settings] " << page[row].label << ": " << page[row].buttons[button] << '\n';
    page[row].press(s_settings_draft, button);
}

// The selected row is a text (the PLAYER page's name).
bool selected_settings_text() {
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (tab != kPlayerTab) return false;
    const std::vector<SettingsRow> page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    return row < page.size() && page[row].text;
}

// The name starts to be typed: the draft's, the cursor after it.
void start_settings_text(bool from_space) {
    s_settings_text.emplace(galaxy::coop::TextKind::PlayerName, s_runtime_draft.coop.player_name);
    s_settings_text_swallow = from_space ? GetTickCount64() + 250u : 0u;
    s_settings_typing.store(true, std::memory_order_release);
}

// The name typed kept in the draft (without the spaces around it; Apply applies it), or dropped.
void end_settings_text(bool keep) {
    if (!s_settings_text) return;
    if (keep) {
        const std::string& text = s_settings_text->text();
        const std::size_t begin = text.find_first_not_of(' ');
        s_runtime_draft.coop.player_name =
            begin == std::string::npos ? std::string() : text.substr(begin, text.find_last_not_of(' ') - begin + 1u);
    }
    s_settings_text.reset();
    s_settings_typing.store(false, std::memory_order_release);
}

// A key while the name is typed: A or Enter keeps it, B or Escape drops it, a controller's X deletes the letter at the
// cursor (Backspace the one before it), the arrows, Home and End as on the main menu's text rows (TextEdit). The rest
// waits, its character typed (native_settings_type). False for F1, which closes the menu as ever.
bool settings_text_key(WPARAM key, bool accept, bool was_down, bool from_pad) {
    if (accept || key == VK_RETURN) {
        if (!was_down) end_settings_text(true);
        return true;
    }
    galaxy::coop::TextEdit& text = *s_settings_text;
    switch (key) {
    case VK_F1: end_settings_text(false); return false;
    case VK_ESCAPE:
        if (!was_down) end_settings_text(false);
        return true;
    case VK_BACK:
        if (from_pad) {
            text.erase();
        } else {
            (void)text.type(galaxy::coop::kTextBackspace);
        }
        return true;
    case VK_DELETE: (void)text.type(galaxy::coop::kTextDelete); return true;
    case VK_LEFT: (void)text.type(galaxy::coop::kTextLeft); return true;
    case VK_RIGHT: (void)text.type(galaxy::coop::kTextRight); return true;
    case VK_UP: (void)text.type(galaxy::coop::kTextUp); return true;
    case VK_DOWN: (void)text.type(galaxy::coop::kTextDown); return true;
    case VK_HOME: (void)text.type(galaxy::coop::kTextHome); return true;
    case VK_END: (void)text.type(galaxy::coop::kTextEnd); return true;
    default: return true;
    }
}

// A setting along a range, as its slider shows it: its steps from the first value and the step it is at; no steps for
// any other row. The mix's volumes run 0 to 200% in fives, the controls' numbers as adjust_native_setting steps them.
struct SliderPlace {
    unsigned steps = 0u;
    unsigned at = 0u;
};

SliderPlace settings_slider(unsigned tab, unsigned row, const std::vector<SettingsRow>& page) {
    SliderPlace place;
    if (settings_page_tab(tab)) {
        if (row < page.size()) {
            place.steps = page[row].slider_steps;
            place.at = page[row].slider_at;
        }
        return place;
    }
    const auto along = [&place](float value, float first, float step, float last) {
        place.steps = static_cast<unsigned>(std::lround((last - first) / step));
        place.at = static_cast<unsigned>(std::clamp(std::lround((value - first) / step), 0l, static_cast<long>(place.steps)));
    };
    if (tab == 3u) {
        const RuntimeSettings& settings = s_runtime_draft;
        const float gains[] = {settings.audio.master, settings.audio.music, settings.audio.sfx, settings.audio.voice,
                               settings.audio.ambience, settings.audio.cutscene, settings.audio.wiimote_speaker};
        if (row < std::size(gains)) along(gains[row], 0.0f, 0.05f, 2.0f);
        return place;
    }
    if (tab != kControlsTab) return place;
    const galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
    switch (controls_row(row).kind) {
    case ControlsRow::PointerSpeed: along(tuning.pointer_speed, 0.2f, 0.2f, 6.0f); break;
    case ControlsRow::StickDeadzone: along(tuning.stick_deadzone, 0.0f, 0.02f, 0.5f); break;
    case ControlsRow::PointerDeadzone: along(tuning.pointer_deadzone, 0.0f, 0.02f, 0.5f); break;
    case ControlsRow::PointerHide: along(tuning.pointer_hide_seconds, 0.0f, 1.0f, 30.0f); break;
    case ControlsRow::VibrationStrength: along(static_cast<float>(tuning.rumble), 10.0f, 10.0f, 100.0f); break;
    case ControlsRow::ShakeSensitivity:
        along(static_cast<float>(tuning.shake_sensitivity), 1.0f, 1.0f, static_cast<float>(galaxy::input::kShakeSensitivitySteps));
        break;
    case ControlsRow::GyroSensitivity: along(tuning.gyro_sensitivity, 0.2f, 0.1f, 5.0f); break;
    default: break;
    }
    return place;
}

// The selected setting's slider moved to step `target` (the pointer dragging it), stepped there as Left and Right would.
void set_slider(unsigned target) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    const auto place = [&] {
        return settings_slider(tab, row, settings_page_tab(tab) ? settings_page(tab, s_settings_draft, s_runtime_draft,
                                                                                get_render_feature_caps())
                                                                  : std::vector<SettingsRow>{});
    };
    const auto text = [tab, row] {
        return native_settings_row_text(tab, row, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    };
    const std::string before = text();
    SliderPlace now = place();
    target = std::min(target, now.steps);
    if (tab == 3u && now.steps != 0u && now.at != target) {
        // A volume straight to its step.
        RuntimeSettings& settings = s_runtime_draft;
        float* const gains[] = {&settings.audio.master, &settings.audio.music,    &settings.audio.sfx,
                                &settings.audio.voice,  &settings.audio.ambience, &settings.audio.cutscene,
                                &settings.audio.wiimote_speaker};
        if (row < std::size(gains)) {
            *gains[row] = static_cast<float>(target) * 0.05f;
        }
    }
    for (unsigned guard = 0u; tab != 3u && now.steps != 0u && now.at != target && guard < 64u; ++guard) {
        adjust_native_setting_unlogged(now.at < target ? 1 : -1);
        const unsigned was = now.at;
        now = place();
        if (now.at == was) break;
    }
    const std::string after = text();
    if (after != before) {
        std::cerr << "[settings] " << after << '\n';
    }
}

// Closing: at once, or with changes not applied, after asking (apply them, discard them, or stay).
void request_native_settings_close() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (pending_settings_changes() != 0u) {
        s_settings_leave_asking = true;
        return;
    }
    set_native_settings_visible(false);
}

bool handle_native_settings_key(WPARAM key, bool was_down, bool from_pad = false) {
    if (s_home_menu_visible.load(std::memory_order_acquire)) {
        return handle_home_menu_key(key, was_down);
    }
    // A controller's A changes the selected setting, as Right does, and presses the selected button.
    const bool accept = key == galaxy::input::settings_pad::kKeyAccept;
    if (accept) {
        key = VK_RIGHT;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!from_pad) {
        galaxy::host::note_keyboard_input();
    }
    if (s_settings_text && s_native_settings_visible.load(std::memory_order_relaxed) &&
        settings_text_key(key, accept, was_down, from_pad)) {
        return true;
    }
    if (key == galaxy::host::kBindingCaptureKey && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        (void)galaxy::host::take_controller_binding_capture();
        return true;
    }
    if (s_joycon_pairing && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // The Pair Joy-Con screen: its one answer (Back) is Escape, B, A, Enter, Space, Backspace or F1; the rest
        // waits (a paired Joy-Con's L and R must not turn the pages meanwhile).
        if (!was_down && (accept || key == VK_ESCAPE || key == VK_F1 || key == VK_RETURN || key == VK_SPACE ||
                          key == VK_BACK)) {
            close_joycon_pairing();
        }
        return true;
    }
    if (s_settings_leave_asking && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // Changes not applied: Enter, Space or Y applies them and closes, Backspace or X discards them and closes,
        // Escape, B or F1 goes back to the page. Every other key waits for the answer.
        if (was_down) {
            return true;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            s_settings_leave_asking = false;
            apply_native_settings_draft();
            // A new display waits to be kept, DLSS to be downloaded and new mods to be restarted for, on the page;
            // anything else closes.
            const NativeSettingsStatus dlss = dlss_status(GetTickCount64());
            if (!settings_display_confirming() && dlss != NativeSettingsStatus::DlssAsk &&
                dlss != NativeSettingsStatus::DlssRestart && !s_mods_restart_ask.load(std::memory_order_acquire)) {
                set_native_settings_visible(false);
            }
        } else if (key == VK_BACK) {
            s_settings_leave_asking = false;
            revert_native_settings_draft();
            set_native_settings_visible(false);
        } else if (key == VK_ESCAPE || key == VK_F1) {
            s_settings_leave_asking = false;
        }
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        (dlss_prompt_key(key, was_down) || mods_restart_key(key, was_down))) {
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        handle_controls_key(key, was_down, from_pad)) {
        return true;
    }
    if (key == VK_F1) {
        if (was_down) {
            return true;
        }
        const bool visible = s_native_settings_visible.load(
            std::memory_order_relaxed);
        if (visible) {
            request_native_settings_close();
            return true;
        }
        set_native_settings_visible(true);
        return true;
    }
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return false;
    }
    expire_display_confirmation();
    if (mods_page_key(key, accept, was_down)) {
        return true;
    }
    switch (key) {
    case VK_ESCAPE:
        if (settings_display_confirming()) {
            revert_display_settings();
            return true;
        }
        request_native_settings_close();
        return true;
    case VK_RETURN:
        if (was_down) {
            return true;
        }
        if (settings_display_confirming()) {
            keep_display_settings();
        } else {
            apply_native_settings_draft();
        }
        return true;
    case VK_BACK:
        if (!was_down && !settings_display_confirming()) {
            revert_native_settings_draft();
        }
        return true;
    case VK_TAB:
    case VK_NEXT:
        cycle_native_settings_tab(1);
        return true;
    case VK_PRIOR:
        cycle_native_settings_tab(-1);
        return true;
    case VK_UP:
        move_native_setting_row(-1);
        return true;
    case VK_DOWN:
        move_native_setting_row(1);
        return true;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_SPACE: {
        // A text: A, Space or Right starts typing it.
        if (selected_settings_text()) {
            if (key != VK_LEFT && !was_down) start_settings_text(key == VK_SPACE);
            return true;
        }
        // A row of buttons: left and right move between them, A or Space presses one (Right too when it is the only
        // one); held, only the moves repeat.
        const std::size_t buttons = selected_settings_buttons();
        if (buttons == 0u) {
            if (key == VK_SPACE) return false;
            adjust_native_setting(key == VK_LEFT ? -1 : 1);
            return true;
        }
        const unsigned button = std::min<unsigned>(selected_settings_button(), static_cast<unsigned>(buttons) - 1u);
        if (key == VK_SPACE || accept || (key == VK_RIGHT && buttons == 1u)) {
            if (!was_down) press_settings_button(button);
        } else if (key == VK_LEFT) {
            select_settings_button(button == 0u ? 0u : button - 1u);
        } else {
            select_settings_button(std::min<unsigned>(button + 1u, static_cast<unsigned>(buttons) - 1u));
        }
        return true;
    }
    default:
        return false;
    }
}
