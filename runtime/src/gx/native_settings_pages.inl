// SPDX-License-Identifier: GPL-3.0-only
// Nebula's in-game settings menu, part two: the pages' rows, the mods and the restarts (inside the window file's
// anonymous namespace).
// host_window_win32.cpp and platform/sdl/host_window_sdl.cpp include it, the first in its place among the window's
// own code; neither builds alone.

// The two accented capitals the language names need, as ui_glyph codes.
constexpr char kGlyphCCedilla = '\x01';
constexpr char kGlyphNTilde = '\x02';

std::array<std::uint8_t, 7> ui_glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    switch (ch) {
    case 'A': return {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
    case 'B': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu};
    case 'C': return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu};
    case 'D': return {0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu};
    case 'E': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu};
    case 'F': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u};
    case 'G': return {0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Fu};
    case 'H': return {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
    case 'I': return {0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
    case 'J': return {0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0Cu};
    case 'K': return {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u};
    case 'L': return {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu};
    case 'M': return {0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u};
    case 'N': return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    case 'O': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
    case 'P': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u};
    case 'Q': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du};
    case 'R': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u};
    case 'S': return {0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
    case 'T': return {0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
    case 'U': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
    case 'V': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u};
    case 'W': return {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0Au};
    case 'X': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u};
    case 'Y': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u};
    case 'Z': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu};
    case '0': return {0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu};
    case '1': return {0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
    case '2': return {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu};
    case '3': return {0x1Eu, 0x01u, 0x01u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
    case '4': return {0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u};
    case '5': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x1Eu};
    case '6': return {0x06u, 0x08u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu};
    case '7': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u};
    case '8': return {0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu};
    case '9': return {0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x02u, 0x0Cu};
    case ':': return {0x00u, 0x04u, 0x04u, 0x00u, 0x04u, 0x04u, 0x00u};
    case '.': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu};
    case '-': return {0x00u, 0x00u, 0x00u, 0x1Eu, 0x00u, 0x00u, 0x00u};
    case '+': return {0x00u, 0x04u, 0x04u, 0x1Fu, 0x04u, 0x04u, 0x00u};
    case '/': return {0x01u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x10u};
    case ',': return {0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x04u, 0x08u};
    case '=': return {0x00u, 0x00u, 0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x00u};
    case '%': return {0x19u, 0x19u, 0x02u, 0x04u, 0x08u, 0x13u, 0x13u};
    case '?': return {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u};
    case '<': return {0x02u, 0x04u, 0x08u, 0x10u, 0x08u, 0x04u, 0x02u};
    case '(': return {0x02u, 0x04u, 0x08u, 0x08u, 0x08u, 0x04u, 0x02u};
    case ')': return {0x08u, 0x04u, 0x02u, 0x02u, 0x02u, 0x04u, 0x08u};
    case '>': return {0x08u, 0x04u, 0x02u, 0x01u, 0x02u, 0x04u, 0x08u};
    // Mod names and messages use these too.
    case '\'': return {0x0Cu, 0x04u, 0x08u, 0x00u, 0x00u, 0x00u, 0x00u};
    case '"': return {0x0Au, 0x0Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x00u};
    case '!': return {0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x00u, 0x04u};
    case '#': return {0x0Au, 0x0Au, 0x1Fu, 0x0Au, 0x1Fu, 0x0Au, 0x0Au};
    case '&': return {0x0Cu, 0x12u, 0x14u, 0x08u, 0x15u, 0x12u, 0x0Du};
    case '_': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x1Fu};
    case '[': return {0x0Eu, 0x08u, 0x08u, 0x08u, 0x08u, 0x08u, 0x0Eu};
    case ']': return {0x0Eu, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x0Eu};
    case '*': return {0x00u, 0x04u, 0x15u, 0x0Eu, 0x15u, 0x04u, 0x00u};
    case ';': return {0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x0Cu, 0x04u, 0x08u};
    case '@': return {0x0Eu, 0x11u, 0x17u, 0x15u, 0x17u, 0x10u, 0x0Eu};
    case '~': return {0x00u, 0x00u, 0x08u, 0x15u, 0x02u, 0x00u, 0x00u};
    case '|': return {0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
    case '\\': return {0x10u, 0x10u, 0x08u, 0x04u, 0x02u, 0x01u, 0x01u};
    case '$': return {0x04u, 0x0Fu, 0x14u, 0x0Eu, 0x05u, 0x1Eu, 0x04u};
    case '{': return {0x02u, 0x04u, 0x04u, 0x08u, 0x04u, 0x04u, 0x02u};
    case '}': return {0x08u, 0x04u, 0x04u, 0x02u, 0x04u, 0x04u, 0x08u};
    case '^': return {0x04u, 0x0Au, 0x11u, 0x00u, 0x00u, 0x00u, 0x00u};
    case '`': return {0x08u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    case kGlyphCCedilla: return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x11u, 0x0Eu, 0x04u};
    // A full N; ui_text draws the tilde above the cell.
    case kGlyphNTilde: return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    default: return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    }
}

// A drawn quad, not ClearRenderTargetView with a rect list: rect-limited clears fault inside the NVIDIA driver
// (nvwgf2umx.dll, STATUS_STACK_BUFFER_OVERRUN, driver 32.0.15.9597), a fail-fast that bypasses the process's SEH
// and terminate handlers. Full-target clears are unaffected.
void ui_rect(
    UiCanvas& renderer,
    int x,
    int y,
    int w,
    int h,
    const std::array<float, 4>& color) {
    renderer.draw_ui_solid_quad(x, y, w, h, color);
}

void ui_text(
    UiCanvas& renderer,
    int x,
    int y,
    int scale,
    const std::string& text,
    const std::array<float, 4>& color) {
    int cursor_x = x;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == ' ') {
            cursor_x += 4 * scale;
            continue;
        }
        // UTF-8 Ç/ç and Ñ/ñ (C3 87/A7, C3 91/B1); any other letter of several bytes draws as one blank cell.
        if (static_cast<unsigned char>(ch) == 0xC3u && i + 1u < text.size()) {
            const auto next = static_cast<unsigned char>(text[++i]);
            ch = next == 0x87u || next == 0xA7u ? kGlyphCCedilla
               : next == 0x91u || next == 0xB1u ? kGlyphNTilde : '\0';
        } else if (static_cast<unsigned char>(ch) >= 0xC0u) {
            while (i + 1u < text.size() && (static_cast<unsigned char>(text[i + 1u]) & 0xC0u) == 0x80u) ++i;
            ch = '\0';
        }
        const auto bitmap_of = [](const std::array<std::uint8_t, 7>& rows) {
            std::uint64_t bitmap = 0u;
            for (unsigned row = 0; row < 7u; ++row) {
                for (unsigned col = 0; col < 5u; ++col) {
                    if ((rows[row] & (1u << (4u - col))) != 0u) {
                        bitmap |= UINT64_C(1) << (row * 5u + col);
                    }
                }
            }
            return bitmap;
        };
        if (ch == kGlyphNTilde) {
            // A two-row tilde above the cell, one row clear of the N: squeezed into the cell it reads as a plain N.
            static const std::uint64_t tilde = bitmap_of({0x0Du, 0x16u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u});
            renderer.draw_ui_glyph(cursor_x, y - 3 * scale, scale, tilde, color);
        }
        renderer.draw_ui_glyph(cursor_x, y, scale, bitmap_of(ui_glyph(ch)), color);
        cursor_x += 6 * scale;
    }
}

// The width ui_text draws `text` at (a two-byte letter is one cell).
int ui_text_width(std::string_view text, int scale) {
    int width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        // A letter of several UTF-8 bytes is one cell.
        while (i + 1u < text.size() && (static_cast<unsigned char>(text[i + 1u]) & 0xC0u) == 0x80u) {
            ++i;
        }
        width += (text[i] == ' ' ? 4 : 6) * scale;
    }
    return width;
}

// `text` shortened with "..." to `max_width`, whole letters at a time: menu text never runs into its neighbour or out
// of the panel, at any window size.
std::string ui_fit_text(std::string text, int scale, int max_width) {
    if (ui_text_width(text, scale) <= max_width) return text;
    const int dots = ui_text_width("...", scale);
    while (!text.empty() && (ui_text_width(text, scale) + dots > max_width || text.back() == ' ')) {
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    return text.empty() ? std::string() : text + "...";
}

// The footer hints name Xbox buttons; a PlayStation controller in use gets its own.
std::string pad_hints(std::string text) {
    const auto device = galaxy::host::last_input_device();
    if (!device || device->scheme != galaxy::input::ControlScheme::PlayStation) {
        return text;
    }
    static constexpr std::array<std::array<const char*, 2>, 6> kNames{{
        {"VIEW+MENU OR MINUS+PLUS", "SHARE+OPTIONS"}, {"LB/RB", "L1/R1"}, {"/A ", "/CROSS "},
        {"/B ", "/CIRCLE "}, {"/X ", "/SQUARE "}, {"/Y ", "/TRIANGLE "}}};
    for (const auto& name : kNames) {
        const std::size_t from_size = std::strlen(name[0]);
        const std::size_t to_size = std::strlen(name[1]);
        for (std::size_t at = text.find(name[0]); at != std::string::npos; at = text.find(name[0], at + to_size)) {
            text.replace(at, from_size, name[1]);
        }
    }
    return text;
}

std::string percent_text(float value) {
    char text[32]{};
    std::snprintf(
        text,
        sizeof(text),
        "%u%%",
        static_cast<unsigned>(std::lround(value * 100.0f)));
    return text;
}

const char* exposed_text(const RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

// A name as the menu shows it, shortened to `width` characters (the overlay's font draws it in capitals).
std::string mods_ui_text(std::string_view text, std::size_t width) {
    std::size_t characters = 0u;
    std::size_t cut = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) continue;
        if (characters == width - 3u) cut = i;
        ++characters;
    }
    return characters <= width ? std::string(text) : std::string(text.substr(0, cut)) + "...";
}

std::string upper_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

// One step of a row's choice, clamped to its ends.
unsigned settings_step(unsigned value, unsigned count, int delta) {
    return static_cast<unsigned>(std::clamp(static_cast<int>(value) + delta, 0, static_cast<int>(count) - 1));
}

