// SPDX-License-Identifier: GPL-3.0-only
// The game's Win32 window on both renderers (host_window_win32.h): the window procedure, the pointer, the settings
// overlay and HOME Menu (state, keys, drawing through a renderer's UiCanvas) and the host API that reads them.

#include "galaxy/gx/host_window_win32.h"
#include "galaxy/controller_hid.h"

#include "galaxy/gx/display_modes.h"
#include "galaxy/coop_menu.h"
#include "galaxy/gx/galaxy_select.h"
#include "galaxy/gx/post_process_settings.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/streamline_bridge.h"
#include "galaxy/gx/texture_pack.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/mii_import.h"
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"
#include "galaxy/native_host.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

#include <shellapi.h>
#include <shobjidl.h>

namespace galaxy::gx {

namespace host_window {

UINT backbuffer_width = 1280;
UINT backbuffer_height = 720;
std::atomic<double> measured_present_hz{0.0};
std::atomic<std::int32_t> pointer_wheel{0};
std::string native_settings_content_root;
std::atomic<std::uintptr_t> settings_window{0};
std::atomic<bool> borderless_active{false};
std::atomic<bool> exclusive_fullscreen_active{false};
std::atomic<unsigned> efb_scale_rejected{0u};
std::atomic<std::uintptr_t> pointer_hwnd{0};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;
std::atomic<bool> follow_window_rearm{false};
// RendererD3D12::suspend_temporal_interpolation (the FRAME RATE row says so).
std::atomic<bool> temporal_interp_suspended{false};
// The session's GPU is an NVIDIA RTX (RendererD3D12::initialize): DLSS can be offered.
std::atomic<bool> rtx_adapter{false};

}  // namespace host_window

using host_window::display_efb_scale_cap;
using host_window::flush_native_settings_if_dirty;
using host_window::poll_render_config_file;
using host_window::hidden_window_mode_enabled;
using host_window::windowed_rect_on_monitor;
using host_window::wnd_proc;
using host_window::kSettingsKeyMessage;
using host_window::kWindowStyle;
using host_window::kPlaceWindowMessage;
using host_window::kTestResizeMessage;
using host_window::WindowPlacement;

namespace {

#include "native_settings_model.inl"


int signed_lparam_x(LPARAM lp) {
    return static_cast<int>(static_cast<short>(lp & 0xFFFF));
}

int signed_lparam_y(LPARAM lp) {
    return static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
}

constexpr galaxy::host::HostPointerCursorPollMode kCursorPollMode =
    galaxy::host::HostPointerCursorPollMode::Adaptive;
// A client-area mouse message counts as recent for 120 ms; a centered cursor sample after one is rejected as stale for
// 5 s (classify_host_pointer_client_message_timing).
constexpr std::uint64_t kPointerMessageRecentMs = 120u;
constexpr std::uint64_t kStaleCenterRejectMs = 5000u;

void maybe_hide_os_cursor() {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const bool focused =
        s_pointer_focused.load(std::memory_order_relaxed) ||
        (hwnd != nullptr && GetForegroundWindow() == hwnd);
    if (focused) {
        SetCursor(nullptr);
    }
}

galaxy::host::HostPointerCoordinates cached_pointer_coordinates(
    bool inside_override) noexcept {
    const galaxy::host::HostPointerSnapshot snapshot =
        s_pointer_snapshot.snapshot();
    galaxy::host::HostPointerCoordinates coordinates = snapshot.coordinates;
    coordinates.inside_client = inside_override && coordinates.absolute_valid;
    return coordinates;
}

struct AcquiredHostPointerCoordinates {
    galaxy::host::HostPointerCoordinates coordinates{};
    std::uint64_t absolute_acquired_ms{};
};

void mark_pointer_outside() noexcept {
    (void)s_pointer_snapshot.update(
        [](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
        });
}

void clear_pointer_buttons() noexcept {
    s_pointer_left_button.store(false, std::memory_order_relaxed);
    s_pointer_right_button.store(false, std::memory_order_relaxed);
}

void invalidate_pointer_focus_once() noexcept {
    if (s_pointer_focus_valid.exchange(false, std::memory_order_acq_rel)) {
        s_pointer_transitions.invalidate_focus(
            cached_pointer_coordinates(true),
            static_cast<std::uint64_t>(GetTickCount64()));
    }
}

void clear_pointer_window_state(HWND hwnd) noexcept {
    // Losing focus or capture releases the buttons, not the pointer: the cursor poll stays authoritative while the
    // window is unfocused, and only WM_MOUSELEAVE or a sample outside the window reports the pointer lost.
    clear_pointer_buttons();
    if (hwnd != nullptr && GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

bool seed_pointer_from_client(
    HWND hwnd,
    bool force_center) {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return false;
    }

    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerSnapshot seeded_snapshot =
        s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_seed(
                snapshot,
                {
                    width,
                    height,
                    force_center,
                    pointer_now_ms,
                });
        });

    // A stationary cursor inside the window sends no WM_MOUSEMOVE after launch, resize or refocus, and the seed above
    // is inactive: publish the real cursor now, with the first-poll fallback's compare-before-publish rule, so the
    // synthetic seed does not stand in for it.
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const auto poll_authority =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            kCursorPollMode, remote_session, false);
    POINT cursor{};
    if (poll_authority.poll_enabled &&
        poll_authority.authoritative && GetCursorPos(&cursor) &&
        ScreenToClient(hwnd, &cursor) && cursor.x >= 0 && cursor.y >= 0 &&
        cursor.x < width && cursor.y < height) {
        const int cursor_x =
            std::clamp(static_cast<int>(cursor.x), 0, width - 1);
        const int cursor_y =
            std::clamp(static_cast<int>(cursor.y), 0, height - 1);
        bool publication_accepted = false;
        (void)s_pointer_snapshot.update(
            [&](galaxy::host::HostPointerSnapshot& current) noexcept {
                publication_accepted =
                    galaxy::host::apply_host_pointer_fallback_inside_publication(
                        current,
                        {
                            cursor_x,
                            cursor_y,
                            width,
                            height,
                            pointer_now_ms,
                            seeded_snapshot.event_sequence,
                            seeded_snapshot.absolute_sequence,
                        }) == galaxy::host::
                            HostPointerFallbackInsidePublicationResult::
                                PublishedInside;
            });
        if (publication_accepted) {
            s_pointer_last_cursor_poll_x.store(
                cursor_x, std::memory_order_relaxed);
            s_pointer_last_cursor_poll_y.store(
                cursor_y, std::memory_order_relaxed);
        }
    }
    return true;
}

AcquiredHostPointerCoordinates update_pointer_absolute(
    HWND hwnd,
    LPARAM lp) {
    galaxy::host::HostPointerCoordinates coordinates{};
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return {};
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return {};
    }

    const int raw_x = signed_lparam_x(lp);
    const int raw_y = signed_lparam_y(lp);
    const bool inside =
        raw_x >= 0 && raw_y >= 0 && raw_x < width && raw_y < height;
    coordinates = {
        true,
        inside,
        raw_x,
        raw_y,
        width,
        height,
    };
    const std::uint64_t now_ms = static_cast<std::uint64_t>(GetTickCount64());
    // The time the message was queued, not dispatched.
    const DWORD message_age = static_cast<DWORD>(now_ms) -
        static_cast<DWORD>(GetMessageTime());
    const std::uint64_t pointer_now_ms = message_age <= 0x7fffffffu && message_age <= now_ms
        ? now_ms - message_age : 0u;
    (void)s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_client_coordinate(
                snapshot,
                {
                    raw_x,
                    raw_y,
                    width,
                    height,
                    pointer_now_ms,
                });
        });

    if (inside && !s_tracking_mouse_leave) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        s_tracking_mouse_leave = TrackMouseEvent(&tme) != FALSE;
    }
    return {coordinates, pointer_now_ms};
}

void update_pointer_button(HWND hwnd, LPARAM lp, bool left, bool down) {
    const AcquiredHostPointerCoordinates publication =
        update_pointer_absolute(hwnd, lp);
    if (left) {
        s_pointer_left_button.store(down, std::memory_order_relaxed);
    } else {
        s_pointer_right_button.store(down, std::memory_order_relaxed);
    }
    s_pointer_transitions.accept_button_transition(
        left ? galaxy::host::HostPointerButton::Left
             : galaxy::host::HostPointerButton::Right,
        down,
        publication.coordinates,
        publication.absolute_acquired_ms);
}

// The input message being handled came from SendInput, not a device.
bool input_message_injected() noexcept {
    INPUT_MESSAGE_SOURCE source{};
    return GetCurrentInputMessageSource(&source) && source.originId == IMO_INJECTED;
}