std::vector<SettingsRow> display_page(const RenderConfig& render) {
    std::vector<SettingsRow> rows;
    char text[96]{};
    const MonitorInfo* monitor = settings_monitor(render);
    rows.push_back({"SCREEN", {}, nullptr, {}, true});
    {
        std::string value = "PRIMARY";
        if (monitor != nullptr) {
            std::size_t index = 0u;
            for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                if (&s_settings_monitors[i] == monitor) index = i;
            }
            const std::string name = monitor->name.empty() ? std::string("Display") : mods_ui_text(monitor->name, 22u);
            std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(index + 1u), name.c_str());
            value = text;
        }
        SettingsRow row{"MONITOR", value, nullptr, "THE DISPLAY THE GAME RUNS ON"};
        if (s_settings_monitors.size() >= 2u) {
            row.change = [](RenderConfig& draft, int delta) {
                const MonitorInfo* current = settings_monitor(draft);
                std::size_t index = 0u;
                for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                    if (&s_settings_monitors[i] == current) index = i;
                }
                const std::size_t next = settings_step(static_cast<unsigned>(index),
                                                       static_cast<unsigned>(s_settings_monitors.size()), delta);
                if (next == index) return;
                const std::string device = narrow_utf8(s_settings_monitors[next].device);
                draft.monitor.fill('\0');
                std::memcpy(draft.monitor.data(), device.data(), std::min(device.size(), draft.monitor.size() - 1u));
                // A fullscreen mode belongs to its monitor; start from the new one's desktop mode.
                draft.fullscreen_width = 0u;
                draft.fullscreen_height = 0u;
                draft.fullscreen_refresh_hz = 0u;
                sanitize_settings_draft(draft);
                normalize_interpolation(draft);
            };
        }
        rows.push_back(std::move(row));
    }
    {
        static constexpr const char* kModes[] = {"WINDOWED", "BORDERLESS FULLSCREEN"};
        rows.push_back({"DISPLAY MODE", kModes[std::min(render.display_mode, kDisplayBorderless)],
                        [](RenderConfig& draft, int delta) {
                            draft.display_mode = settings_step(std::min(draft.display_mode, kDisplayBorderless), 2u, delta);
                            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                                snap_window_size(draft);
                            }
                            sanitize_settings_draft(draft);
                        },
                        "BORDERLESS FILLS THE SCREEN AND SWITCHES INSTANTLY"});
    }
    {
        const auto [w, h] = settings_output_size(render);
        std::snprintf(text, sizeof(text), "%uX%u", w, h);
        SettingsRow row{"RESOLUTION", text, nullptr, "BORDERLESS USES THE DESKTOP RESOLUTION"};
        if (render.display_mode == kDisplayWindowed) {
            row.help = "THE WINDOW SIZE";
            row.change = [](RenderConfig& draft, int delta) {
                const auto sizes = settings_window_sizes(settings_monitor(draft), draft.aspect_mode);  // smallest first
                std::size_t index = 0u;
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (std::uint64_t{sizes[i].first} * sizes[i].second <=
                        std::uint64_t{draft.window_width} * draft.window_height) {
                        index = i;
                    }
                }
                const int moved = static_cast<int>(index) + delta;
                if (moved < 0 || moved >= static_cast<int>(sizes.size())) return;
                draft.window_width = sizes[static_cast<std::size_t>(moved)].first;
                draft.window_height = sizes[static_cast<std::size_t>(moved)].second;
                sanitize_settings_draft(draft);
            };
        }
        rows.push_back(std::move(row));
    }
    {
        const unsigned hz = settings_refresh_hz(render);
        std::snprintf(text, sizeof(text), "%u HZ", hz != 0u ? hz : 60u);
        rows.push_back({"REFRESH RATE", text, nullptr, "SET IN THE WINDOWS DISPLAY SETTINGS"});
    }
    {
        static constexpr const char* kAspects[] = {"AUTO", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9"};
        static_assert(std::size(kAspects) == kAspectModeCount);
        rows.push_back({"ASPECT RATIO", kAspects[std::min(render.aspect_mode, kAspectModeCount - 1u)],
                        [](RenderConfig& draft, int delta) {
                            draft.aspect_mode = settings_step(draft.aspect_mode, kAspectModeCount, delta);
                            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                                snap_window_size(draft);
                            }
                        },
                        "AUTO MATCHES THE WINDOW. WIDER SHOWS MORE OF THE WORLD"});
    }
    rows.push_back({"VSYNC", render.vsync_interval == 0u ? "OFF" : "ON",
                    [](RenderConfig& draft, int) {
                        draft.vsync_interval = draft.vsync_interval == 0u ? 1u : 0u;
                        normalize_interpolation(draft);
                    },
                    "WAITS FOR THE DISPLAY: NO TEARING, A LITTLE MORE LATENCY"});
    rows.push_back({"INTERFACE", {}, nullptr, {}, true});
    rows.push_back({"HUD RESOLUTION", render.hud_output_resolution ? "DISPLAY" : "INTERNAL",
                    [](RenderConfig& draft, int) { draft.hud_output_resolution = !draft.hud_output_resolution; },
                    "DISPLAY DRAWS MENUS AND TEXT SHARP AT THE SCREEN RESOLUTION"});
    return rows;
}

void open_folder_in_explorer(const std::filesystem::path& folder);