// While the game window is in front the display stays on and the PC stays awake: Windows does not count controller
// input as activity. Window thread only.
void hold_display_on(bool hold) noexcept {
    static const HANDLE request = [] {
        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString = const_cast<LPWSTR>(L"Nebula is running");
        return PowerCreateRequest(&reason);
    }();
    static bool held = false;
    if (request == INVALID_HANDLE_VALUE || hold == held || (hold && hidden_window_mode_enabled())) return;
    if (hold) {
        (void)PowerSetRequest(request, PowerRequestDisplayRequired);
        (void)PowerSetRequest(request, PowerRequestSystemRequired);
    } else {
        (void)PowerClearRequest(request, PowerRequestDisplayRequired);
        (void)PowerClearRequest(request, PowerRequestSystemRequired);
    }
    held = hold;
}

#include "native_settings_pages.inl"


}  // namespace

namespace host_window {

// The internal resolution that fills the display: the smallest scale whose 640x456 game picture covers the monitor's
// current mode, or the swap chain in exclusive fullscreen. Higher scales cost GPU time and memory without adding
// pixels the display can show.
unsigned display_efb_scale_cap() {
    UINT width = 0u;
    UINT height = 0u;
    if (host_window::exclusive_fullscreen_active.load(std::memory_order_acquire)) {
        width = host_window::backbuffer_width;
        height = host_window::backbuffer_height;
    } else {
        const HWND window = reinterpret_cast<HWND>(
            host_window::settings_window.load(std::memory_order_acquire));
        const HMONITOR monitor = window != nullptr
            ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                mode.dmPelsWidth != 0u && mode.dmPelsHeight != 0u) {
                width = mode.dmPelsWidth;
                height = mode.dmPelsHeight;
            } else {
                width = static_cast<UINT>(info.rcMonitor.right - info.rcMonitor.left);
                height = static_cast<UINT>(info.rcMonitor.bottom - info.rcMonitor.top);
            }
        }
    }
    if (width == 0u || height == 0u) {
        return kMaxEfbScale;
    }
    const double needed = std::max(width / static_cast<double>(kEfbWidth), height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
}

#include "native_settings_host.inl"


bool hidden_window_mode_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_HIDE_WINDOW") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

unsigned shown_test_window_mode() {
    static const unsigned mode = [] {
        char value[16]{};
        std::size_t length = 0;
        if (!hidden_window_mode_enabled() ||
            getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SHOWN_WINDOW") != 0 || length <= 1) {
            return 0u;
        }
        return std::strcmp(value, "corner") == 0 ? 2u : std::strcmp(value, "edge") == 0 ? 1u : 0u;
    }();
    return mode;
}

// The window's outer rect for a windowed client size on a monitor: limited to
// the monitor's work area and centered in it.
RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width,
                              unsigned height) {
    RECT frame{0, 0, 0, 0};
    const HMONITOR handle = MonitorFromPoint(
        POINT{monitor.bounds.left + 1, monitor.bounds.top + 1}, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 96u;
    UINT dpi_y = 96u;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const auto get_dpi = reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"));
    if (get_dpi == nullptr || FAILED(get_dpi(handle, 0, &dpi_x, &dpi_y))) {
        dpi_x = GetDpiForSystem();
    }
    (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, dpi_x);
    const long frame_w = frame.right - frame.left;
    const long frame_h = frame.bottom - frame.top;
    const long work_w = monitor.work_area.right - monitor.work_area.left;
    const long work_h = monitor.work_area.bottom - monitor.work_area.top;
    const long client_w = std::clamp<long>(static_cast<long>(width), 320, std::max(320L, work_w - frame_w));
    const long client_h = std::clamp<long>(static_cast<long>(height), 240, std::max(240L, work_h - frame_h));
    const long outer_w = client_w + frame_w;
    const long outer_h = client_h + frame_h;
    const long left = monitor.work_area.left + std::max(0L, (work_w - outer_w) / 2);
    const long top = monitor.work_area.top + std::max(0L, (work_h - outer_h) / 2);
    return RECT{left, top, left + outer_w, top + outer_h};
}

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    OpenedWindow opened;
    quit_request.store(false, std::memory_order_release);
    const bool hidden_window = hidden_window_mode_enabled();
    // The configured monitor and display mode. Exclusive fullscreen starts as borderless on that monitor; the first
    // frame's display check switches the mode once the swap chain exists. Hidden test windows stay windowed.
    const RenderConfig start_config = get_render_config();
    const unsigned start_mode = hidden_window
        ? kDisplayWindowed : std::min(start_config.display_mode, kDisplayExclusive);
    const bool fullscreen_window = start_mode != kDisplayWindowed;
    const std::vector<MonitorInfo> start_monitors = enumerate_monitors();
    store_monitor_cache(start_monitors);
    const MonitorInfo* start_monitor =
        find_monitor(start_monitors, widen_utf8(start_config.monitor.data()));
    int actual_window_width = window_width;
    int actual_window_height = window_height;
    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    DWORD window_style = kWindowStyle;

    if (fullscreen_window && start_monitor != nullptr) {
        const RECT& bounds = start_monitor->bounds;
        window_x = bounds.left;
        window_y = bounds.top;
        actual_window_width = static_cast<int>(bounds.right - bounds.left);
        actual_window_height = static_cast<int>(bounds.bottom - bounds.top);
        if (actual_window_width <= 0 || actual_window_height <= 0) {
            std::cerr << "[Renderer] invalid monitor dimensions\n";
            return {};
        }
        window_style = WS_POPUP;
    } else if (!hidden_window && start_monitor != nullptr && window_width > 0 &&
               window_height > 0) {
        // Windowed: limited to the monitor's work area and centered on it.
        const RECT outer = windowed_rect_on_monitor(
            *start_monitor, static_cast<unsigned>(window_width),
            static_cast<unsigned>(window_height));
        RECT frame{0, 0, 0, 0};
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW,
                                       GetDpiForSystem());
        window_x = outer.left;
        window_y = outer.top;
        actual_window_width = static_cast<int>(
            (outer.right - outer.left) - (frame.right - frame.left));
        actual_window_height = static_cast<int>(
            (outer.bottom - outer.top) - (frame.bottom - frame.top));
    }

    // A test window shown on the main monitor (GALAXY_TEST_SHOWN_WINDOW), so its frames reach the display as a
    // player's do and Windows captures them: "corner" whole in the work area's bottom-right corner, "edge" with one
    // pixel there. Never focused, so nobody types into it.
    if (const unsigned shown = shown_test_window_mode(); shown != 0u) {
        RECT work{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == 0) {
            work = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        window_x = shown == 2u ? work.right - actual_window_width : work.right - 1;
        window_y = shown == 2u ? work.bottom - actual_window_height : work.bottom - 1;
    }

    if (actual_window_width <= 0 || actual_window_height <= 0) {
        std::cerr << "[Renderer] invalid client dimensions\n";
        return {};
    }

    backbuffer_width   = static_cast<UINT>(actual_window_width);
    backbuffer_height  = static_cast<UINT>(actual_window_height);

    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        // The arrow while the window is unfocused; WM_SETCURSOR hides it while the game has focus.
        wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
        wc.hIcon         = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
        wc.hIconSm       = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        RECT frame{0, 0, 0, 0};
        if (!fullscreen_window) {
            if (!AdjustWindowRectExForDpi(&frame, window_style, FALSE, WS_EX_APPWINDOW, GetDpiForSystem())) {
                std::cerr << "[Renderer] AdjustWindowRect failed\n";
                return {};
            }
        }
        const std::int64_t outer_width =
            static_cast<std::int64_t>(actual_window_width) +
            static_cast<std::int64_t>(frame.right) - frame.left;
        const std::int64_t outer_height =
            static_cast<std::int64_t>(actual_window_height) +
            static_cast<std::int64_t>(frame.bottom) - frame.top;
        if (outer_width <= 0 || outer_height <= 0 ||
            outer_width > std::numeric_limits<int>::max() ||
            outer_height > std::numeric_limits<int>::max()) {
            std::cerr << "[Renderer] adjusted window dimensions exceed Win32 limits\n";
            return {};
        }
        // Files dropped on the window import (WM_DROPFILES).
        const DWORD ex_style =
            hidden_window ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
                          : (WS_EX_APPWINDOW | WS_EX_ACCEPTFILES);
        opened.window = owner.start({
            kWindowClass, L"Nebula", window_style, ex_style,
            window_x, window_y, static_cast<int>(outer_width),
            static_cast<int>(outer_height), !hidden_window});
        if (opened.window == nullptr) {
            std::cerr << "[Renderer] CreateWindow failed\n";
            return {};
        }
        settings_window.store(
            reinterpret_cast<std::uintptr_t>(opened.window), std::memory_order_release);
        borderless_active.store(fullscreen_window, std::memory_order_release);
        // No pointer seed from this thread: the window thread publishes geometry and input through WM_SIZE and
        // WM_SETFOCUS, and the first real message or cursor poll places the pointer.
        opened.shown = !hidden_window;
        if (shown_test_window_mode() != 0u) {
            ShowWindowAsync(opened.window, SW_SHOWNOACTIVATE);
        }
    }
    return opened;
}

void close_game_window(WindowOwner& owner) noexcept {
    owner.stop();
    pointer_hwnd.store(0u, std::memory_order_release);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void show_window(NativeWindow window) noexcept {
    ShowWindowAsync(window, SW_SHOWNORMAL);
}

bool present_grid(std::int64_t&, std::int64_t&, std::int64_t&, std::int64_t&) noexcept {
    return false;  // the presents pace themselves (present_pacer)
}

// The vblanks' own rate when they are timed (the mode reports whole hertz: 239 or 240 for 239.76).
double display_refresh_hz(NativeWindow window) noexcept {
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    if (vblank_grid(window, vblank, period)) {
        return std::round(static_cast<double>(galaxy::qpc_frequency()) / static_cast<double>(period) * 100.0) / 100.0;
    }
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info) ||
        !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1u) {
        return 0.0;
    }
    return static_cast<double>(mode.dmDisplayFrequency);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DROPFILES) {
        // Dropped files: mod archives and folders into the mods folder (the MODS page opens on the import),
        // anything else to the Mii import.
        const auto drop = reinterpret_cast<HDROP>(wp);
        std::vector<std::filesystem::path> mods;
        std::vector<std::filesystem::path> miis;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0u);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0u);
            std::wstring name(length + 1u, L'\0');
            DragQueryFileW(drop, i, name.data(), length + 1u);
            name.resize(length);
            const std::filesystem::path path(name);
            std::error_code error;
            if (std::filesystem::is_directory(path, error) || galaxy::mods::is_mod_archive(path)) {
                mods.push_back(path);
            } else {
                miis.push_back(path);
            }
        }
        DragFinish(drop);
        if (!mods.empty()) {
            run_mod_imports(std::move(mods));
            s_native_settings_tab.store(kModsTab, std::memory_order_relaxed);
            s_native_settings_row.store(1u, std::memory_order_relaxed);
            set_native_settings_visible(true);
        }
        if (!miis.empty()) {
            std::thread([miis = std::move(miis)] {
                std::string message;
                (void)galaxy::host::import_mii_files(miis, &message);
                std::cerr << "[settings] mii import (dropped): " << message << '\n';
                {
                    const std::lock_guard<std::mutex> lock(s_mii_mutex);
                    s_mii_status = message;
                }
                s_mii_count.store(-1, std::memory_order_release);
            }).detach();
        }
        return 0;
    }
    if (msg == kSettingsKeyMessage) {
        (void)handle_native_settings_key(wp, false, true);
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        store_monitor_cache(enumerate_monitors());
    }
    if (msg == kTestResizeMessage) {
        RECT rect{0, 0, static_cast<LONG>(wp), static_cast<LONG>(lp)};
        (void)AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        (void)SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (msg == kPlaceWindowMessage) {
        const auto* placement = reinterpret_cast<const WindowPlacement*>(lp);
        if (placement != nullptr) {
            // Keep the window's visibility (hidden test windows stay hidden).
            const LONG_PTR visible = GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE;
            (void)SetWindowLongPtrW(
                hwnd, GWL_STYLE, static_cast<LONG_PTR>(placement->style) | visible);
            (void)SetWindowPos(
                hwnd, placement->borderless ? HWND_TOP : nullptr,
                placement->rect.left, placement->rect.top,
                placement->rect.right - placement->rect.left,
                placement->rect.bottom - placement->rect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE |
                    (placement->borderless ? 0u : SWP_NOZORDER));
            host_window::borderless_active.store(placement->borderless, std::memory_order_release);
        }
        return 0;
    }
    if (msg == WM_CREATE) {
        host_window::pointer_hwnd.store(reinterpret_cast<std::uintptr_t>(hwnd),
                             std::memory_order_release);
    }
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE) {
        (msg == WM_ENTERMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(true, std::memory_order_release);
        galaxy::host::clear_native_window_key_presses();
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
    }
    if (msg == WM_EXITSIZEMOVE &&
        !host_window::borderless_active.load(std::memory_order_acquire) &&
        !host_window::exclusive_fullscreen_active.load(std::memory_order_acquire)) {
        // A size the player drags the window to becomes the windowed size.
        RECT client{};
        if (GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
            RenderConfig render = get_render_config();
            if (render.window_width != static_cast<unsigned>(client.right) ||
                render.window_height != static_cast<unsigned>(client.bottom)) {
                render.window_width = static_cast<unsigned>(client.right);
                render.window_height = static_cast<unsigned>(client.bottom);
                set_render_config(render);
                s_native_settings_dirty.store(true, std::memory_order_release);
                // Saved now, not when the game closes: Nebula Settings reads the file meanwhile.
                if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
                    flush_native_settings_if_dirty();
                }
            }
        }
    }
    if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE) {
        (msg == WM_EXITMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(false, std::memory_order_release);
        s_pointer_focus_valid.store(GetForegroundWindow() == hwnd,
                                    std::memory_order_release);
        seed_pointer_from_client(hwnd, false);
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        // Alt+Space belongs to the native system menu, not the game's A key.
        if (msg == WM_SYSKEYDOWN && wp == VK_SPACE) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        const bool was_down =
            (static_cast<std::uintptr_t>(lp) & (std::uintptr_t{1} << 30u)) !=
            0u;
        {
            const UINT raw = static_cast<UINT>(wp);
            const UINT shift = raw == VK_SHIFT
                ? MapVirtualKeyA((static_cast<UINT>(lp) >> 16u) & 0xffu, MAPVK_VSC_TO_VK_EX) : 0u;
            const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
            s_handled_key_normalized = galaxy::input::normalize_native_window_key(
                raw, shift, (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
        }
        if (native_settings_handle_window_key(wp, was_down)) {
            return 0;
        }
        // T in a co-op game: the chat's line opens (galaxy/coop_chat.h), and the game doesn't see the key.
        if (!was_down && coop_chat_key(static_cast<std::uint32_t>(wp))) {
            return 0;
        }
        // A main menu text row being edited: the editing keys go to it (characters come as WM_CHAR), nothing to the
        // game.
        if (coop_text_entry()) {
            switch (wp) {
            case VK_BACK: push_coop_text(galaxy::coop::kTextBackspace); break;
            case VK_TAB: push_coop_text(galaxy::coop::kTextTab); break;
            case VK_DELETE: push_coop_text(galaxy::coop::kTextDelete); break;
            case VK_RETURN: push_coop_text(galaxy::coop::kTextEnter); break;
            case VK_ESCAPE: push_coop_text(galaxy::coop::kTextEscape); break;
            case VK_LEFT: push_coop_text(galaxy::coop::kTextLeft); break;
            case VK_RIGHT: push_coop_text(galaxy::coop::kTextRight); break;
            case VK_UP: push_coop_text(galaxy::coop::kTextUp); break;
            case VK_DOWN: push_coop_text(galaxy::coop::kTextDown); break;
            case VK_HOME: push_coop_text(galaxy::coop::kTextHome); break;
            case VK_END: push_coop_text(galaxy::coop::kTextEnd); break;
            default: return DefWindowProcW(hwnd, msg, wp, lp);
            }
            return 0;
        }
        if (wp == VK_TAB) s_coop_keys_held.fetch_or(1u, std::memory_order_relaxed);
        if (wp == VK_ESCAPE) s_coop_keys_held.fetch_or(2u, std::memory_order_relaxed);
        if (!galaxy::host::native_keyboard_capture_active()) {
            const UINT key = static_cast<UINT>(wp);
            const UINT mapped_shift = key == VK_SHIFT
                ? MapVirtualKeyA(
                    (static_cast<UINT>(lp) >> 16u) & 0xffu,
                    MAPVK_VSC_TO_VK_EX) : 0u;
            const std::uint32_t normalized =
                galaxy::input::normalize_native_window_key(key, mapped_shift,
                    (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
            if (!was_down) {
                galaxy::host::publish_native_window_input_origin(normalized, input_message_injected());
                galaxy::host::publish_native_window_key_press(normalized);
            }
            galaxy::host::publish_native_window_key_state(normalized, true);
        }
    }
    if (msg == WM_CHAR && native_settings_type(static_cast<char32_t>(wp))) {
        // Typed into the settings' PLAYER page name (its keys are the menu's).
        return 0;
    }
    if (msg == WM_CHAR && coop_text_entry()) {
        // Typed into the main menu's text row (Backspace, Enter and Escape come as their keys).
        if (wp >= 0x20u && wp != 0x7Fu) push_coop_text(static_cast<char32_t>(wp));
        return 0;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        const UINT key = static_cast<UINT>(wp);
        if (key == VK_TAB) s_coop_keys_held.fetch_and(~1u, std::memory_order_relaxed);
        if (key == VK_ESCAPE) s_coop_keys_held.fetch_and(~2u, std::memory_order_relaxed);
        const UINT mapped_shift = key == VK_SHIFT
            ? MapVirtualKeyA(
                (static_cast<UINT>(lp) >> 16u) & 0xffu,
                MAPVK_VSC_TO_VK_EX) : 0u;
        galaxy::host::publish_native_window_key_state(
            galaxy::input::normalize_native_window_key(key, mapped_shift,
                (static_cast<UINT>(lp) & (1u << 24u)) != 0u),
            false);
    }
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        const bool focused =
            s_pointer_focused.load(std::memory_order_relaxed) ||
            GetForegroundWindow() == hwnd;
        if (focused) {
            SetCursor(nullptr);
            return TRUE;
        }
    }
    if (msg == WM_MOUSEMOVE) {
        galaxy::host::publish_native_pointer_input_origin(false, input_message_injected());
        update_pointer_absolute(hwnd, lp);
        maybe_hide_os_cursor();
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Left)) ||
        (msg == WM_RBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Right)) ||
        (msg == WM_MBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Middle)) ||
        (msg == WM_XBUTTONDOWN &&
         native_settings_capture_mouse_button(GET_XBUTTON_WPARAM(wp) == XBUTTON1
                                                  ? galaxy::input::MouseButton::X1
                                                  : galaxy::input::MouseButton::X2))) {
        return msg == WM_XBUTTONDOWN ? TRUE : 0;
    }
    if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        // Middle and side buttons are bindable like keys (VK_MBUTTON, VK_XBUTTON*).
        const bool down = msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
        const std::uint32_t vk = (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP)
            ? VK_MBUTTON
            : (GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
        if (!galaxy::host::native_keyboard_capture_active()) {
            if (down) galaxy::host::publish_native_window_input_origin(vk, input_message_injected());
            if (down) galaxy::host::publish_native_window_key_press(vk);
            galaxy::host::publish_native_window_key_state(vk, down);
        }
        return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) ? TRUE : 0;
    }
    if (msg == WM_MOUSEWHEEL && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // The wheel moves the settings menu's selection a row a notch, as the arrow keys; never while a binding
        // waits for its input.
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        if (controls_capturing()) return 0;
        static int s_wheel = 0;
        s_wheel += GET_WHEEL_DELTA_WPARAM(wp);
        for (; s_wheel >= WHEEL_DELTA; s_wheel -= WHEEL_DELTA) (void)handle_native_settings_key(VK_UP, false);
        for (; s_wheel <= -WHEEL_DELTA; s_wheel += WHEEL_DELTA) (void)handle_native_settings_key(VK_DOWN, false);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        // The game's own screens scroll by it (the achievement board).
        host_window::pointer_wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp), std::memory_order_relaxed);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, true, true);
        // Win32 delivers a release outside the client only to the window that captured the mouse; a lost release
        // would leave the Wii Remote's A or B held.
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        update_pointer_button(hwnd, lp, true, false);
        if (!s_pointer_right_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, false, true);
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        update_pointer_button(hwnd, lp, false, false);
        if (!s_pointer_left_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
        const bool had_pressed_button =
            s_pointer_left_button.load(std::memory_order_relaxed) ||
            s_pointer_right_button.load(std::memory_order_relaxed);
        const galaxy::host::HostPointerCaptureCleanupDecision cleanup =
            galaxy::host::select_host_pointer_capture_cleanup(
                msg == WM_CANCELMODE,
                had_pressed_button);
        if (cleanup.publish_cancel_all) {
            s_pointer_transitions.cancel_unexpected_capture(
                cached_pointer_coordinates(false),
                static_cast<std::uint64_t>(GetTickCount64()));
        }
        if (cleanup.clear_window_state) {
            clear_pointer_window_state(hwnd);
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        s_tracking_mouse_leave = false;
        mark_pointer_outside();
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        const auto* suggested=reinterpret_cast<const RECT*>(lp);
        if (suggested) SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
            suggested->right-suggested->left,suggested->bottom-suggested->top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        return 0;
    }
    if (msg == WM_SIZE) {
        seed_pointer_from_client(hwnd, false);
        // Minimizing reports a 0x0 (or near-0) client, which ResizeBuffers would refuse.
        if (wp != SIZE_MINIMIZED) {
            const int new_width = LOWORD(lp);
            const int new_height = HIWORD(lp);
            if (new_width > 0 && new_height > 0) {
                host_window::pending_resize_extent.store(
                    (static_cast<std::uint64_t>(new_width) << 32u) |
                    static_cast<std::uint32_t>(new_height), std::memory_order_release);
            }
        }
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        RECT client{};
        if (GetClientRect(hwnd, &client)) {
            FillRect(
                reinterpret_cast<HDC>(wp),
                &client,
                static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        return 1;
    }
    if (msg == WM_SETFOCUS) {
        s_pointer_focused.store(true, std::memory_order_relaxed);
        s_pointer_focus_valid.store(true, std::memory_order_release);
        seed_pointer_from_client(hwnd, false);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_MOVE) {
        (void)s_pointer_snapshot.update([](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::invalidate_host_pointer_geometry(snapshot);
        });
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        s_coop_keys_held.store(0u, std::memory_order_relaxed);
        galaxy::host::clear_native_window_key_presses();
        s_pointer_focused.store(false, std::memory_order_relaxed);
        invalidate_pointer_focus_once();
        clear_pointer_window_state(hwnd);
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        return 0;
    }
    if (msg == WM_ACTIVATEAPP) {
        s_pointer_focused.store(wp != 0, std::memory_order_relaxed);
        hold_display_on(wp != 0);
        if (wp != 0) {
            s_pointer_focus_valid.store(true, std::memory_order_release);
            seed_pointer_from_client(hwnd, false);
            maybe_hide_os_cursor();
        } else {
            galaxy::host::clear_native_window_key_presses();
            invalidate_pointer_focus_once();
            clear_pointer_window_state(hwnd);
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        save_native_settings_now();
        host_window::quit_request.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        save_native_settings_now();
        host_window::quit_request.store(true, std::memory_order_release);
        hold_display_on(false);
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
        host_window::pointer_hwnd.store(0u, std::memory_order_release);
        s_system_menu_active.store(false, std::memory_order_release);
        host_window::move_size_active.store(false, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

NativeSettingsView build_native_settings_view();

void draw_native_settings_overlay(UiCanvas& renderer) {
    poll_dlss_download();
    {
        // GALAXY_TEST_SETTINGS_OPEN=<page>[:<present>]: hidden test runs open the menu on that page
        // once, at that present.
        static int test_page = -1;
        static unsigned long test_present = 0u;
        static const bool test_read = [] {
            char value[32]{};
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SETTINGS_OPEN") == 0 && length > 1u) {
                char* end = nullptr;
                test_page = static_cast<int>(std::strtol(value, &end, 10));
                if (end != nullptr && *end == ':') test_present = std::strtoul(end + 1, nullptr, 10);
            }
            return true;
        }();
        (void)test_read;
        static unsigned long presents = 0u;
        static bool test_opened = false;
        if (test_page >= 0 && !test_opened && ++presents >= test_present) {
            test_opened = true;
            const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
            s_native_settings_tab.store(static_cast<unsigned>(test_page) % kNativeSettingsTabCount,
                                        std::memory_order_relaxed);
            s_native_settings_row.store(0u, std::memory_order_relaxed);
            set_native_settings_visible(true);
        }
        // GALAXY_TEST_SETTINGS_KEYS=<present>:<key>,...: keys pressed in the menu at those presents (up, down, left,
        // right, enter, esc, tab, backspace, space; f1 opens and closes the menu).
        static const std::vector<std::pair<unsigned long, WPARAM>> test_keys = [] {
            std::vector<std::pair<unsigned long, WPARAM>> keys;
            static char value[32768]{};  // a walk through every page's rows runs to some 500 keys
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SETTINGS_KEYS") != 0 || length <= 1u) return keys;
            static constexpr std::pair<std::string_view, WPARAM> kNames[] = {
                {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"enter", VK_RETURN},
                {"esc", VK_ESCAPE}, {"tab", VK_TAB}, {"backspace", VK_BACK}, {"space", VK_SPACE}, {"f1", VK_F1}};
            std::string_view rest(value);
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view item = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1u);
                const std::size_t colon = item.find(':');
                if (colon == std::string_view::npos) continue;
                const unsigned long at = std::strtoul(std::string(item.substr(0, colon)).c_str(), nullptr, 10);
                for (const auto& [name, key] : kNames) {
                    if (item.substr(colon + 1u) == name) keys.emplace_back(at, key);
                }
            }
            return keys;
        }();
        static unsigned long key_presents = 0u;
        static std::size_t next_key = 0u;
        ++key_presents;
        while (next_key < test_keys.size() && key_presents >= test_keys[next_key].first) {
            handle_native_settings_key(test_keys[next_key].second, false);
            ++next_key;
        }
        // GALAXY_TEST_WINDOW_SIZES=<present>:<width>x<height>,...: the window resized at those presents, whatever is
        // open (a live resize).
        struct TestSize {
            unsigned long at;
            unsigned long width;
            unsigned long height;
        };
        static const std::vector<TestSize> test_sizes = [] {
            std::vector<TestSize> sizes;
            char value[512]{};
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_WINDOW_SIZES") != 0 || length <= 1u) return sizes;
            for (char* item = value; item != nullptr && *item != '\0';) {
                char* end = nullptr;
                TestSize size{std::strtoul(item, &end, 10), 0u, 0u};
                if (end != nullptr && *end == ':') size.width = std::strtoul(end + 1, &end, 10);
                if (end != nullptr && *end == 'x') size.height = std::strtoul(end + 1, &end, 10);
                if (size.width != 0u && size.height != 0u) sizes.push_back(size);
                item = end != nullptr ? std::strchr(end, ',') : nullptr;
                if (item != nullptr) ++item;
            }
            return sizes;
        }();
        static std::size_t next_size = 0u;
        while (next_size < test_sizes.size() && key_presents >= test_sizes[next_size].at) {
            const HWND window = reinterpret_cast<HWND>(host_window::pointer_hwnd.load(std::memory_order_acquire));
            if (window != nullptr) {
                (void)PostMessageW(window, kTestResizeMessage, test_sizes[next_size].width, test_sizes[next_size].height);
            }
            ++next_size;
        }
    }
    // The native game draws the menu itself (native_settings_view).
    if (!s_native_settings_visible.load(std::memory_order_relaxed) ||
        s_settings_drawn_by_game.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const NativeSettingsLayout layout =
        compute_native_settings_layout(host_window::backbuffer_width, host_window::backbuffer_height, tab);
    if (!layout.supported) {
        return;
    }
    const int scale = layout.text_scale;
    const int panel_w = layout.panel_width;
    const int panel_h = layout.panel_height;
    const int panel_x = layout.panel_x;
    const int panel_y = layout.panel_y;
    const std::array<float, 4> panel{0.02f, 0.025f, 0.035f, 1.0f};
    const std::array<float, 4> rail{0.0f, 0.42f, 0.62f, 1.0f};
    const std::array<float, 4> text{0.88f, 0.94f, 0.96f, 1.0f};
    const std::array<float, 4> muted{0.42f, 0.48f, 0.50f, 1.0f};
    const std::array<float, 4> accent{0.15f, 0.72f, 0.85f, 1.0f};
    const std::array<float, 4> error{0.95f, 0.28f, 0.24f, 1.0f};
    const std::array<float, 4> selected{0.10f, 0.18f, 0.22f, 1.0f};

    ui_rect(renderer, panel_x, panel_y, panel_w, panel_h, panel);
    ui_rect(renderer, panel_x, panel_y, 6, panel_h, rail);

    // Corners from the game's own file-select window frame (game_asset_extract.h): one rounded top-left corner,
    // mirrored through the UVs for the other three. Nothing is drawn when the texture could not be loaded.
    renderer.ensure_native_settings_corner_texture_loaded();
    {
        constexpr int kCornerPx = 20;
        const std::array<float, 4> corner_tint{1.0f, 1.0f, 1.0f, 0.9f};
        // top-left
        renderer.draw_ui_textured_quad(
            panel_x, panel_y, kCornerPx, kCornerPx,
            0.0f, 0.0f, 1.0f, 1.0f, corner_tint);
        // top-right (mirror U)
        renderer.draw_ui_textured_quad(
            panel_x + panel_w - kCornerPx, panel_y, kCornerPx, kCornerPx,
            1.0f, 0.0f, 0.0f, 1.0f, corner_tint);
        // bottom-left (mirror V)
        renderer.draw_ui_textured_quad(
            panel_x, panel_y + panel_h - kCornerPx, kCornerPx, kCornerPx,
            0.0f, 1.0f, 1.0f, 0.0f, corner_tint);
        // bottom-right (mirror both)
        renderer.draw_ui_textured_quad(
            panel_x + panel_w - kCornerPx, panel_y + panel_h - kCornerPx,
            kCornerPx, kCornerPx,
            1.0f, 1.0f, 0.0f, 0.0f, corner_tint);
    }

    ui_text(
        renderer,
        panel_x + 28,
        panel_y + (scale == 1 ? 10 : 24),
        scale,
        "SETTINGS",
        text);

    const char* const* tabs = kNativeSettingsTabNames;
    static_assert(std::size(kNativeSettingsTabNames) == kNativeSettingsTabCount);
    int tab_x = panel_x + 28;
    // The labels share the panel width; the longest is eight 6-unit glyphs plus a gap, so the label scale shrinks on
    // narrow panels.
    const int tab_step = (panel_w - 56) / static_cast<int>(kNativeSettingsTabCount);
    const int tab_scale = std::clamp(tab_step / 54, 1, scale);
    for (unsigned i = 0; i < kNativeSettingsTabCount; ++i) {
        ui_text(
            renderer,
            tab_x,
            panel_y + (scale == 1 ? 30 : 70),
            tab_scale,
            tabs[i],
            i == tab ? accent : muted);
        tab_x += tab_step;
    }

    const std::lock_guard<std::recursive_mutex> settings_lock(s_native_settings_mutex);
    const NativeSettingsView view = build_native_settings_view();
    const unsigned row_count = static_cast<unsigned>(view.rows.size());
    const unsigned selected_row = view.selected;
    // Pages longer than the panel scroll to keep the selection visible.
    const unsigned visible_rows = std::min(row_count, kNativeSettingsVisibleRows);
    s_settings_first_row = native_settings_first_row(s_settings_first_row, selected_row, row_count);
    if (row_count > visible_rows) {
        const int track_x = panel_x + panel_w - 18;
        const int track_y = layout.rows_y - 6;
        const int track_h = static_cast<int>(visible_rows - 1u) * layout.row_step + scale * 7 + 12;
        ui_rect(renderer, track_x, track_y, 4, track_h, selected);
        const int thumb_h = std::max(8, track_h * static_cast<int>(visible_rows) / static_cast<int>(row_count));
        const int thumb_y = track_y + (track_h - thumb_h) * static_cast<int>(s_settings_first_row) /
                                          static_cast<int>(row_count - visible_rows);
        ui_rect(renderer, track_x, thumb_y, 4, thumb_h, accent);
    }
    // Rows: the label on the left, the value on the right between arrows where it can change;
    // section headings in the accent colour. A value takes up to three fifths of the row, the label the rest.
    const int row_left = panel_x + 70;
    const int row_right = panel_x + panel_w - 48;
    const int row_gap = 12 * scale;
    for (unsigned row = s_settings_first_row; row < s_settings_first_row + visible_rows; ++row) {
        const int y =
            layout.rows_y + static_cast<int>(row - s_settings_first_row) * layout.row_step;
        std::string label = view.rows[row].label;
        const std::string& value = view.rows[row].value;
        const bool heading = view.rows[row].heading;
        const bool changes = view.rows[row].changes;
        if (heading) {
            label = ui_fit_text(std::move(label), scale, row_right - (panel_x + 36));
            const int rule_x = panel_x + 44 + ui_text_width(label, scale);
            ui_text(renderer, panel_x + 36, y, scale, label, accent);
            if (row_right - rule_x > 0) ui_rect(renderer, rule_x, y + scale * 3, row_right - rule_x, std::max(1, scale / 2), muted);
            continue;
        }
        if (row == selected_row) {
            const int selection_height = std::max(14, scale * 7 + 4);
            ui_rect(
                renderer,
                panel_x + 24,
                y - 6,
                panel_w - 48,
                selection_height,
                selected);
            ui_text(renderer, panel_x + 36, y, scale, ">", accent);
        }
        int value_x = row_right;
        if (!value.empty()) {
            const bool arrows = changes && row == selected_row;
            const int arrow_width = arrows ? ui_text_width("<  >", scale) : 0;
            const std::string fitted = ui_fit_text(value, scale, (row_right - row_left) * 3 / 5 - arrow_width);
            const std::string shown = arrows ? "< " + fitted + " >" : fitted;
            value_x = row_right - ui_text_width(shown, scale);
            ui_text(renderer, value_x, y, scale, shown, row == selected_row ? accent : (changes ? text : muted));
            value_x -= row_gap;
        }
        ui_text(renderer, row_left, y, scale, ui_fit_text(std::move(label), scale, value_x - row_left), changes ? text : muted);
    }

    const int footer_y = layout.footer_y;
    const int footer_scale = std::min(2, scale);
    const int footer_step = footer_scale == 1 ? 18 : 28;
    std::string status;
    std::array<float, 4> status_color = accent;
    const bool controls_page = tab == kControlsTab;
    char line[96]{};
    switch (view.status) {
    case NativeSettingsStatus::CaptureKey:
        status = "PRESS A KEY OR MOUSE BUTTON  ESC CANCEL";
        break;
    case NativeSettingsStatus::CapturePad:
        status = "PRESS A BUTTON, STICK OR TRIGGER  ESC CANCEL";
        break;
    case NativeSettingsStatus::Message:
        status = view.status_text;
        status_color = error;
        break;
    case NativeSettingsStatus::DlssAsk:
    case NativeSettingsStatus::DlssInstalling:
    case NativeSettingsStatus::DlssRestart:
    case NativeSettingsStatus::DlssFailed: {
        bool dlss_error = false;
        status = dlss_footer(GetTickCount64(), dlss_error);
        status_color = dlss_error ? error : accent;
        break;
    }
    case NativeSettingsStatus::KeepDisplay:
        std::snprintf(line, sizeof(line), "KEEP DISPLAY? ENTER/Y KEEP  ESC/B UNDO  %d", view.status_number);
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::SaveFailed:
        status = "SAVE FAILED - RETRY ON NEXT CLOSE";
        status_color = error;
        break;
    case NativeSettingsStatus::Pending:
        std::snprintf(line, sizeof(line),
                      controls_page ? "%d CHANGE%s  SPACE/Y APPLY  ESC/B DISCARD"
                                    : "%d CHANGE%s  ENTER/Y APPLY  BACKSPACE/X UNDO",
                      view.status_number, view.status_number == 1 ? "" : "S");
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::Leave:
        std::snprintf(line, sizeof(line), "%d CHANGE%s NOT APPLIED  ENTER/Y APPLY  BACKSPACE/X DISCARD  ESC/B STAY",
                      view.status_number, view.status_number == 1 ? "" : "S");
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::Applied:
        status = "APPLIED";
        break;
    case NativeSettingsStatus::LanguageRestart:
        status = "THE NEW LANGUAGE APPLIES WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::ModsRestart:
        status = "MOD CHANGES APPLY WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::GpuRestart:
        status = "THE NEW GRAPHICS CARD APPLIES WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::Help:
        status = view.help;
        status_color = muted;
        break;
    case NativeSettingsStatus::JoyCon:
        std::snprintf(line, sizeof(line), "%s  ESC/B BACK",
                      (view.status_number & kJoyConPairedBit) != 0 ? "JOY-CON PAIRED"
                      : (view.status_number & (kJoyConLeftBit | kJoyConRightBit)) == (kJoyConLeftBit | kJoyConRightBit)
                          ? "HOLD L ON THE LEFT JOY-CON AND R ON THE RIGHT ONE"
                          : "CONNECT A LEFT AND A RIGHT JOY-CON");
        status = pad_hints(line);
        break;
    default:
        break;
    }
    if (!status.empty()) {
        ui_text(renderer, panel_x + 28, footer_y, footer_scale, ui_fit_text(std::move(status), footer_scale, panel_w - 56),
                status_color);
    }
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step,
        footer_scale,
        pad_hints("F1, VIEW+MENU OR MINUS+PLUS TOGGLE  TAB/LB/RB PAGE"),
        muted);
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step * 2,
        footer_scale,
        pad_hints(controls_page ? "ENTER/A ADD INPUT  BACKSPACE/X CLEAR  ESC/B CLOSE"
                                : "ARROWS/D-PAD MOVE+CHANGE  ESC/B CLOSE"),
        muted);
}


// The HOME Menu panel, centred over the frame, until the game draws the menu itself. The mouse picks an answer by
// hovering and chooses with a click.
void draw_home_menu(UiCanvas& renderer) {
    static bool s_left_was_down = true;
    if (!s_home_menu_visible.load(std::memory_order_acquire) ||
        s_home_menu_drawn_by_game.load(std::memory_order_relaxed)) {
        s_left_was_down = true;
        return;
    }
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    const unsigned count = asking != 0 ? kHomeMenuAnswers : kHomeMenuRows;
    const char* const* rows = asking != 0 ? kHomeMenuAnswerText : kHomeMenuRowText;
    const int bb_w = static_cast<int>(host_window::backbuffer_width);
    const int bb_h = static_cast<int>(host_window::backbuffer_height);
    const int scale = bb_h >= 1440 ? 4 : (bb_h >= 720 ? 3 : 2);
    const int row_h = 15 * scale;
    const int w = std::min(bb_w - 8 * scale, 140 * scale);
    const int h = 26 * scale + row_h * static_cast<int>(count) + 14 * scale;
    const int x = (bb_w - w) / 2;
    const int y = (bb_h - h) / 2;
    const int rows_y = y + 24 * scale;
    const std::array<float, 4> panel{0.02f, 0.025f, 0.035f, 1.0f};
    const std::array<float, 4> rail{0.0f, 0.42f, 0.62f, 1.0f};
    const std::array<float, 4> text{0.88f, 0.94f, 0.96f, 1.0f};
    const std::array<float, 4> muted{0.42f, 0.48f, 0.50f, 1.0f};
    const std::array<float, 4> highlight{0.10f, 0.18f, 0.22f, 1.0f};

    const HostPointerState pointer = poll_host_pointer_state();
    if (pointer.absolute_valid && pointer.inside_client && pointer.client_width > 0 && pointer.client_height > 0) {
        const int px = pointer.client_x * bb_w / pointer.client_width;
        const int py = pointer.client_y * bb_h / pointer.client_height;
        if (px >= x && px < x + w && py >= rows_y && py < rows_y + row_h * static_cast<int>(count)) {
            const unsigned hovered = static_cast<unsigned>((py - rows_y) / row_h);
            if (pointer.left_button && !s_left_was_down) {
                home_menu_choose(hovered);
            } else {
                home_menu_point(hovered);
            }
        }
    }
    s_left_was_down = pointer.left_button;

    ui_rect(renderer, x, y, w, h, panel);
    ui_rect(renderer, x, y, 6, h, rail);
    ui_text(renderer, x + 14 * scale, y + 8 * scale, scale,
            asking == 0 ? "HOME MENU" : (asking == 1 ? "QUIT TO DESKTOP?" : "RESET?"), text);
    const unsigned selected = s_home_menu_row.load(std::memory_order_relaxed) % count;
    for (unsigned row = 0; row < count; ++row) {
        const int ry = rows_y + static_cast<int>(row) * row_h;
        if (row == selected) {
            ui_rect(renderer, x + 6, ry, w - 6, row_h, highlight);
        }
        ui_text(renderer, x + 14 * scale, ry + 4 * scale, scale, rows[row], row == selected ? text : muted);
    }
}

// For the first seconds of a session, until the overlay is first opened, a one-line hint names the keys that open it.
void draw_native_settings_hint(UiCanvas& renderer) {
    static std::uint64_t s_hint_until_ms = 0u;
    static bool s_hint_done = false;
    if (s_hint_done) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (s_hint_until_ms == 0u) {
        s_hint_until_ms = now + 8000u;
    }
    if (now >= s_hint_until_ms ||
        s_native_settings_visible.load(std::memory_order_relaxed)) {
        s_hint_done = true;
        return;
    }
    if (host_window::backbuffer_width < 427u || host_window::backbuffer_height < 240u) {
        return;
    }
    static constexpr const char* kHint = "F1, VIEW+MENU OR MINUS+PLUS: SETTINGS";
    const int scale = host_window::backbuffer_height >= 1440u ? 3 : (host_window::backbuffer_height >= 720u ? 2 : 1);
    int text_w = -scale;
    for (const char* ch = kHint; *ch != '\0'; ++ch) {
        text_w += (*ch == ' ' ? 4 : 6) * scale;
    }
    const int pad = 4 * scale;
    const int x = 8 * scale;
    const int y = static_cast<int>(host_window::backbuffer_height) - 8 * scale - 7 * scale - pad * 2;
    ui_rect(renderer, x, y, text_w + pad * 2, 7 * scale + pad * 2,
            {0.02f, 0.025f, 0.035f, 1.0f});
    ui_text(renderer, x + pad, y + pad, scale, kHint,
            {0.88f, 0.94f, 0.96f, 1.0f});
}

}  // namespace host_window

void set_native_settings_content_root(const std::string& utf8_path) {
    host_window::native_settings_content_root = utf8_path;
}

HostClientExtent current_host_client_extent() noexcept {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_acquire));
    RECT client{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &client)) {
        return {};
    }
    return {client.right - client.left, client.bottom - client.top};
}