std::vector<SettingsRow> graphics_page(const RenderConfig& render) {
    std::vector<SettingsRow> rows;
    char text[96]{};
    rows.push_back({"RESOLUTION", {}, nullptr, {}, true});
    {
        static constexpr const char* kUpscalers[] = {"OFF", "FSR 1", "FSR 3", "DLSS"};
        const std::vector<unsigned> choices = available_upscalers();
        const bool offered = std::find(choices.begin(), choices.end(), render.upscaler) != choices.end();
        std::string value = kUpscalers[offered ? std::min(render.upscaler, 3u) : 1u];
        if (offered && render.upscaler == 3u && !streamline::dlss_supported()) {
            value += dlss_installed_now() ? " (AFTER RESTART)" : " (DOWNLOAD)";
        }
        rows.push_back({"UPSCALING", value,
                        [](RenderConfig& draft, int delta) {
                            const std::vector<unsigned> options = available_upscalers();
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < options.size(); ++i) {
                                if (options[i] == draft.upscaler) index = i;
                            }
                            draft.upscaler = options[settings_step(static_cast<unsigned>(index),
                                                                   static_cast<unsigned>(options.size()), delta)];
                            normalize_interpolation(draft);
                        },
                        "DLSS AND FSR 3 RENDER FEWER PIXELS AND REBUILD A SHARP IMAGE"});
    }
    const unsigned internal = std::clamp(render.efb_scale, 1u, settings_scale_cap(render));
    const unsigned render_scale = upscaler_render_scale(render, internal);
    // Ray tracing replaces MSAA, ambient occlusion and the lighting modes: their rows are hidden while it is on.
    const bool rt_offered = host_window::ray_tracing_offered.load(std::memory_order_relaxed);
    const bool rt_on = rt_offered && render.ray_tracing_enabled;
    const bool upscaled = temporal_upscaler(render) != 0u || spatial_quality_mode(render);
    {
        static constexpr const char* kTemporal[] = {"NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kDlss[] = {"DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kSpatial[] = {"NATIVE", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        const bool temporal = render.upscaler >= 2u;
        const unsigned quality = temporal ? std::min(render.temporal_quality, 4u) : std::min(render.spatial_quality, 4u);
        const unsigned upscaler = std::min(render.upscaler, 3u);
        std::snprintf(text, sizeof(text), "%s (%uX)",
                      upscaler == 3u ? kDlss[quality] : upscaler == 2u ? kTemporal[quality] : kSpatial[quality],
                      render_scale);
        rows.push_back({upscaler == 3u ? "DLSS MODE" : upscaler == 2u ? "FSR 3 MODE" : "QUALITY MODE", text,
                        [](RenderConfig& draft, int delta) {
                            if (draft.upscaler >= 2u) {
                                draft.temporal_quality = settings_step(std::min(draft.temporal_quality, 4u), 5u, delta);
                            } else {
                                draft.spatial_quality = settings_step(std::min(draft.spatial_quality, 4u), 5u, delta);
                            }
                        },
                        "THE RENDER RESOLUTION BELOW THE INTERNAL ONE. QUALITY RENDERS THE MOST PIXELS"});
    }
    {
        std::string value = "MATCH OUTPUT";
        if (!render.internal_follow_window) {
            std::snprintf(text, sizeof(text), "%uX %uX%u%s", internal, kEfbWidth * internal, kEfbHeight * internal,
                          internal >= settings_scale_cap(render) ? " MAX" : "");
            value = text;
        }
        rows.push_back({"INTERNAL RESOLUTION", value,
                        [](RenderConfig& draft, int delta) {
                            const unsigned cap = settings_scale_cap(draft);
                            const unsigned current = draft.internal_follow_window ? 0u
                                                                                  : std::clamp(draft.efb_scale, 1u, cap);
                            const unsigned next = settings_step(current, cap + 1u, delta);
                            if (next == 0u) {
                                draft.internal_follow_window = true;
                            } else {
                                draft.internal_follow_window = false;
                                draft.efb_scale = next;
                            }
                        },
                        upscaled ? "THE UPSCALER'S OUTPUT. MATCH OUTPUT FOLLOWS THE SCREEN"
                                 : "MATCH OUTPUT RENDERS AT THE SCREEN RESOLUTION"});
    }
    std::snprintf(text, sizeof(text), "%u%%", std::min(render.sharpness, 100u));
    rows.push_back({"SHARPENING", text,
                    [](RenderConfig& draft, int delta) {
                        draft.sharpness = static_cast<unsigned>(
                            std::clamp(static_cast<int>(std::min(draft.sharpness, 100u)) + delta * 10, 0, 100));
                    },
                    "CONTRAST-ADAPTIVE SHARPENING OF THE FINAL IMAGE"});
    rows.back().slider_steps = 10u;
    rows.back().slider_at = static_cast<std::uint16_t>(std::min(render.sharpness, 100u) / 10u);
    {
        std::string value = anti_aliasing_row_text(render.aa_mode, render.smaa_quality, get_post_process_status());
        if (const std::size_t colon = value.find(": "); colon != std::string::npos) value.erase(0, colon + 2u);
        if (render.aa_mode != 0u && render.msaa_samples > 1u && render.upscaler != 2u && render.upscaler != 3u &&
            !render.ray_tracing_enabled) {
            value += " (OFF WITH MSAA)";
        }
        rows.push_back({"ANTI-ALIASING", value,
                        [](RenderConfig& draft, int delta) { draft.aa_mode = settings_step(std::min(draft.aa_mode, 3u), 4u, delta); },
                        "SMOOTHS EDGES. AUTO SKIPS IT WHEN THE IMAGE IS ALREADY SUPERSAMPLED"});
    }
    if (render.aa_mode == 1u || render.aa_mode == 2u) {
        static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
        rows.push_back({"SMAA QUALITY", kQuality[std::min(render.smaa_quality, 3u)],
                        [](RenderConfig& draft, int delta) {
                            draft.smaa_quality = settings_step(std::min(draft.smaa_quality, 3u), 4u, delta);
                        },
                        "HIGHER FINDS MORE EDGES"});
    }
    if (!rt_on) {
        const unsigned samples = std::bit_floor(std::clamp(render.msaa_samples, 1u, 8u));
        std::string value = samples <= 1u ? "OFF" : std::to_string(samples) + "X";
        if (samples > 1u && (render.upscaler == 2u || render.upscaler == 3u)) value += " (OFF WITH DLSS / FSR 3)";
        rows.push_back({"MSAA", value,
                        [](RenderConfig& draft, int delta) {
                            const unsigned index = static_cast<unsigned>(
                                std::countr_zero(std::bit_floor(std::clamp(draft.msaa_samples, 1u, 8u))));
                            draft.msaa_samples = 1u << settings_step(index, 4u, delta);
                        },
                        "SAMPLES EACH EDGE OF THE 3D SCENE SEVERAL TIMES. COSTS GPU TIME"});
    }

    if (rt_offered) {
        rows.push_back({"RAY TRACING", {}, nullptr, {}, true});
        rows.push_back({"RAY TRACING", render.ray_tracing_enabled ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.ray_tracing_enabled = !draft.ray_tracing_enabled; },
                        "TRACED SHADOWS, SKY AND BOUNCE LIGHT, GLOWS THAT LIGHT THE WORLD"});
    }
    if (rt_on) {
        // rt_quality: 0 medium, 1 high, 2 low; a change restarts the ray tracer (RendererD3D12::reconcile_ray_tracing).
        static constexpr unsigned kOrder[] = {2u, 0u, 1u};
        static constexpr const char* kNames[] = {"MEDIUM", "HIGH", "LOW"};
        rows.push_back({"RAY TRACING QUALITY", kNames[std::min(render.rt_quality, 2u)],
                        [](RenderConfig& draft, int delta) {
                            const unsigned quality = std::min(draft.rt_quality, 2u);
                            const unsigned at = quality == 2u ? 0u : quality == 0u ? 1u : 2u;
                            draft.rt_quality = kOrder[settings_step(at, 3u, delta)];
                        },
                        "HIGH: MORE LIGHTS PER PIXEL, FULL-RESOLUTION WATER"});
        // The effects players choose, each switched live.
        const auto effect_row = [&](const char* name, bool RenderConfig::*field, const char* help) {
            rows.push_back({name, render.*field ? "ON" : "OFF",
                            [field](RenderConfig& draft, int) { draft.*field = !(draft.*field); }, help});
        };
        effect_row("REFLECTIONS", &RenderConfig::rt_reflections,
                   "TRACED REFLECTIONS ON METAL, GLOSSY FLOORS, GLASS AND CRYSTAL");
        effect_row("WATER REFLECTIONS", &RenderConfig::rt_water,
                   "TRACED REFLECTIONS AND REFRACTION ON SEAS, LAKES AND POOLS");
        effect_row("SUN RAYS", &RenderConfig::rt_sun_rays, "SHAFTS OF LIGHT THE AIR SCATTERS AROUND THE SUN");
        effect_row("LENS BLOOM", &RenderConfig::rt_lens_bloom, "LIGHT THE LENS SCATTERS AROUND EVERY BRIGHT SURFACE");
    }

    rows.push_back({"FRAME RATE", {}, nullptr, {}, true});
    const Interpolation mode = interpolation_of(render);
    {
        std::string value = mode == Interpolation::kOff      ? "OFF"
                            : mode == Interpolation::kNative ? "NATIVE"
                                                             : "DLSS FRAME GENERATION";
        if (mode == Interpolation::kFrameGeneration) {
            if (!streamline::frame_generation_supported()) value += " (AFTER RESTART)";
            else if (render.upscaler != 3u) value += " (NEEDS DLSS)";
        }
        rows.push_back({"INTERPOLATION", value,
                        [](RenderConfig& draft, int delta) {
                            const auto modes = interpolation_modes(draft);
                            const Interpolation current = interpolation_of(draft);
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < modes.size(); ++i) {
                                if (modes[i] == current) index = i;
                            }
                            const Interpolation next = modes[settings_step(static_cast<unsigned>(index),
                                                                           static_cast<unsigned>(modes.size()), delta)];
                            if (next == current) return;
                            draft.frame_generation = next == Interpolation::kFrameGeneration ? 1u : 0u;
                            // Interpolation starts at the display's own rate.
                            draft.max_fps = next == Interpolation::kOff ? 60.0f : 0.0f;
                            if (next == Interpolation::kFrameGeneration && draft.upscaler != 3u) {
                                draft.upscaler = 3u;  // frame generation runs with DLSS
                            }
                            normalize_interpolation(draft);
                        },
                        mode == Interpolation::kOff ? "THE GAME RUNS AT 60. NATIVE OR DLSS ADD FRAMES BETWEEN"
                        : mode == Interpolation::kNative
                            ? "NEBULA DRAWS EXTRA FRAMES FROM THE GAME MOTION"
                            : "NVIDIA GENERATES THE FRAMES BETWEEN THE GAME'S 60"});
    }
    const double measured = host_window::measured_present_hz.load(std::memory_order_relaxed) *
                            (frame_generation_running() ? static_cast<double>(streamline::frames_presented()) : 1.0);
    char now[48]{};
    std::snprintf(now, sizeof(now), "SHOWING %.1f FPS", measured);
    if (mode == Interpolation::kOff) {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        rows.push_back({"FRAME RATE LIMIT", rate <= 30u ? "30 FPS" : "60 FPS",
                        [](RenderConfig& draft, int delta) { draft.max_fps = delta < 0 ? 30.0f : 60.0f; },
                        now});
    } else {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        if (rate == 0u) {
            const unsigned refresh = settings_refresh_hz(render);
            std::snprintf(text, sizeof(text), refresh != 0u ? "MATCH REFRESH (%u HZ)" : "MATCH REFRESH", refresh);
        } else {
            std::snprintf(text, sizeof(text), "%u FPS", rate);
        }
        rows.push_back({"TARGET FRAME RATE", text,
                        [](RenderConfig& draft, int delta) {
                            const auto targets = interpolation_targets(draft, interpolation_of(draft));
                            if (targets.empty()) return;
                            const auto current = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
                            std::size_t index = targets.size() - 1u;
                            for (std::size_t i = 0; i < targets.size(); ++i) {
                                if (targets[i] == current) index = i;
                            }
                            draft.max_fps = static_cast<float>(targets[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(targets.size()), delta)]);
                        },
                        now});
    }

    rows.push_back({"DETAIL", {}, nullptr, {}, true});
    {
        static constexpr std::array<unsigned, 5> kAnisotropy{1u, 2u, 4u, 8u, 16u};
        if (render.anisotropic_filtering <= 1u) {
            std::snprintf(text, sizeof(text), "TRILINEAR");
        } else {
            std::snprintf(text, sizeof(text), "%uX ANISOTROPIC", std::min(render.anisotropic_filtering, 16u));
        }
        rows.push_back({"TEXTURE FILTERING", text,
                        [](RenderConfig& draft, int delta) {
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < kAnisotropy.size(); ++i) {
                                if (kAnisotropy[i] <= draft.anisotropic_filtering) index = i;
                            }
                            draft.anisotropic_filtering = kAnisotropy[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(kAnisotropy.size()), delta)];
                        },
                        "SHARPER TEXTURES ON FLOORS AND WALLS AT AN ANGLE"});
    }
    rows.push_back({"MIPMAPS", render.enhanced_mipmaps ? "GENERATED" : "ORIGINAL",
                    [](RenderConfig& draft, int) { draft.enhanced_mipmaps = !draft.enhanced_mipmaps; },
                    "GENERATED ADDS SMOOTH DISTANT DETAIL WHERE THE GAME HAS NONE"});
    if (curved_surfaces_offered()) {
        rows.push_back({"CURVED SURFACES", render.curved_surfaces ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.curved_surfaces = !draft.curved_surfaces; },
                        "ROUNDS THE OUTLINES OF CURVED MODELS"});
    }
    if (!rt_on) {
        static constexpr const char* kLighting[] = {"PER VERTEX", "PER PIXEL", "ENHANCED"};
        rows.push_back({"LIGHTING", kLighting[lighting_mode(render)],
                        [](RenderConfig& draft, int delta) {
                            const unsigned mode = settings_step(lighting_mode(draft), 3u, delta);
                            draft.per_pixel_lighting = mode != 0u;
                            draft.enhanced_lighting = mode == 2u;
                        },
                        lighting_mode(render) == 2u
                            ? "HIGHLIGHTS, SOFT SKY LIGHT AND SHINIER METALS FROM THE GAME'S OWN LIGHTS"
                            : "PER PIXEL LIGHTS EVERY PIXEL INSTEAD OF EVERY CORNER"});
    }
    if (!rt_on) {
        rows.push_back({"AMBIENT OCCLUSION", render.ambient_occlusion ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.ambient_occlusion = !draft.ambient_occlusion; },
                        "SOFT SHADOWS IN CORNERS, CREASES AND WHERE THINGS MEET THE GROUND"});
    }
    {
        const bool enhanced = render.bloom_mode == kBloomEnhanced;
        rows.push_back({"BLOOM", enhanced ? "ENHANCED" : "NATIVE",
                        [](RenderConfig& draft, int) {
                            draft.bloom_mode = draft.bloom_mode == kBloomEnhanced ? kBloomNative : kBloomEnhanced;
                        },
                        enhanced ? "THE GAME'S GLOW REBUILT AT THE FULL RESOLUTION"
                                 : "THE GAME'S OWN GLOW, AS THE CONSOLE DRAWS IT"});
        if (enhanced) {
            // In steps of ten percent; 0 off.
            const unsigned value = std::min(render.bloom, kMaxBloom);
            if (value == 0u) {
                std::snprintf(text, sizeof(text), "OFF");
            } else {
                std::snprintf(text, sizeof(text), "%u%%", value);
            }
            rows.push_back({"  INTENSITY", text,
                            [](RenderConfig& draft, int delta) {
                                draft.bloom = static_cast<unsigned>(std::clamp(
                                    static_cast<int>(std::min(draft.bloom, kMaxBloom)) + delta * 10, 0,
                                    static_cast<int>(kMaxBloom)));
                            },
                            "THE ENHANCED GLOW'S STRENGTH"});
            rows.back().slider_steps = static_cast<std::uint16_t>(kMaxBloom / 10u);
            rows.back().slider_at = static_cast<std::uint16_t>(value / 10u);
        }
    }
    // Texture packs (the enabled mods') apply with the settings above; the folders of dumped textures and movies.
    rows.push_back({"TEXTURE PACKS", {}, nullptr, {}, true});
    rows.push_back({"TEXTURE PACKS", !render.texture_packs ? "OFF" : render.texture_pack_preload ? "PRELOAD" : "ON",
                    [](RenderConfig& draft, int delta) {
                        const int mode = !draft.texture_packs ? 0 : draft.texture_pack_preload ? 2 : 1;
                        const unsigned next = settings_step(static_cast<unsigned>(mode), 3u, delta);
                        draft.texture_packs = next != 0u;
                        draft.texture_pack_preload = next == 2u;
                    },
                    "ON LOADS TEXTURES AS THEY APPEAR. PRELOAD LOADS THEM ALL AT ONCE"});
    if (render.texture_packs) {
        const galaxy::gx::TexturePacks::Summary packs = galaxy::gx::TexturePacks::summary();
        char value[32]{};
        std::snprintf(value, sizeof(value), "%llu TEXTURES", static_cast<unsigned long long>(packs.textures));
        std::string help = "REPLACEMENT TEXTURES IN THE ENABLED MODS";
        if (packs.levels != 0u) help += ", WITH " + std::to_string(packs.levels) + " MIP LEVELS";
        if (!packs.unreadable.empty()) {
            help += ". " + std::to_string(packs.unreadable.size()) + " FILE NAME" +
                    (packs.unreadable.size() == 1u ? " IS" : "S ARE") + " NOT TEXTURE NAMES: " +
                    mods_ui_text(packs.unreadable.front(), 48u);
        }
        rows.push_back({"  REPLACEMENTS", packs.indexed ? std::string(value) : std::string("READING"), nullptr, help});
    }
    rows.push_back({"DUMP TEXTURES", render.texture_dump ? "ON" : "OFF",
                    [](RenderConfig& draft, int) { draft.texture_dump = !draft.texture_dump; },
                    "SAVES EVERY TEXTURE THE GAME SHOWS, FOR MAKING A PACK"});
    rows.push_back(button_row("TEXTURE DUMPS FOLDER", {"OPEN"},
                              [](RenderConfig&, unsigned) { open_folder_in_explorer(texture_dump_directory()); },
                              "WHERE DUMPED TEXTURES GO"));
    rows.push_back({"MOVIES", {}, nullptr, {}, true});
    rows.push_back(button_row("MOVIES FOLDER", {"OPEN"}, [](RenderConfig&, unsigned) { open_folder_in_explorer(movie_directory()); },
                              "VIDEOS NAMED LIKE THE GAME'S (PROLOGUEA.MP4) PLAY INSTEAD"));
    return rows;
}

// GAME > GALAXY SELECT: the galaxy and star the TRAVEL row goes to.
std::size_t s_galaxy_select_stage = 0u;
std::uint32_t s_galaxy_select_star = 1u;

// Mii import (GENERAL page): the last import's result and the console's Mii count (read again after an import).
std::mutex s_mii_mutex;
std::string s_mii_status;
std::atomic<int> s_mii_count{-1};
std::atomic<bool> s_mii_dialog_open{false};

// A picker or Explorer may open on the player's desktop: never from a hidden test run (logged instead).
bool opens_on_desktop(const char* what) {
    if (!hidden_window_mode_enabled()) {
        return true;
    }
    std::cerr << "[settings] not opened in a hidden run: " << what << '\n';
    return false;
}

// Miis picked: imported into the console's Mii database and the running game's.
void import_picked_miis(const std::vector<std::filesystem::path>& files) {
    if (!files.empty()) {
        std::string message;
        (void)galaxy::host::import_mii_files(files, &message);
        std::cerr << "[settings] mii import: " << message << '\n';
        {
            const std::lock_guard<std::mutex> lock(s_mii_mutex);
            s_mii_status = message;
        }
        s_mii_count.store(-1, std::memory_order_release);
    }
    s_mii_dialog_open.store(false, std::memory_order_release);
}

// The file picker on its own thread (a COM apartment of its own; the game keeps running), or the platform's
// (pick_files_on_desktop); the game keeps running.
void start_mii_import_dialog() {
    if (!opens_on_desktop("the Mii picker")) {
        return;
    }
    if (s_mii_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
#if !defined(_WIN32)
    pick_files_on_desktop("Import Miis", false, [](std::vector<std::filesystem::path> files) { import_picked_miis(files); });
#else
    const HWND owner = reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire));
    std::thread([owner] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> files;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            // Every console's Mii files (galaxy::host::read_mii_file); the Mii Studio code as text.
            const COMDLG_FILTERSPEC filters[] = {
                {L"Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)",
                 L"*.mii;*.rcd;*.rsd;*.mae;*.miigx;*.ncd;*.nsd;*.cfcd;*.cfsd;*.ffcd;*.ffsd;*.charinfo;*.nfsd;*.nfcd;"
                 L"*.mnms;*.emnms;*.miic;*.dat;*.bin;*.txt"},
                {L"All files", L"*.*"}};
            dialog->SetFileTypes(2u, filters);
            dialog->SetTitle(L"Import Miis");
            DWORD options = 0u;
            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
            }
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0u;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            files.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        import_picked_miis(files);
    }).detach();
#endif
}

// Explorer at a folder (made if missing), off the window's thread.
void open_folder_in_explorer(const std::filesystem::path& folder) {
    if (folder.empty()) {
        return;
    }
    if (!opens_on_desktop(folder.string().c_str())) {
        return;
    }
    std::thread([folder] {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
#if !defined(_WIN32)
        open_folder_on_desktop(folder);
#else
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
#endif
    }).detach();
}

// Explorer at the Mii import folder (its files join the database at every start).
void open_mii_folder() { open_folder_in_explorer(galaxy::host::mii_import_folder()); }

// The MODS page: the mods folder's mods (galaxy::mods), read when the page first shows and on a rescan (pages are
// rebuilt for every draw). Which are on, their order and their choices are a draft until Apply saves them to mods.ini
// (they apply at the next start, and Apply offers a restart); importing, removing and rescanning save the draft first.
std::mutex s_mods_mutex;
std::vector<galaxy::mods::ModInfo> s_mods;
std::vector<galaxy::mods::ModInfo> s_mods_saved;  // as mods.ini has them
bool s_mods_scanned = false;
std::string s_mod_focus;    // the mod last selected: the buttons' Options and Remove act on it
std::string s_mod_remove_armed;  // Remove pressed once on this mod: pressed again, it goes
std::string s_mod_held;     // a mod picked up to move with the arrows (empty: none)
std::string s_mod_options;  // the mod whose options show instead of the lists (empty: none)

// With s_mods_mutex held.
std::vector<galaxy::mods::ModInfo>& mods_list() {
    if (!s_mods_scanned) {
        s_mods = galaxy::mods::scan_mods();
        s_mods_saved = s_mods;
        s_mods_scanned = true;
    }
    return s_mods;
}

void forget_mods_scan() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    s_mods_scanned = false;
    s_mod_options.clear();
    s_mod_remove_armed.clear();
}

// With s_mods_mutex held: the texture packs and movies of the mods mods.ini has on to the renderer now (disc changes
// wait for the next start).
void apply_live_mods() {
    set_texture_pack_mod_folders(galaxy::mods::texture_folders(s_mods_saved));
    set_movie_mod_folders(galaxy::mods::movie_folders(s_mods_saved));
}

// With s_mods_mutex held: the list's order, then mods.ini.
void save_mods() {
    for (std::size_t i = 0; i < s_mods.size(); ++i) {
        s_mods[i].order = static_cast<unsigned>(i + 1u);
    }
    if (!galaxy::mods::save_mod_settings(s_mods)) {
        std::cerr << "[settings] mods.ini could not be written\n";
    }
    s_mods_saved = s_mods;
    apply_live_mods();
}

bool mods_restart_pending() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    return s_mods_scanned && galaxy::mods::mods_differ_from_session(s_mods_saved);
}

void refresh_overlaps_locked();

// With s_mods_mutex held: the mods folder read again (mods.ini's state), the draft's choices kept for the mods still
// there in the draft's order; mods new to the list as the read has them, the ones on at the top.
void reread_mods_locked() {
    std::vector<galaxy::mods::ModInfo> fresh = galaxy::mods::scan_mods();
    std::vector<galaxy::mods::ModInfo> merged;
    for (const galaxy::mods::ModInfo& drafted : s_mods) {
        const auto it = std::find_if(fresh.begin(), fresh.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == drafted.id; });
        if (it == fresh.end()) continue;
        galaxy::mods::ModInfo mod = *it;
        mod.enabled = drafted.enabled;
        mod.own_text = drafted.own_text;
        for (std::size_t o = 0; o < mod.options.size() && mod.options.size() == drafted.options.size(); ++o) {
            mod.options[o].selected = drafted.options[o].selected;
        }
        merged.push_back(std::move(mod));
    }
    for (const galaxy::mods::ModInfo& mod : fresh) {
        if (std::none_of(merged.begin(), merged.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == mod.id; })) {
            merged.insert(mod.enabled ? merged.begin() : merged.end(), mod);
        }
    }
    std::stable_partition(merged.begin(), merged.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; });
    for (std::size_t i = 0; i < merged.size(); ++i) {
        merged[i].order = static_cast<unsigned>(i + 1u);
    }
    s_mods = std::move(merged);
    s_mods_saved = std::move(fresh);
    s_mods_scanned = true;
    refresh_overlaps_locked();
    apply_live_mods();
}

// With s_mods_mutex held: the mods whose state or choices differ from mods.ini's, and one more when the order of the
// mods on does.
unsigned mods_draft_changes_locked() {
    if (!s_mods_scanned) return 0u;
    unsigned changes = 0u;
    std::vector<std::string> order;
    std::vector<std::string> saved_order;
    for (const galaxy::mods::ModInfo& mod : s_mods) {
        const auto saved = std::find_if(s_mods_saved.begin(), s_mods_saved.end(),
                                        [&](const galaxy::mods::ModInfo& m) { return m.id == mod.id; });
        if (saved == s_mods_saved.end()) {
            ++changes;
            continue;
        }
        bool same = mod.enabled == saved->enabled && mod.own_text == saved->own_text &&
                    mod.options.size() == saved->options.size();
        for (std::size_t o = 0; same && o < mod.options.size(); ++o) {
            same = mod.options[o].selected == saved->options[o].selected;
        }
        changes += same ? 0u : 1u;
        if (mod.enabled && saved->enabled) order.push_back(mod.id);
    }
    for (const galaxy::mods::ModInfo& mod : s_mods_saved) {
        if (mod.enabled && std::find(order.begin(), order.end(), mod.id) != order.end()) saved_order.push_back(mod.id);
    }
    return changes + (order != saved_order ? 1u : 0u);
}

unsigned mods_draft_changes() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    return mods_draft_changes_locked();
}

// With s_mods_mutex held: the draft to mods.ini; `ask`: the menu asks to restart when this session started otherwise.
// The overlaps follow the saved order at the next read.
void commit_mods_locked(bool ask) {
    if (mods_draft_changes_locked() == 0u) return;
    save_mods();
    s_mod_held.clear();
    if (ask && galaxy::mods::mods_differ_from_session(s_mods)) {
        s_mods_restart_ask.store(true, std::memory_order_release);
    }
    s_mods_scanned = false;
}

void commit_mods_draft() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    commit_mods_locked(true);
}

void revert_mods_draft() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    s_mods = s_mods_saved;
    s_mod_held.clear();
}

// With s_mods_mutex held: who wins each conflict by the draft's order (their sizes come from the last read).
void refresh_overlaps_locked() {
    const auto place = [](const std::string& id) {
        return std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; }) -
               s_mods.begin();
    };
    for (galaxy::mods::ModInfo& mod : s_mods) {
        const auto self = place(mod.id);
        for (galaxy::mods::ModOverlap& overlap : mod.overlaps) {
            overlap.other_above = place(overlap.other) < self;
        }
    }
}

// With s_mods_mutex held: mod `id` into the list of mods on at `index` (0 the top), or off (`on` false: among the
// others by name). The draft only.
void place_mod_locked(const std::string& id, bool on, std::size_t index) {
    const auto find = [&] {
        return std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
    };
    auto it = find();
    if (it == s_mods.end()) return;
    if (it->enabled != on) {
        galaxy::mods::set_mod_enabled(s_mods, static_cast<std::size_t>(it - s_mods.begin()), on);
        it = find();
    }
    if (on) {
        const std::size_t enabled = static_cast<std::size_t>(
            std::count_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; }));
        const std::size_t from = static_cast<std::size_t>(it - s_mods.begin());
        const std::size_t to = std::min(index, enabled - 1u);
        if (from < to) {
            std::rotate(s_mods.begin() + from, s_mods.begin() + from + 1, s_mods.begin() + to + 1);
        } else if (from > to) {
            std::rotate(s_mods.begin() + to, s_mods.begin() + from, s_mods.begin() + from + 1);
        }
    }
    for (std::size_t i = 0; i < s_mods.size(); ++i) {
        s_mods[i].order = static_cast<unsigned>(i + 1u);
    }
    refresh_overlaps_locked();
}

// Mod import (the MODS page's rows and files dropped on the window): one batch at a time on its own thread; the
// IMPORT MOD row shows its progress and result.
std::mutex s_mod_import_mutex;
std::string s_mod_import_value;    // the row's value: progress, then the outcome
std::string s_mod_import_message;  // the outcome, the row's help
std::atomic<bool> s_mod_import_busy{false};
std::atomic<bool> s_mod_dialog_open{false};