HostClientExtent aspect_latch_extent() noexcept {
    const unsigned mode = std::min(get_render_config().aspect_mode, kAspectModeCount - 1u);
    if (mode != 0u) {
        // Any extent with the chosen ratio; only the ratio is latched.
        return {static_cast<int>(kAspectRatios[mode][0] * 120u),
                static_cast<int>(kAspectRatios[mode][1] * 120u)};
    }
    const HostClientExtent client = current_host_client_extent();
    if (client.width <= 1 || client.height <= 1) {
        return client;
    }
    const double aspect = static_cast<double>(client.width) / client.height;
    // Every shape from 9:21 (narrower than 4:3: Vert+, make_experimental_ultrawide_aspect) to 32:9.
    if (aspect < 9.0 / 21.0) {
        return {900, 2100};
    }
    if (aspect > 32.0 / 9.0) {
        return {3840, 1080};
    }
    return client;
}

HostPointerState poll_host_pointer_state() noexcept {
    constexpr std::uint32_t kPointerDebugCachedEvent = 1u << 8u;
    constexpr std::uint32_t kPointerDebugCursorPollAttempted = 1u << 9u;
    constexpr std::uint32_t kPointerDebugCursorPollInside = 1u << 10u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPoll = 1u << 11u;
    constexpr std::uint32_t kPointerDebugRejectedStaleCenter = 1u << 12u;
    constexpr std::uint32_t kPointerDebugRecentClientMove = 1u << 13u;
    constexpr std::uint32_t kPointerDebugFallbackCursorPoll = 1u << 14u;
    constexpr std::uint32_t kPointerDebugLocalPollAuthority = 1u << 16u;
    constexpr std::uint32_t kPointerDebugRemoteSession = 1u << 17u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPollOutside = 1u << 18u;
    const bool message_left_button =
        s_pointer_left_button.load(std::memory_order_relaxed);
    const bool message_right_button =
        s_pointer_right_button.load(std::memory_order_relaxed);
    const HWND hwnd =
        reinterpret_cast<HWND>(host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const galaxy::host::HostPointerSnapshot cached_snapshot =
        s_pointer_snapshot.snapshot();
    HostPointerState state{};
    state.debug_window = reinterpret_cast<std::uintptr_t>(hwnd);
    state.left_button = message_left_button;
    state.right_button = message_right_button;
    state.absolute_acquired_ms = cached_snapshot.absolute_acquired_ms;
    if (s_system_menu_active.load(std::memory_order_acquire) ||
        host_window::move_size_active.load(std::memory_order_acquire)) {
        // A system menu or move/size loop owns the mouse: the game sees no buttons, and the cursor poll does not take
        // the pointer back meanwhile.
        state.left_button = false;
        state.right_button = false;
        return state;
    }

    // Client-area mouse messages win over the per-frame cursor poll: on Remote Desktop and with a hidden cursor,
    // GetCursorPos can report a stale centered position for long stretches while WM_MOUSEMOVE carries the live
    // coordinates. The seed does not advance the event sequence, so startup centering stays inactive until a real
    // cursor sample arrives.
    const std::uint64_t event_seq = cached_snapshot.event_sequence;
    const std::uint64_t leave_seq = cached_snapshot.leave_sequence;
    const bool cached_inside = cached_snapshot.coordinates.inside_client;
    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerClientMessageTiming client_message_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            pointer_now_ms, cached_snapshot.last_client_message_ms,
            kPointerMessageRecentMs, kStaleCenterRejectMs);
    const bool recent_client_move = client_message_timing.recent;
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const galaxy::host::HostPointerCursorPollAuthorityDecision
        cursor_poll_authority =
            galaxy::host::select_host_pointer_cursor_poll_authority(
                kCursorPollMode, remote_session,
                recent_client_move);
    if (remote_session) {
        state.debug_flags |= kPointerDebugRemoteSession;
    }
    if (cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative && !remote_session) {
        state.debug_flags |= kPointerDebugLocalPollAuthority;
    }

    state.window_focused = s_pointer_focused.load(std::memory_order_relaxed) ||
                           (hwnd != nullptr && GetForegroundWindow() == hwnd);

    if (event_seq != 0) {
        const int width = cached_snapshot.coordinates.client_width;
        const int height = cached_snapshot.coordinates.client_height;
        if (width > 1 && height > 1) {
            state.debug_flags |= kPointerDebugCachedEvent;
            if (recent_client_move) {
                state.debug_flags |= kPointerDebugRecentClientMove;
            }
            const int cached_x =
                std::clamp(cached_snapshot.coordinates.client_x, 0, width - 1);
            const int cached_y =
                std::clamp(cached_snapshot.coordinates.client_y, 0, height - 1);
            POINT cursor{};
            if (cursor_poll_authority.poll_enabled && hwnd != nullptr &&
                GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
                state.debug_flags |= kPointerDebugCursorPollAttempted;
                const bool cursor_inside = cursor.x >= 0 && cursor.y >= 0 &&
                                           cursor.x < width &&
                                           cursor.y < height;
                if (cursor_inside) {
                    state.debug_flags |= kPointerDebugCursorPollInside;
                    const int cursor_x =
                        std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                    const int cursor_y =
                        std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                    const int previous_cursor_x =
                        s_pointer_last_cursor_poll_x.load(
                            std::memory_order_relaxed);
                    const int previous_cursor_y =
                        s_pointer_last_cursor_poll_y.load(
                            std::memory_order_relaxed);
                    const int center_x = (width - 1) / 2;
                    const int center_y = (height - 1) / 2;
                    const bool cursor_near_center =
                        std::abs(cursor_x - center_x) <= 2 &&
                        std::abs(cursor_y - center_y) <= 2;
                    const bool cached_far_from_center =
                        std::abs(cached_x - center_x) > width / 8 ||
                        std::abs(cached_y - center_y) > height / 8;
                    const bool client_move_recent_for_stale_center =
                        cursor_poll_authority.stale_center_guard_allowed &&
                        client_message_timing.stale_center_guard_active;
                    const bool likely_stale_center_poll =
                        client_move_recent_for_stale_center && cached_inside &&
                        cursor_near_center && cached_far_from_center;
                    if (likely_stale_center_poll) {
                        state.debug_flags |= kPointerDebugRejectedStaleCenter;
                    }
                    // Under Remote Desktop GetCursorPos trails WM_MOUSEMOVE on some hosts while on others the
                    // messages stall: the poll takes over once the last client message is no longer fresh, still
                    // rejecting the stale centered sample.
                    const bool cursor_poll_recovers_visibility =
                        (!cached_inside && !recent_client_move) ||
                        event_seq <= leave_seq;
                    // Local polling is authoritative at the input cadence; adaptive remote polling only once the
                    // client-message coordinate has aged out.
                    const galaxy::host::HostPointerCursorPollSelection
                        cursor_poll_selection =
                            galaxy::host::select_host_pointer_cursor_poll({
                                previous_cursor_x !=
                                        std::numeric_limits<int>::min() &&
                                    previous_cursor_y !=
                                        std::numeric_limits<int>::min(),
                                previous_cursor_x,
                                previous_cursor_y,
                                cursor_x,
                                cursor_y,
                                likely_stale_center_poll,
                                cursor_poll_recovers_visibility,
                                cursor_poll_authority.authoritative,
                                cached_snapshot.coordinates.absolute_valid,
                                cached_snapshot.coordinates.client_x,
                                cached_snapshot.coordinates.client_y,
                            });
                    if (cursor_poll_selection.should_publish) {
                        bool publication_accepted = false;
                        (void)s_pointer_snapshot.update(
                                [&](galaxy::host::HostPointerSnapshot&
                                        current) noexcept {
                                    const auto result = galaxy::host::
                                        apply_host_pointer_cursor_inside_publication(
                                            current,
                                            {
                                                true,
                                                cursor_x,
                                                cursor_y,
                                                pointer_now_ms,
                                                event_seq,
                                                width,
                                                height,
                                                cached_snapshot.absolute_sequence,
                                            });
                                    publication_accepted =
                                        result == galaxy::host::
                                                      HostPointerCursorInsidePublicationResult::
                                                          PublishedInside;
                                });
                        if (publication_accepted) {
                            // Only a poll that won the publication becomes the baseline; rejected or
                            // superseded samples stay eligible once the authority/stale-center timeout expires.
                            s_pointer_last_cursor_poll_x.store(
                                cursor_x, std::memory_order_relaxed);
                            s_pointer_last_cursor_poll_y.store(
                                cursor_y, std::memory_order_relaxed);
                            state.debug_flags |=
                                kPointerDebugSelectedCursorPoll;
                        }
                    }
                } else {
                    bool outside_publication_selected = false;
                    (void)s_pointer_snapshot.update(
                            [&](galaxy::host::HostPointerSnapshot& current)
                                noexcept {
                                const auto result = galaxy::host::
                                    apply_host_pointer_cursor_outside_publication(
                                        current,
                                        {
                                            true,
                                            false,
                                            cursor_poll_authority.authoritative,
                                            event_seq,
                                            width,
                                            height,
                                            cached_snapshot.absolute_sequence,
                                        });
                                outside_publication_selected =
                                    result != galaxy::host::
                                                  HostPointerCursorOutsidePublicationResult::
                                                      Rejected;
                            });
                    if (outside_publication_selected) {
                        state.debug_flags |=
                            kPointerDebugSelectedCursorPollOutside;
                    }
                }
            }
            // The window procedure may have published a newer move, leave or resize during
            // GetCursorPos/ScreenToClient: report the latest snapshot, not the older cached one.
            const galaxy::host::HostPointerSnapshot final_snapshot =
                s_pointer_snapshot.snapshot();
            state.client_width = final_snapshot.coordinates.client_width;
            state.client_height = final_snapshot.coordinates.client_height;
            state.client_x = final_snapshot.coordinates.client_x;
            state.client_y = final_snapshot.coordinates.client_y;
            state.inside_client =
                galaxy::host::select_host_pointer_ir_availability(
                    state.window_focused,
                    final_snapshot.coordinates.absolute_valid,
                    final_snapshot.coordinates.inside_client)
                    .acquired_inside_available;
            if (state.inside_client) {
                maybe_hide_os_cursor();
            }
            state.absolute_valid = final_snapshot.coordinates.absolute_valid;
            state.absolute_sequence = final_snapshot.absolute_sequence;
            state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
            return state;
        }
    }

    if (hwnd != nullptr && cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative) {
        RECT client{};
        POINT cursor{};
        if (GetClientRect(hwnd, &client) && GetCursorPos(&cursor) &&
            ScreenToClient(hwnd, &cursor)) {
            state.debug_flags |= kPointerDebugFallbackCursorPoll;
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            const bool inside = cursor.x >= 0 && cursor.y >= 0 &&
                                cursor.x < width && cursor.y < height;
            if (width > 1 && height > 1 && inside) {
                const int client_x =
                    std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                const int client_y =
                    std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                const std::uint64_t fallback_now_ms =
                    static_cast<std::uint64_t>(GetTickCount64());
                bool publication_accepted = false;
                const galaxy::host::HostPointerSnapshot publication =
                    s_pointer_snapshot.update(
                        [&](galaxy::host::HostPointerSnapshot&
                                current) noexcept {
                            const auto result = galaxy::host::
                                apply_host_pointer_fallback_inside_publication(
                                    current,
                                    {
                                        client_x,
                                        client_y,
                                        width,
                                        height,
                                        fallback_now_ms,
                                        event_seq,
                                        cached_snapshot.absolute_sequence,
                                    });
                            publication_accepted =
                                result == galaxy::host::
                                              HostPointerFallbackInsidePublicationResult::
                                                  PublishedInside;
                        });
                if (publication_accepted) {
                    state.client_width = publication.coordinates.client_width;
                    state.client_height = publication.coordinates.client_height;
                    state.client_x = publication.coordinates.client_x;
                    state.client_y = publication.coordinates.client_y;
                    state.inside_client = true;
                    state.absolute_valid = true;
                    state.absolute_sequence = publication.absolute_sequence;
                    state.absolute_acquired_ms =
                        publication.absolute_acquired_ms;
                    maybe_hide_os_cursor();
                    return state;
                }
            }
        }
    }

    if (hwnd != nullptr) {
        seed_pointer_from_client(hwnd, false);
    }
    const galaxy::host::HostPointerSnapshot final_snapshot =
        s_pointer_snapshot.snapshot();
    state.inside_client =
        galaxy::host::select_host_pointer_ir_availability(
            state.window_focused, final_snapshot.coordinates.absolute_valid,
            final_snapshot.coordinates.inside_client)
            .acquired_inside_available;
    state.client_x = final_snapshot.coordinates.client_x;
    state.client_y = final_snapshot.coordinates.client_y;
    state.client_width = final_snapshot.coordinates.client_width;
    state.client_height = final_snapshot.coordinates.client_height;
    state.absolute_valid = final_snapshot.coordinates.absolute_valid;
    state.absolute_sequence = final_snapshot.absolute_sequence;
    state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
    return state;
}