void run_mod_imports(std::vector<std::filesystem::path> sources) {
    if (sources.empty()) return;
    if (s_mod_import_busy.exchange(true, std::memory_order_acq_rel)) {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        s_mod_import_message = "AN IMPORT IS RUNNING: TRY AGAIN WHEN IT ENDS";
        return;
    }
    std::thread([sources = std::move(sources)] {
        const std::filesystem::path folder = galaxy::mods::mods_folder();
        std::string message;
        unsigned imported = 0;
        unsigned failed = 0;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto progress = [&](const galaxy::mods::ImportProgress& p) {
                char text[32]{};
                const unsigned percent =
                    p.total != 0u ? static_cast<unsigned>(std::min<std::uint64_t>(99u, p.done * 100u / p.total)) : 0u;
                if (sources.size() > 1u) {
                    std::snprintf(text, sizeof(text), "%zu/%zu: %u%%", i + 1u, sources.size(), percent);
                } else {
                    std::snprintf(text, sizeof(text), "IMPORTING %u%%", percent);
                }
                const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
                s_mod_import_value = text;
            };
            const galaxy::mods::ImportResult result =
                folder.empty() ? galaxy::mods::ImportResult{false, {}, false, 0, 0, {}, "NO MODS FOLDER"}
                               : galaxy::mods::import_mod(sources[i], folder, progress);
            std::cerr << "[mods] import " << sources[i].string() << ": " << result.message << " (" << result.files
                      << " files, " << (result.bytes >> 20u) << " MB)\n";
            (result.ok ? imported : failed) += 1u;
            message = message.empty() ? result.message : message + "; " + result.message;
        }
        {
            const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
            s_mod_import_value = failed == 0u ? "DONE" : imported == 0u ? "FAILED" : "SOME FAILED";
            s_mod_import_message = message + ". MODS APPLY WHEN NEBULA RESTARTS";
        }
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            reread_mods_locked();
        }
        s_mod_import_busy.store(false, std::memory_order_release);
    }).detach();
}

// The picker for archives or folders on its own thread (the game keeps running).
void start_mod_import_dialog(bool folders) {
    if (!opens_on_desktop("the mod picker")) {
        return;
    }
    if (s_mod_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
#if !defined(_WIN32)
    pick_files_on_desktop(folders ? "Import mod folders" : "Import mods", folders,
                          [](std::vector<std::filesystem::path> picked) {
                              s_mod_dialog_open.store(false, std::memory_order_release);
                              run_mod_imports(std::move(picked));
                          });
#else
    const HWND owner = reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire));
    std::thread([owner, folders] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> picked;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0u;
            dialog->GetOptions(&options);
            options |= FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            if (folders) {
                options |= FOS_PICKFOLDERS;
                dialog->SetTitle(L"Import mod folders");
            } else {
                const COMDLG_FILTERSPEC filters[] = {
                    {L"Mods and texture packs (zip, 7z, rar, tar)",
                     L"*.zip;*.7z;*.rar;*.tar;*.tgz;*.gz;*.txz;*.xz;*.tbz2;*.bz2;*.tzst;*.zst"},
                    {L"All files", L"*.*"}};
                dialog->SetFileTypes(2u, filters);
                dialog->SetTitle(L"Import mods");
                options |= FOS_FILEMUSTEXIST;
            }
            dialog->SetOptions(options);
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0u;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            picked.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) CoUninitialize();
        s_mod_dialog_open.store(false, std::memory_order_release);
        run_mod_imports(std::move(picked));
    }).detach();
#endif
}

std::vector<SettingsRow> mods_page(const RenderConfig& render);

// The name of a language folder as the menu says it.
std::string language_name(std::string_view folder) {
    static constexpr std::pair<const char*, const char*> kNames[] = {
        {"English", "ENGLISH"}, {"French", "FRENCH"}, {"Spanish", "SPANISH"}, {"German", "GERMAN"},
        {"Italian", "ITALIAN"}, {"Dutch", "DUTCH"}, {"Japanese", "JAPANESE"}, {"Korean", "KOREAN"},
        {"SimpChinese", "CHINESE"}, {"TradChinese", "CHINESE"}};
    for (const auto& [suffix, name] : kNames) {
        if (folder.ends_with(suffix)) return name;
    }
    return mods_ui_text(folder, 12u);
}

// The language folder the next start plays in (the language setting applies at the next start, as mods do).
std::string next_start_language_folder() {
    std::string game_id = galaxy::mods::mods_game_id();
    if (game_id.empty()) game_id = "RMGE01";
    return galaxy::mods::language_folder(game_id, static_cast<unsigned>(get_runtime_settings().language));
}

// The selection to a row of the MODS page (after a change moved it).
void select_mods_row(const RenderConfig& render, const std::string& key) {
    const std::vector<SettingsRow> rows = mods_page(render);
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (rows[r].key == key) {
            s_native_settings_row.store(static_cast<unsigned>(r), std::memory_order_relaxed);
            return;
        }
    }
}

// What a mod changes, its conflicts by the draft's order, or why it does nothing: the line under the page.
std::string mod_details(const galaxy::mods::ModInfo& mod) {
    if (!mod.problem.empty() && !mod.files && !mod.textures && !mod.movies && !mod.loose) {
        return mods_ui_text(mod.problem, 120u);
    }
    std::string kinds;
    for (const auto& [on, name] : {std::pair{mod.riivolution, "RIIVOLUTION"}, std::pair{mod.files || mod.loose, "FILES"},
                                   std::pair{mod.textures, "TEXTURES"}, std::pair{mod.movies, "MOVIES"}}) {
        if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
    }
    if (!mod.languages.empty()) {
        std::string languages;
        for (const std::string& folder : mod.languages) {
            const std::string name = language_name(folder);
            if (languages.find(name) == std::string::npos) languages += languages.empty() ? name : ", " + name;
        }
        kinds += ". TEXT: " + languages;
    }
    if (mod.code_patches != 0u) {
        kinds += ". " + std::to_string(mod.code_patches) + " CODE CHANGE" + (mod.code_patches == 1u ? "" : "S") +
                 " CANNOT RUN IN NEBULA";
    }
    for (const galaxy::mods::ModOverlap& overlap : mod.overlaps) {
        std::string what;
        if (overlap.files != 0u) what = std::to_string(overlap.files) + (overlap.files == 1u ? " FILE" : " FILES");
        if (overlap.textures != 0u) {
            what += (what.empty() ? "" : ", ") + std::to_string(overlap.textures) +
                    (overlap.textures == 1u ? " TEXTURE" : " TEXTURES");
        }
        // The other mod's name apart from the words, so that the menu can name both ("OVERRIDDEN BY: X (2 FILES)").
        kinds += std::string(overlap.other_above ? ". OVERRIDDEN BY: " : ". OVERRIDES: ") + mods_ui_text(overlap.other, 24u) +
                 " (" + what + ")";
    }
    return kinds.empty() ? std::string("NOTHING THIS GAME USES") : kinds;
}

void close_mod_options(const RenderConfig& render);

// Whether a mod has options to show: its Riivolution choices, or the text it shows in a language it lacks.
bool mod_has_options(const galaxy::mods::ModInfo& mod, const std::string& playing) {
    const bool covers = std::find(mod.languages.begin(), mod.languages.end(), playing) != mod.languages.end();
    return !mod.options.empty() || (!mod.languages.empty() && !covers && !playing.empty());
}

// A mod's options instead of the lists: its Riivolution choices and the text it shows, part of the draft. Done,
// Escape or B goes back to the lists. With s_mods_mutex held.
std::vector<SettingsRow> mod_options_page(std::size_t i) {
    std::vector<SettingsRow> rows;
    const galaxy::mods::ModInfo& mod = s_mods[i];
    SettingsRow title{mods_ui_text(mod.id, 26u), {}, nullptr, {}, true};
    title.names = 1u;
    rows.push_back(std::move(title));
    for (std::size_t o = 0; o < mod.options.size(); ++o) {
        const galaxy::mods::ModOption& option = mod.options[o];
        const std::string value = option.selected == 0u || option.selected > option.choices.size()
                                      ? std::string("OFF")
                                      : mods_ui_text(option.choices[option.selected - 1u], 22u);
        const bool other_region = option.selected != 0u && option.selected <= option.other_region.size() &&
                                  option.other_region[option.selected - 1u];
        // A mod's option name as its menu shows it, without the colon some end in.
        std::string_view name = option.name;
        while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
        SettingsRow choice{mods_ui_text(name, 24u), value,
                           [i, o](RenderConfig&, int delta) {
                               const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                               if (i >= s_mods.size() || o >= s_mods[i].options.size()) return;
                               galaxy::mods::ModOption& changed = s_mods[i].options[o];
                               changed.selected = settings_step(
                                   std::min<unsigned>(changed.selected, static_cast<unsigned>(changed.choices.size())),
                                   static_cast<unsigned>(changed.choices.size() + 1u), delta);
                           },
                           other_region ? std::string("MADE FOR ANOTHER REGION'S DISC: IT MAY NOT WORK WITH THIS ONE")
                                        : mods_ui_text(option.section, 30u)};
        choice.names = option.selected != 0u ? 3u : 1u;
        rows.push_back(std::move(choice));
    }
    // Text in the language played: the mod's own (in a language it has) or the game's.
    const std::string playing = next_start_language_folder();
    const bool covers = std::find(mod.languages.begin(), mod.languages.end(), playing) != mod.languages.end();
    if (!mod.languages.empty() && !covers && !playing.empty()) {
        std::string theirs = language_name(mod.languages.front());
        for (const std::string& folder : mod.languages) {
            if (language_name(folder) == "ENGLISH") theirs = "ENGLISH";
        }
        rows.push_back({"TEXT", mod.own_text ? "THE MOD'S " + theirs : "THE GAME'S " + language_name(playing),
                        [i](RenderConfig&, int delta) {
                            const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                            if (i >= s_mods.size() || delta == 0) return;
                            s_mods[i].own_text = !s_mods[i].own_text;
                        },
                        "THIS MOD HAS NO " + language_name(playing) + " TEXT. ITS OWN TEXT MATCHES ITS CHANGES"});
    }
    rows.push_back(button_row({}, {"DONE"}, [](RenderConfig& draft, unsigned) { close_mod_options(draft); }, {}));
    return rows;
}

// A mod: its name, and what it changes as the line under the page. With s_mods_mutex held.
SettingsRow mod_row(const galaxy::mods::ModInfo& mod) {
    SettingsRow row{mods_ui_text(mod.id, 26u), {}, nullptr, mod_details(mod)};
    row.key = "mod:" + mod.id;
    row.names = 1u;
    return row;
}

// The buttons under the mods, as the launcher's: Import file, Import folder, Options and Remove (of the mod last
// selected; '-' before a word: unavailable; Remove asks by its word first), Open mods folder and Rescan. The line under
// the page says how an import or a removal goes. With s_mods_mutex held.
SettingsRow mods_buttons_row() {
    std::string help;
    {
        const std::lock_guard<std::mutex> import_lock(s_mod_import_mutex);
        help = !s_mod_import_message.empty() ? mods_ui_text(s_mod_import_message, 120u) : s_mod_import_value;
    }
    if (s_mod_dialog_open.load(std::memory_order_acquire)) help = "CHOOSING";
    const auto focus = std::find_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
    const bool options = focus != s_mods.end() && mod_has_options(*focus, next_start_language_folder());
    const char* const remove = focus == s_mods.end()            ? "-REMOVE"
                               : s_mod_remove_armed == s_mod_focus ? "PRESS AGAIN TO REMOVE"
                                                                   : "REMOVE";
    SettingsRow row = button_row(
        {}, {"IMPORT FILE", "IMPORT FOLDER", options ? "OPTIONS" : "-OPTIONS", remove, "OPEN FOLDER", "RESCAN"},
        [](RenderConfig&, unsigned button) {
            switch (button) {
            case 0u:
            case 1u:
                if (!s_mod_import_busy.load(std::memory_order_acquire)) start_mod_import_dialog(button == 1u);
                return;
            case 2u: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                const auto it = std::find_if(s_mods.begin(), s_mods.end(),
                                             [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
                if (it != s_mods.end() && mod_has_options(*it, next_start_language_folder())) {
                    s_mod_options = s_mod_focus;
                    s_mod_held.clear();
                }
                return;
            }
            case 3u: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                if (s_mod_focus.empty()) return;
                if (s_mod_remove_armed != s_mod_focus) {
                    s_mod_remove_armed = s_mod_focus;
                    return;
                }
                std::string message;
                galaxy::mods::remove_mod(galaxy::mods::mods_folder(), s_mod_focus, &message);
                std::cerr << "[settings] " << message << '\n';
                {
                    const std::lock_guard<std::mutex> import_lock(s_mod_import_mutex);
                    s_mod_import_message = message;
                }
                s_mod_remove_armed.clear();
                s_mod_focus.clear();
                reread_mods_locked();
                return;
            }
            case 4u:
                open_folder_in_explorer(galaxy::mods::mods_folder());
                return;
            default: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                reread_mods_locked();
                return;
            }
            }
        },
        std::move(help));
    row.key = "buttons";
    return row;
}

// The MODS page: the mods in the mods folder by name, then the buttons. It imports and removes them and sets their
// options; which play is the room's, picked on the main menu's Host page (docs/COOP_PLAN.md, "Mods"). A mod's options
// show instead while they are open.
std::vector<SettingsRow> mods_page(const RenderConfig&) {
    std::vector<SettingsRow> rows;
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    std::vector<galaxy::mods::ModInfo>& mods = mods_list();
    const auto open = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_options; });
    if (open != mods.end()) {
        return mod_options_page(static_cast<std::size_t>(open - mods.begin()));
    }
    s_mod_options.clear();
    rows.push_back({"MODS (THE HOST PAGE PICKS THE ROOM'S)", {}, nullptr, {}, true});
    std::vector<const galaxy::mods::ModInfo*> by_name;
    for (const galaxy::mods::ModInfo& mod : mods) by_name.push_back(&mod);
    std::sort(by_name.begin(), by_name.end(),
              [](const galaxy::mods::ModInfo* a, const galaxy::mods::ModInfo* b) { return a->id < b->id; });
    for (const galaxy::mods::ModInfo* mod : by_name) rows.push_back(mod_row(*mod));
    // The buttons act on the mod selected last; Remove asks again for another.
    const unsigned selected = s_native_settings_row.load(std::memory_order_relaxed);
    if (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount == kModsTab &&
        selected < rows.size() && rows[selected].key.rfind("mod:", 0) == 0) {
        s_mod_focus = rows[selected].key.substr(4u);
    }
    if (s_mod_remove_armed != s_mod_focus) s_mod_remove_armed.clear();
    rows.push_back(mods_buttons_row());
    return rows;
}

// Back from a mod's options to the lists, on that mod.
void close_mod_options(const RenderConfig& render) {
    std::string id;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        id.swap(s_mod_options);
    }
    select_mods_row(render, "mod:" + id);
}

// The MODS page's keys: on a mod's options, Escape goes back to the mods; elsewhere the page's keys are every page's.
bool mods_page_key(WPARAM key, bool, bool was_down) {
    if (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount != kModsTab) return false;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        if (s_mod_options.empty() || key != VK_ESCAPE) return false;
    }
    if (!was_down) close_mod_options(s_settings_draft);
    return true;
}

std::vector<SettingsRow> game_page(const RuntimeSettings& runtime) {
    std::vector<SettingsRow> rows;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%uX", std::clamp(runtime.text_speed, 1u, 4u));
    rows.push_back({"TEXT SPEED", runtime.text_speed >= 4u ? std::string("INSTANT") : std::string(text),
                    [](RenderConfig&, int delta) {
                        RuntimeSettings& settings = s_runtime_draft;
                        settings.text_speed = settings_step(std::clamp(settings.text_speed, 1u, 4u) - 1u, 4u, delta) + 1u;
                    },
                    "HOW FAST DIALOGUE TEXT APPEARS. INSTANT: EACH PAGE AT ONCE"});
    {
        static constexpr const char* kIcons[] = {"AUTO", "WII", "XBOX", "NINTENDO SWITCH", "PLAYSTATION"};
        rows.push_back({"BUTTON ICONS", kIcons[std::min<std::size_t>(static_cast<std::size_t>(runtime.button_icons), 4u)],
                        [](RenderConfig&, int delta) {
                            RuntimeSettings& settings = s_runtime_draft;
                            settings.button_icons = static_cast<ButtonIcons>(
                                settings_step(static_cast<unsigned>(settings.button_icons), 5u, delta));
                        },
                        "THE BUTTONS THE GAME'S ICONS SHOW. AUTO: THE CONTROLLER IN USE"});
    }
    rows.push_back({"LANGUAGE", game_language_name(runtime.language),
                    [](RenderConfig&, int delta) {
                        // Saved when applied; the game switches at the next start (game_language_for_boot).
                        // The languages on the disc as it is read (galaxy::set_disc_languages).
                        RuntimeSettings& settings = s_runtime_draft;
                        std::vector<GameLanguage> offered;
                        for (const GameLanguage language : kGameLanguages) {
                            if (galaxy::game_language_on_disc(language)) offered.push_back(language);
                        }
                        std::size_t index = 0u;
                        for (std::size_t i = 0; i < offered.size(); ++i) {
                            if (offered[i] == settings.language) index = i;
                        }
                        settings.language = offered[settings_step(static_cast<unsigned>(index),
                                                                  static_cast<unsigned>(offered.size()), delta)];
                    },
                    "THE GAME'S TEXT AND MENUS"});
    rows.push_back({"DISCORD STATUS", runtime.discord_status ? std::string("ON") : std::string("OFF"),
                    [](RenderConfig&, int delta) {
                        if (delta == 0) return;
                        s_runtime_draft.discord_status = !s_runtime_draft.discord_status;
                    },
                    "SHOWS YOUR CURRENT GALAXY AND MISSION IN DISCORD"});
    {
        rows.push_back({"MIIS", {}, nullptr, {}, true});
        std::string status;
        {
            const std::lock_guard<std::mutex> lock(s_mii_mutex);
            status = s_mii_status;
        }
        if (s_mii_dialog_open.load(std::memory_order_acquire)) {
            status = "CHOOSING FILES";
        }
        rows.push_back(button_row("IMPORT MIIS", {"IMPORT"}, [](RenderConfig&, unsigned) { start_mii_import_dialog(); },
                                  status.empty() ? std::string("MIIS FROM A WII, DS, 3DS, WII U, SWITCH, MII STUDIO OR MII "
                                                               "CREATOR, FOR THE MII ICONS")
                                                 : status));
        int count = s_mii_count.load(std::memory_order_acquire);
        if (count < 0) {
            count = static_cast<int>(galaxy::host::mii_library_count());
            s_mii_count.store(count, std::memory_order_release);
        }
        char mii_text[32]{};
        std::snprintf(mii_text, sizeof(mii_text), "%d OF 100", count);
        rows.push_back({"MIIS ON THIS CONSOLE", mii_text, nullptr, "THE MII ICON PICKER ON THE FILE SELECT SHOWS THEM"});
        rows.push_back(button_row("MIIS FOLDER", {"OPEN"}, [](RenderConfig&, unsigned) { open_mii_folder(); },
                                  "FILES PUT HERE ARE IMPORTED AT EVERY START"));
    }
    return rows;
}

// The room's cheats as they apply now: the host's for a player who joined, else this player's room's.
bool coop_room_cheats() {
    const CoopRoomStatus status = coop_room_status();
    return status.joined ? status.room.cheats : galaxy::get_runtime_settings().coop.cheats;
}