host::HostPointerTransitionPoll
poll_host_pointer_transition(std::uint64_t after_sequence) noexcept {
    return s_pointer_transitions.read_after(after_sequence);
}

bool native_settings_overlay_visible() noexcept {
    return s_native_settings_visible.load(std::memory_order_relaxed);
}

namespace {
std::atomic<bool> s_sharp_2d_requested{false};
}  // namespace

void request_sharp_2d(bool on) noexcept {
    s_sharp_2d_requested.store(on, std::memory_order_relaxed);
}

bool sharp_2d_requested() noexcept {
    return s_sharp_2d_requested.load(std::memory_order_relaxed);
}

bool native_window_input_captured() noexcept {
    return s_native_settings_visible.load(std::memory_order_relaxed) || coop_text_entry() ||
        s_home_menu_visible.load(std::memory_order_relaxed) ||
        s_system_menu_active.load(std::memory_order_acquire) ||
        host_window::move_size_active.load(std::memory_order_acquire);
}

#include "native_settings_api.inl"


void home_menu_open() noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    s_home_menu_row.store(0u, std::memory_order_relaxed);
    s_home_menu_asking.store(0, std::memory_order_relaxed);
    s_home_menu_choice.store(-1, std::memory_order_relaxed);
    s_home_menu_visible.store(true, std::memory_order_release);
}

int home_menu_poll() noexcept {
    return s_home_menu_choice.load(std::memory_order_acquire);
}

void home_menu_close() noexcept {
    s_home_menu_visible.store(false, std::memory_order_release);
}

bool home_menu_visible() noexcept {
    return s_home_menu_visible.load(std::memory_order_acquire);
}

int home_menu_view(bool drawing) noexcept {
    if (!s_home_menu_visible.load(std::memory_order_acquire)) {
        return -1;
    }
    if (drawing) {
        s_home_menu_drawn_by_game.store(true, std::memory_order_relaxed);
    }
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    const unsigned row = s_home_menu_row.load(std::memory_order_relaxed) % home_menu_answer_count();
    return static_cast<int>(row | static_cast<unsigned>(s_home_menu_asking.load(std::memory_order_relaxed)) << 8u);
}

void home_menu_point(unsigned answer) noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    if (s_home_menu_visible.load(std::memory_order_relaxed) && answer < home_menu_answer_count()) {
        s_home_menu_row.store(answer, std::memory_order_relaxed);
    }
}

void home_menu_choose(unsigned answer) noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    if (s_home_menu_visible.load(std::memory_order_relaxed) && answer < home_menu_answer_count()) {
        s_home_menu_row.store(answer, std::memory_order_relaxed);
        choose_home_menu_answer(answer);
    }
}

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const HWND window = reinterpret_cast<HWND>(
        host_window::settings_window.load(std::memory_order_acquire));
    if (window != nullptr) {
        (void)PostMessageW(window, kSettingsKeyMessage,
                           static_cast<WPARAM>(virtual_key), 0);
    }
}