// ONLINE (docs/COOP_PLAN.md, "The Online tab"): the room's settings and the cheats, which work only while the room
// has them on (coop_cheats, applied): any galaxy and star, entered with the game's own stage change (names in its own
// words), and the Grand Stars that power the Observatory. The way out of the room is the pause menu's Quit.
std::vector<SettingsRow> online_page(const RuntimeSettings& runtime) {
    std::vector<SettingsRow> rows;
    // A player who joined sees the host's room, which only the host changes.
    const CoopRoomStatus status = coop_room_status();
    const RuntimeCoopSettings& coop = status.joined ? status.room : runtime.coop;
    rows.push_back({"ROOM", {}, nullptr, {}, true});
    char players[32]{};
    std::snprintf(players, sizeof(players), "%u OF %u", std::max(status.players, 1u), coop.max_players);
    rows.push_back({"PLAYERS", players, nullptr, "THE PLAYERS IN THE ROOM NOW, YOU INCLUDED"});
    const std::size_t first_room_row = rows.size();
    rows.push_back({"MAX PLAYERS", std::to_string(coop.max_players),
                    [](RenderConfig&, int delta) {
                        unsigned& max = s_runtime_draft.coop.max_players;
                        max = settings_step(max - 2u, kCoopMaxPlayersLimit - 1u, delta) + 2u;
                    },
                    "HOW MANY PLAYERS THE ROOM HOLDS, YOU INCLUDED"});
    static constexpr const char* kInteractions[] = {"NON-SOLID", "SOLID", "FRIENDLY FIRE"};
    rows.push_back({"PLAYER INTERACTION", kInteractions[std::min(static_cast<unsigned>(coop.player_interaction), 2u)],
                    [](RenderConfig&, int delta) {
                        CoopPlayerInteraction& interaction = s_runtime_draft.coop.player_interaction;
                        interaction = static_cast<CoopPlayerInteraction>(
                            settings_step(static_cast<unsigned>(interaction), 3u, delta));
                    },
                    "NON-SOLID: PLAYERS PASS THROUGH. SOLID: THEY BUMP AND KNOCK BACK. FRIENDLY FIRE: ATTACKS HURT"});
    static constexpr unsigned kKnockbacks[] = {kCoopKnockbackWeak, kCoopKnockbackNormal, kCoopKnockbackTooMuch};
    static constexpr const char* kKnockbackNames[] = {"WEAK", "NORMAL", "TOO MUCH"};
    const auto knockback = [](unsigned strength) {
        return strength < (kCoopKnockbackWeak + kCoopKnockbackNormal) / 2u      ? 0u
               : strength < (kCoopKnockbackNormal + kCoopKnockbackTooMuch) / 2u ? 1u
                                                                                 : 2u;
    };
    rows.push_back({"KNOCKBACK STRENGTH", kKnockbackNames[knockback(coop.knockback_strength)],
                    [knockback](RenderConfig&, int delta) {
                        unsigned& strength = s_runtime_draft.coop.knockback_strength;
                        strength = kKnockbacks[settings_step(knockback(strength), 3u, delta)];
                    },
                    "HOW FAR A PLAYER'S ATTACK KNOCKS ANOTHER"});
    rows.push_back({"PVP MODE", coop.pvp_mode == CoopPvpMode::Revamped ? std::string("REVAMPED") : std::string("CLASSIC"),
                    [](RenderConfig&, int delta) {
                        CoopPvpMode& mode = s_runtime_draft.coop.pvp_mode;
                        mode = static_cast<CoopPvpMode>(settings_step(static_cast<unsigned>(mode), 2u, delta));
                    },
                    "FRIENDLY FIRE'S RULES: CLASSIC, OR REVAMPED (WHICH ATTACKS BEAT WHICH)"});
    rows.push_back({"BUBBLE ON DEATH", coop.bubble_death ? std::string("ON") : std::string("OFF"),
                    [](RenderConfig&, int delta) {
                        if (delta == 0) return;
                        s_runtime_draft.coop.bubble_death = !s_runtime_draft.coop.bubble_death;
                    },
                    "ON: A PLAYER WHO DIES NEAR ANOTHER FLOATS TO THEM IN A BUBBLE, FOR A LIFE, INSTEAD OF A MISS"});
    rows.push_back({"ALLOW CHEATS", coop.cheats ? std::string("ON") : std::string("OFF"),
                    [](RenderConfig&, int delta) {
                        if (delta == 0) return;
                        s_runtime_draft.coop.cheats = !s_runtime_draft.coop.cheats;
                    },
                    "ON: EVERYONE IN THE ROOM MAY USE THE CHEATS BELOW, ONCE APPLIED. OFF: THEY DO NOTHING"});

    // A player who joined: the room's rows show the host's settings, unchanged.
    if (status.joined) {
        for (std::size_t r = first_room_row; r < rows.size(); ++r) {
            rows[r].change = nullptr;
            rows[r].help = "THE HOST'S SETTING: ONLY THE HOST CHANGES THE ROOM";
        }
    }

    // The cheats: the room's, applied (a change waiting to be applied does not count yet); the host's for a player
    // who joined.
    const bool cheats = status.joined ? status.room.cheats : galaxy::get_runtime_settings().coop.cheats;
    rows.push_back({"CHEATS", {}, nullptr, {}, true});
    want_galaxy_select_names();
    const std::size_t stage = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
    const std::uint32_t star = std::clamp(s_galaxy_select_star, 1u, kGalaxyStages[stage].stars);
    std::string name = galaxy_select_name(stage);
    if (name.empty()) {
        name = std::string_view(kGalaxyStages[stage].id) == kObservatoryId ? std::string(kObservatoryName)
                                                                            : upper_ascii(kGalaxyStages[stage].id);
    }
    rows.push_back({"GALAXY", name,
                    [](RenderConfig&, int delta) {
                        s_galaxy_select_stage = settings_step(
                            static_cast<unsigned>(std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u)),
                            static_cast<unsigned>(kGalaxyStageCount), delta);
                        s_galaxy_select_star = 1u;
                    },
                    "ANY GALAXY, FOR TESTING. STARS COLLECTED THERE ARE SAVED"});
    rows.back().names = 2u;  // the game's own name in the played language
    std::string star_name = galaxy_select_star_name(stage, star);
    char star_text[96]{};
    std::snprintf(star_text, sizeof(star_text), star_name.empty() ? "%u" : "%u  %s", star, star_name.c_str());
    rows.push_back({"STAR", star_text,
                    [](RenderConfig&, int delta) {
                        const std::size_t at = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
                        s_galaxy_select_star = settings_step(s_galaxy_select_star - 1u, kGalaxyStages[at].stars,
                                                             delta) + 1u;
                    },
                    "THE MISSION TO PLAY"});
    rows.back().names = 2u;
    rows.push_back(button_row("TRAVEL", {cheats ? "GO" : "-GO"},
                    [](RenderConfig&, unsigned) {
                        if (!coop_room_cheats()) return;
                        const std::size_t at = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
                        request_galaxy_travel(at, std::clamp(s_galaxy_select_star, 1u, kGalaxyStages[at].stars));
                        std::cerr << "[settings] travel to " << kGalaxyStages[at].id << ' '
                                  << s_galaxy_select_star << '\n';
                        set_native_settings_visible(false);
                    },
                    cheats ? "LOADS IT WITH THE GAME OWN STAGE CHANGE, FROM ANY GALAXY"
                           : "THE ROOM'S CHEATS ARE OFF: TURN THEM ON ABOVE AND APPLY"));
    rows.push_back(button_row("GRAND STARS", {cheats ? "GET" : "-GET"},
                    [](RenderConfig&, unsigned) {
                        if (!coop_room_cheats()) return;
                        request_grand_stars();
                        std::cerr << "[settings] grand stars\n";
                        set_native_settings_visible(false);
                    },
                    "GIVES THIS SAVE FILE GRAND STARS 1-6 (EVERY DOME POWERED) AND LOADS THE OBSERVATORY. "
                    "USE A SPARE FILE"));
    // From the main menu (no game yet) the page only shows: its room is set on the Host page.
    if (!coop_in_game()) {
        for (SettingsRow& row : rows) {
            if (row.heading) continue;
            row.change = nullptr;
            row.press = nullptr;
            if (!row.buttons.empty()) {
                // Each button unavailable ('-' before its word).
                row.value.clear();
                for (std::string& word : row.buttons) {
                    if (word.empty() || word[0] != '-') word.insert(word.begin(), '-');
                    row.value += (row.value.empty() ? "" : "|") + word;
                }
            }
            row.help = "ONLY IN A GAME. THE ROOM IS SET ON THE MAIN MENU'S HOST PAGE";
        }
    }
    return rows;
}

// The PLAYER page: the player's name and character, theirs in every game they play (the main menu's Host and Join send
// them). The name is a text, typed in place (start_settings_text); a change applies from the next game hosted or joined.
std::vector<SettingsRow> player_page(const RuntimeSettings& runtime) {
    std::vector<SettingsRow> rows;
    const RuntimeCoopSettings& coop = runtime.coop;
    SettingsRow name{"NAME",
                     s_settings_text            ? s_settings_text->shown()
                     : coop.player_name.empty() ? std::string("PLAYER")
                                                : coop.player_name,
                     nullptr,
                     s_settings_text ? "TYPE YOUR NAME, OR PICK EACH LETTER WITH UP AND DOWN. A OR ENTER KEEPS IT, B OR "
                                       "ESCAPE DROPS IT"
                                     : "YOUR NAME OVER YOUR HEAD AND IN THE PLAYER LIST, FROM THE NEXT GAME YOU HOST OR JOIN"};
    name.names = 2u;
    name.text = true;
    rows.push_back(std::move(name));
    rows.push_back({"CHARACTER", coop.character == CoopCharacter::Luigi ? std::string("LUIGI") : std::string("MARIO"),
                    [](RenderConfig&, int delta) {
                        CoopCharacter& character = s_runtime_draft.coop.character;
                        character = static_cast<CoopCharacter>(settings_step(static_cast<unsigned>(character), 2u, delta));
                    },
                    "WHO YOU PLAY AS, IN ANY SAVE. OTHER PLAYERS SEE YOU AS HIM"});
    return rows;
}

std::vector<SettingsRow> system_page(const RenderFeatureCaps& caps) {
    std::vector<SettingsRow> rows;
    {
        std::string name = mods_ui_text(caps.adapter_description.data(), 32u);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        rows.push_back({"GRAPHICS CARD", name, nullptr, "CHOSEN IN NEBULA SETTINGS. A CHANGE APPLIES AT THE NEXT START"});
    }
    char text[64]{};
    if (caps.dxr_raytracing_tier != 0u) {
        std::snprintf(text, sizeof(text), "TIER %u.%u %s", caps.dxr_raytracing_tier / 10u, caps.dxr_raytracing_tier % 10u,
                      exposed_text(caps.dxr));
    } else {
        std::snprintf(text, sizeof(text), "NONE");
    }
    rows.push_back({"RAY TRACING HARDWARE", text, nullptr, "DIRECTX RAYTRACING ON THIS GPU"});
    rows.push_back({"NVIDIA STREAMLINE", exposed_text(caps.streamline), nullptr, "LOADS DLSS FROM THE NVIDIA DOWNLOAD"});
    rows.push_back({"NVIDIA REFLEX", exposed_text(caps.reflex), nullptr, "LOW LATENCY WITH FRAME GENERATION"});
    rows.push_back({"DLSS", exposed_text(caps.dlss_super_resolution), nullptr, "UPSCALING ON NVIDIA RTX GPUS"});
    rows.push_back({"DLSS FRAME GENERATION", exposed_text(caps.dlss_frame_generation), nullptr,
                    "GENERATED FRAMES ON NVIDIA RTX 40 AND 50 GPUS"});
    std::string post = post_process_caps_text(get_post_process_status());
    if (const std::size_t colon = post.find(": "); colon != std::string::npos) post.erase(0, colon + 2u);
    rows.push_back({"POST-PROCESS", post, nullptr, "ANTI-ALIASING, FSR 1 AND SHARPENING"});
    return rows;
}

std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps) {
    switch (tab % kNativeSettingsTabCount) {
    case 0: return display_page(draft);
    case 1: return graphics_page(draft);
    case kGeneralTab: return game_page(runtime);
    case kPlayerTab: return player_page(runtime);
    case kSystemTab: return system_page(caps);
    case kOnlineTab: return online_page(runtime);
    case kModsTab: return mods_page(draft);
    default: return {};
    }
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps) {
    char text[128]{};
    if (settings_page_tab(tab % kNativeSettingsTabCount)) {
        const auto page = settings_page(tab % kNativeSettingsTabCount, render, runtime, caps);
        if (row >= page.size()) {
            return {};
        }
        return page[row].heading ? page[row].label : page[row].label + ": " + page[row].value;
    }
    switch (tab % kNativeSettingsTabCount) {
    case 2: {
        (void)runtime;
        using galaxy::input::ControlScheme;
        const ControlsRowInfo info = controls_row(row);
        const galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
        const auto on_off = [](bool value) { return value ? "ON" : "OFF"; };
        switch (info.kind) {
        case ControlsRow::InputMode:
            switch (s_input_mode_draft) {
            case RuntimeInputMode::Controller: return "INPUT: CONTROLLER ONLY";
            case RuntimeInputMode::RealWiimote: return "INPUT: WII REMOTE";
            case RuntimeInputMode::KeyboardMouse: return "INPUT: KEYBOARD+MOUSE ONLY";
            default: return "INPUT: AUTO";
            }
        case ControlsRow::Device: {
            const galaxy::input::DeviceEntry& entry = s_controls_devices.entries[
                galaxy::input::resolve_device(s_controls_devices, s_controls_device)];
            // Bluetooth is abbreviated: the row has no room for the longest names.
            std::string line = upper_ascii(entry.label.data());
            const std::size_t bluetooth = line.find("(BLUETOOTH)");
            if (bluetooth != std::string::npos) line.replace(bluetooth, 11u, "(BT)");
            if (line.size() > 40u) line = line.substr(0, 38u) + "..";
            return "EDIT: " + line + (entry.active ? "  ACTIVE" : "");
        }
        case ControlsRow::Binding: {
            std::string line = std::string(galaxy::input::wii_target_label(info.target)) + ": ";
            if (controls_capturing() && s_capture_target == static_cast<std::size_t>(info.target)) {
                return line + "PRESS AN INPUT";
            }
            std::string bound = upper_ascii(galaxy::input::format_target_bindings(
                s_controls_scheme, s_controls_draft.table(s_controls_scheme)[info.target]));
            if (bound.size() > 34u) bound = bound.substr(0, 32u) + "..";
            return line + bound;
        }
        case ControlsRow::PointerSpeed:
            std::snprintf(text, sizeof(text), "POINTER SPEED: %.1f", static_cast<double>(tuning.pointer_speed));
            return text;
        case ControlsRow::InvertPointerY:
            return std::string("INVERT POINTER Y: ") + on_off(tuning.invert_pointer_y);
        case ControlsRow::StickDeadzone:
            std::snprintf(text, sizeof(text), "STICK DEADZONE: %u%%",
                          static_cast<unsigned>(std::lround(tuning.stick_deadzone * 100.0f)));
            return text;
        case ControlsRow::PointerDeadzone:
            std::snprintf(text, sizeof(text), "POINTER STICK DEADZONE: %u%%",
                          static_cast<unsigned>(std::lround(tuning.pointer_deadzone * 100.0f)));
            return text;
        case ControlsRow::PointerHide:
            if (tuning.pointer_hide_seconds <= 0.0f) return "HIDE STILL POINTER: NEVER";
            std::snprintf(text, sizeof(text), "HIDE STILL POINTER: %.0f S",
                          static_cast<double>(tuning.pointer_hide_seconds));
            return text;
        case ControlsRow::Vibration:
            return std::string("VIBRATION: ") + on_off(tuning.vibration);
        case ControlsRow::VibrationStrength:
            std::snprintf(text, sizeof(text), "VIBRATION STRENGTH: %u%%", static_cast<unsigned>(tuning.rumble));
            return text;
        case ControlsRow::MotionShake: {
            static constexpr const char* kMotion[] = {"OFF", "WII REMOTE", "NUNCHUK"};
            return std::string("MOTION SHAKE: ") + kMotion[static_cast<unsigned>(tuning.motion_shake) % 3u];
        }
        case ControlsRow::ShakeSensitivity:
            std::snprintf(text, sizeof(text), "SHAKE SENSITIVITY: %u", static_cast<unsigned>(tuning.shake_sensitivity));
            return text;
        case ControlsRow::GyroPointer:
            return std::string("GYRO POINTER: ") + on_off(tuning.gyro_pointer) +
                   (tuning.gyro_pointer ? "  (R STICK: CAMERA)" : "  (R STICK: POINTER)");
        case ControlsRow::GyroSensitivity:
            std::snprintf(text, sizeof(text), "GYRO SENSITIVITY: %.1f",
                          static_cast<double>(tuning.gyro_sensitivity));
            return text;
        case ControlsRow::PairJoyCon:
            return "PAIR JOY-CON";
        case ControlsRow::Reset:
            return std::string("RESET ") + galaxy::input::control_scheme_label(s_controls_scheme) + " TO DEFAULTS";
        }
        return {};
    }
    case 3:
        switch (row) {
        case 0: return std::string("MASTER: ") + percent_text(runtime.audio.master);
        case 1: return std::string("MUSIC: ") + percent_text(runtime.audio.music);
        case 2: return std::string("SFX: ") + percent_text(runtime.audio.sfx);
        case 3: return std::string("VOICE: ") + percent_text(runtime.audio.voice);
        case 4: return std::string("AMBIENCE: ") + percent_text(runtime.audio.ambience);
        case 5: return std::string("CUTSCENE: ") + percent_text(runtime.audio.cutscene);
        case 6: return std::string("WIIMOTE SOUNDS: ") + percent_text(runtime.audio.wiimote_speaker);
        default: return {};
        }
    default:
        return {};
    }
}