void native_settings_reset_overlay_state_for_tests() noexcept {
    s_display_confirm_deadline_ms = 0u;
    s_settings_leave_asking = false;
    s_settings_drawn_by_game.store(false, std::memory_order_relaxed);
    s_capture_target = galaxy::input::kWiiTargetCount;
    s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
    s_controls_device = {};
    s_controls_devices = {};
    s_test_controllers.reset();
    s_test_active_device.reset();
    s_settings_first_row = 0u;
    s_controls_message_until_ms = 0u;
    s_native_settings_visible.store(false, std::memory_order_relaxed);
    s_native_settings_dirty.store(false, std::memory_order_relaxed);
    s_native_settings_save_error.store(false, std::memory_order_relaxed);
    s_native_settings_tab.store(0u, std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
    settle_native_settings_row();
}
std::string native_settings_row_text_for_tests(unsigned row) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    return native_settings_row_text(s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount,
                                   row, s_settings_draft, s_runtime_draft, get_render_feature_caps());
}
void native_settings_set_controllers_for_tests(std::span<const galaxy::input::ControllerInfo> controllers,
                                                std::optional<galaxy::input::DeviceKey> active) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_test_controllers = std::vector<galaxy::input::ControllerInfo>(controllers.begin(), controllers.end());
    s_test_active_device = active;
    refresh_controls_devices(true);
}
RenderConfig native_settings_draft_for_tests() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    return s_settings_draft;
}
void native_settings_wait_saved_for_tests() {
    std::unique_lock<std::mutex> save_lock(s_settings_save_mutex);
    s_settings_save_changed.wait(save_lock, [] { return !s_settings_save_requested && !s_settings_save_running; });
}
void host_window::native_settings_set_refresh_override_for_tests(unsigned hz) noexcept {
    s_refresh_override_hz.store(hz, std::memory_order_relaxed);
}

std::int32_t take_pointer_wheel() noexcept {
    return host_window::pointer_wheel.exchange(0, std::memory_order_relaxed);
}

bool request_restart() noexcept {
    return restart_through_launcher();
}

unsigned coop_keys_held() noexcept {
    return s_native_settings_visible.load(std::memory_order_relaxed) ? 0u
                                                                      : s_coop_keys_held.load(std::memory_order_relaxed);
}
}  // namespace galaxy::gx