enum class DlssOffer : std::uint8_t { None, Ask, Downloading, Ready, Failed };
DlssOffer s_dlss_offer = DlssOffer::None;
bool s_dlss_installed_now = false;
std::uint64_t s_dlss_failed_until_ms = 0u;

#if defined(_WIN32)
HANDLE s_dlss_download = nullptr;
std::atomic<bool> s_dlss_downloading{false};

std::wstring launcher_path() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH) {
        return {};
    }
    std::wstring launcher(path, length);
    const std::size_t slash = launcher.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    launcher.resize(slash + 1u);
    launcher += L"Nebula.exe";
    return GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring{} : launcher;
}

bool streamline_files_present() {
    const std::wstring interposer = streamline::default_directory() + L"\\sl.interposer.dll";
    return GetFileAttributesW(interposer.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool start_launcher(const std::wstring& arguments, HANDLE* process_out) {
    const std::wstring launcher = launcher_path();
    if (launcher.empty()) {
        return false;
    }
    std::wstring command = L"\"" + launcher + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0u, nullptr, nullptr,
                        &startup, &process)) {
        std::cerr << "[dlss] could not start Nebula.exe: win32=" << GetLastError() << '\n';
        return false;
    }
    // Its window comes to the front over the game.
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    if (process_out != nullptr) {
        *process_out = process.hProcess;
    } else {
        CloseHandle(process.hProcess);
    }
    return true;
}

bool dlss_offered() {
    if (!kStreamlineBuilt || streamline::dlss_supported()) {
        return false;
    }
    if (s_dlss_installed_now) {
        return true;
    }
    static const bool offered = host_window::rtx_adapter.load(std::memory_order_relaxed) &&
                                !streamline_files_present() && !launcher_path().empty();
    return offered;
}

bool dlss_installed_now() {
    return s_dlss_installed_now && !streamline::dlss_supported();
}

// DLSS Frame Generation (GRAPHICS > INTERPOLATION): offered while it runs, or with DLSS once its plugins are
// installed (it loads when the game starts with it on).
bool frame_generation_offered() {
    if (streamline::frame_generation_supported()) {
        return true;
    }
    static const bool installed = [] {
        const std::wstring directory = streamline::default_directory();
        for (const wchar_t* name : {L"sl.dlss_g.dll", L"nvngx_dlssg.dll", L"sl.reflex.dll", L"sl.pcl.dll"}) {
            if (GetFileAttributesW((directory + L"\\" + name).c_str()) == INVALID_FILE_ATTRIBUTES) {
                return false;
            }
        }
        return true;
    }();
    return installed && streamline::dlss_supported();
}

// DLSS chosen before it is installed is not applied: the menu asks to download it. Once downloaded this session it
// applies and runs after a restart (FSR 1 until then).
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live) {
    if (applied.upscaler != 3u || live.upscaler == 3u || !dlss_offered() || s_dlss_installed_now) {
        return false;
    }
    applied.upscaler = live.upscaler;
    if (s_dlss_offer != DlssOffer::Downloading) {
        s_dlss_offer = DlssOffer::Ask;
    }
    return true;
}

void dlss_dismiss_prompt() {
    if (s_dlss_offer == DlssOffer::Ask || s_dlss_offer == DlssOffer::Ready) {
        s_dlss_offer = DlssOffer::None;
    }
}

void dlss_fail(const char* why) {
    if (s_settings_draft.upscaler == 3u && !s_dlss_installed_now) {
        s_settings_draft.upscaler = get_render_config().upscaler;
    }
    s_dlss_offer = DlssOffer::Failed;
    s_dlss_failed_until_ms = GetTickCount64() + 4000u;
    std::cerr << "[dlss] not installed: " << why << '\n';
}

// The launcher starts the game again once this process has exited; this one closes.
bool restart_through_launcher() {
    if (!start_launcher(L"--restart-after " + std::to_wstring(GetCurrentProcessId()), nullptr)) {
        return false;
    }
    PostMessageW(reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire)), WM_CLOSE, 0, 0);
    return true;
}

// Enter/Y and Esc/B answer the DLSS questions; other keys wait for an answer.
bool dlss_prompt_key(WPARAM key, bool was_down) {
    if (s_dlss_offer != DlssOffer::Ask && s_dlss_offer != DlssOffer::Ready) {
        return false;
    }
    if (was_down || (key != VK_RETURN && key != VK_ESCAPE)) {
        return true;
    }
    if (key == VK_ESCAPE) {
        if (s_dlss_offer == DlssOffer::Ask) {
            s_settings_draft.upscaler = get_render_config().upscaler;
        }
        s_dlss_offer = DlssOffer::None;
        return true;
    }
    if (s_dlss_offer == DlssOffer::Ask) {
        if (start_launcher(L"--install-dlss", &s_dlss_download)) {
            s_dlss_downloading.store(true, std::memory_order_release);
            s_dlss_offer = DlssOffer::Downloading;
            std::cerr << "[dlss] download started\n";
        } else {
            dlss_fail("no launcher");
        }
        return true;
    }
    // Ready: the game starts again.
    s_dlss_offer = DlssOffer::None;
    if (restart_through_launcher()) {
        std::cerr << "[dlss] restarting\n";
    }
    return true;
}
#else
// DLSS is Windows-only (Streamline); a restart is the platform's (restart_nebula).
bool dlss_offered() { return false; }
bool dlss_installed_now() { return false; }
bool frame_generation_offered() { return false; }
bool dlss_hold_back(RenderConfig&, const RenderConfig&) { return false; }
void dlss_dismiss_prompt() {}
bool restart_through_launcher() { return restart_nebula(); }
bool dlss_prompt_key(WPARAM, bool) { return false; }
#endif

// After mod changes were applied: Enter, Space or Y restarts the game now; Escape, Backspace or B keeps them for the
// next start. Every other key waits for the answer.
bool mods_restart_key(WPARAM key, bool was_down) {
    if (!s_mods_restart_ask.load(std::memory_order_acquire)) return false;
    if (was_down) return true;
    if (key == VK_RETURN || key == VK_SPACE) {
        s_mods_restart_ask.store(false, std::memory_order_release);
        if (restart_through_launcher()) {
            std::cerr << "[mods] restarting\n";
        } else {
            show_controls_message("COULD NOT RESTART");
        }
    } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
        s_mods_restart_ask.store(false, std::memory_order_release);
    }
    return true;
}

// Render thread, every frame: notices when the download window closes.
void poll_dlss_download() {
#if defined(_WIN32)
    if (!s_dlss_downloading.load(std::memory_order_acquire)) {
        return;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (s_dlss_download == nullptr || WaitForSingleObject(s_dlss_download, 0u) != WAIT_OBJECT_0) {
        return;
    }
    DWORD code = 1u;
    GetExitCodeProcess(s_dlss_download, &code);
    CloseHandle(s_dlss_download);
    s_dlss_download = nullptr;
    s_dlss_downloading.store(false, std::memory_order_release);
    if (code != 0u || !streamline_files_present()) {
        dlss_fail(code == 1u ? "closed" : "download failed");
        return;
    }
    s_dlss_installed_now = true;
    RenderConfig render = get_render_config();
    render.upscaler = 3u;
    set_render_config(render);
    s_settings_draft.upscaler = 3u;
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    s_dlss_offer = DlssOffer::Ready;
    std::cerr << "[dlss] installed; it starts with the next launch\n";
#endif
}

std::string dlss_footer(std::uint64_t now_ms, bool& error) {
    switch (s_dlss_offer) {
    case DlssOffer::Ask:
        return "DOWNLOAD NVIDIA DLSS (" + std::to_string(kDlssDownloadMegabytes) + " MB)? ENTER/Y YES  ESC/B NO";
    case DlssOffer::Downloading:
        return "INSTALLING NVIDIA DLSS...";
    case DlssOffer::Ready:
        return "DLSS READY. RESTART NOW? ENTER/Y YES  ESC/B LATER";
    case DlssOffer::Failed:
        if (now_ms < s_dlss_failed_until_ms) {
            error = true;
            return "DLSS WAS NOT INSTALLED";
        }
        s_dlss_offer = DlssOffer::None;
        return {};
    default:
        return {};
    }
}

// The DLSS offer as the menu's status: its two questions, the install, a failure for a moment; Help when there is
// none.
NativeSettingsStatus dlss_status(std::uint64_t now_ms) {
    bool error = false;
    if (dlss_footer(now_ms, error).empty()) {
        return NativeSettingsStatus::Help;
    }
    switch (s_dlss_offer) {
    case DlssOffer::Ask: return NativeSettingsStatus::DlssAsk;
    case DlssOffer::Downloading: return NativeSettingsStatus::DlssInstalling;
    case DlssOffer::Ready: return NativeSettingsStatus::DlssRestart;
    default: return NativeSettingsStatus::DlssFailed;
    }
}
