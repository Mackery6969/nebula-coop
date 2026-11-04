// The native game's host (docs/NATIVE_PORT.md, "Module layout"): loads the game module (smg_game.dll), gives it
// Nebula's services (nebula_game_api.h) and runs it. Built three ways:
//   - nebula_native_host (native/CMakeLists.txt): headless. Records are dropped as they arrive; audio is paced but not
//     played; no remotes connect.
//   - NebulaNative (the top-level build, NEBULA_HOST_RENDERER): the records go to Nebula's D3D12 renderer.
//   - On SDL platforms (platform/sdl, NEBULA_HOST_SDL): the same with the Vulkan renderer, SDL's window, sound and
//     input; on Android SDL's activity calls SDL_main.
//
//   <host> --pak <game.pak> --saves <dir> [--module <smg_game.dll>] [--seconds N]
//   Windowed hosts also take --host <port> or --join <address[:port]> (online co-op).
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
#include <dwmapi.h>
#endif
#include "galaxy/frame_phase.h"
#include "galaxy/render/present_clock.h"
#include <nebula_cooked_texture.h>
#include <nebula_fingerprint.h>
#include <nebula_game_api.h>

#include "galaxy/atomic_file.h"
#include "galaxy/coop_chat.h"
#include "galaxy/coop_menu.h"
#include "galaxy/coop_mods.h"
#include "galaxy/coop_popups.h"
#include "galaxy/coop_saves.h"
#include "galaxy/coop_session.h"
#include "galaxy/coop_snapshot.h"
#include "galaxy/coop_world.h"
#include "galaxy/run_options.h"
#include "../../tools/test_driver/switches.h"
#include "galaxy/disc_files.h"
#include "galaxy/save_files.h"
#include "galaxy/crash_report.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/host_env.h"
#endif
#include "galaxy/discord_presence.h"
#include "galaxy/mii_formats.h"
#include "galaxy/mii_import.h"
#include "galaxy/mods.h"
#include "galaxy/game_instance.h"
#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/render_api.h"
#include "galaxy/native_audio.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_input.h"
#include "galaxy/loss_report.h"
#include "galaxy/movie_video.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/render/host_window_win32.h"
#endif
#include "galaxy/perf_hint.h"
#include "galaxy/platform_time.h"
#include "galaxy/pointer_control.h"
#include "galaxy/achievements.h"
#include "galaxy/stats.h"
#if defined(_WIN32) && !defined(NEBULA_HOST_SDL)
// A real Wii Remote over Windows HID (wiimote_hid.h).
#define NEBULA_REAL_WIIMOTE 1
#include "galaxy/input/wiimote_input.h"
#include "galaxy/wiimote_hid.h"
#endif
#include "galaxy/runtime_settings.h"
#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/native_shapes.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/texture_pack.h"
#if defined(NEBULA_HOST_D3D12)
#endif

#if defined(_WIN32)
#include <crtdbg.h>
#else
#include "galaxy/virtual_keys.h"
#endif
#endif
#if !defined(NEBULA_HOST_RENDERER)
namespace galaxy::host {
// The headless host gives the platform no performance hints.
inline void perf_hint_add_current_thread() noexcept {}
inline int64_t perf_hint_retrace() noexcept { return 0; }
}  // namespace galaxy::host
#endif
#if defined(NEBULA_HOST_SDL)
#include "sdl_host.h"

#include <SDL3/SDL_main.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <ctime>
#include <span>
#if defined(_WIN32)
#include <io.h>
#endif
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <intrin.h>
#include <dbghelp.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "galaxy/platform_paths.h"

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// Sleeps until `when` on a high-resolution waitable timer (Windows 10 1803+; the scheduler tick otherwise), so the
// frame, the audio interface and the remotes keep their periods to well under a millisecond.
void sleep_until_precise(Clock::time_point when) {
#if defined(_WIN32)
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                       TIMER_ALL_ACCESS);
    const auto now = Clock::now();
    if (when <= now) {
        return;
    }
    if (timer == nullptr) {
        std::this_thread::sleep_until(when);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>((std::chrono::duration_cast<std::chrono::nanoseconds>(when - now).count() + 99) / 100);
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
#else
    std::this_thread::sleep_until(when);
#endif
}
const Clock::time_point kStart = Clock::now();

std::string utf8_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void log_line(const char* tag, const char* text) {
    const double t = std::chrono::duration<double>(Clock::now() - kStart).count();
    std::fprintf(stdout, "[%9.3f] %s %s\n", t, tag, text);
    std::fflush(stdout);
}

// The start, step by step ("boot:" lines, milliseconds from the process's creation): the black screen before the
// title is their sum.
void boot_mark(const char* step) {
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - kStart).count();
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&now);
        const auto ticks = [](const FILETIME& f) {
            return static_cast<double>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime);
        };
        ms = (ticks(now) - ticks(created)) / 1.0e4;
    }
#endif
    char text[128];
    std::snprintf(text, sizeof(text), "%s at %.0f ms", step, ms);
    log_line("boot:", text);
}

// Crash reports (galaxy/crash_report.h): why Nebula stopped, as a code with what to do. What the failing thread knew,
// kept for the report (gathered on that thread: its activity is its own).
galaxy::crash::Facts gCrashFacts;
char gCrashText[1024];
char gCrashActivityFile[512];
char gCrashModRead[640];

void gather_crash_facts(galaxy::crash::Facts::Kind kind, const char* text, bool in_game = false,
                        bool stack_overflow = false) noexcept {
    std::snprintf(gCrashText, sizeof(gCrashText), "%s", text != nullptr ? text : "");
    gCrashFacts.kind = kind;
    gCrashFacts.in_game = in_game;
    gCrashFacts.stack_overflow = stack_overflow;
    gCrashFacts.text = gCrashText;
    gCrashFacts.activity = galaxy::crash::current_activity(gCrashActivityFile, sizeof(gCrashActivityFile));
    gCrashFacts.activity_file = gCrashActivityFile;
    double seconds = -1.0;
    if (galaxy::crash::last_mod_read(gCrashModRead, sizeof(gCrashModRead), &seconds)) {
        gCrashFacts.last_mod_read = gCrashModRead;
        gCrashFacts.last_mod_seconds = seconds;
    }
    gCrashFacts.mods_change_game = galaxy::crash::session_mods_change_game();
}

// Crash dumps and reports: GALAXY_CRASH_DUMP_DIR, else %LOCALAPPDATA%\Nebula\crashes for the player's executable and
// crashes\ in the working directory for the headless test host.
fs::path crash_folder() {
#if defined(_WIN32)
    if (const wchar_t* dir = ::galaxy::run_options().crash_dump_dir_w; dir != nullptr && dir[0] != 0) return fs::path(dir);
#if defined(NEBULA_HOST_RENDERER)
    wchar_t local[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    return fs::path(local) / "Nebula" / "crashes";
#endif
#else
    if (const char* dir = ::galaxy::run_options().crash_dump_dir; dir != nullptr && dir[0] != 0) return fs::path(dir);
#endif
    return fs::path("crashes");
}

// The player sees the report; hidden test runs (GALAXY_HIDE_WINDOW) and the headless host keep it in its file.
bool crash_dialog_wanted() {
#if defined(NEBULA_HOST_RENDERER) || defined(NEBULA_HOST_SDL)
    const char* hidden = ::galaxy::run_options().hide_window;
    return !(hidden != nullptr && hidden[0] != 0 && hidden[0] != '0');
#else
    return false;
#endif
}

FILE* open_for_writing(const fs::path& file) {
#if defined(_WIN32)
    return _wfopen(file.c_str(), L"wb");
#else
    return std::fopen(file.c_str(), "wb");
#endif
}

// gCrashFacts as a report: its code logged, its text written beside `dump` (or as nebula-<time>.txt in the crash
// folder) and, for the player, shown by a new instance of this executable (this process may be what failed; it ends
// right after). The SDL hosts show it in a message box here.
void report_crash_facts(const fs::path& dump) {
    const galaxy::crash::Report report = galaxy::crash::classify(gCrashFacts);
    char line[160];
    std::snprintf(line, sizeof(line), "%s %s", report.code, report.title);
    log_line("crash:", line);
    char technical[1600];
    std::snprintf(technical, sizeof(technical), "%s%s%s%s%s", gCrashText,
                  gCrashFacts.last_mod_read.empty() ? "" : "\nLast file read from a mod: ", gCrashModRead,
                  dump.empty() ? "" : "\nDump: ", dump.empty() ? "" : utf8_of(dump.filename()).c_str());
    static char text[8192];
    const std::size_t length = galaxy::crash::format_report(report, technical, text, sizeof(text));
    std::error_code error;
    fs::path file = dump;
    if (file.empty()) {
        const fs::path folder = crash_folder();
        fs::create_directories(folder, error);
        const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char name[64];
        std::strftime(name, sizeof(name), "nebula-%Y%m%d-%H%M%S.txt", std::localtime(&stamp));
        file = folder / name;
    } else {
        file.replace_extension(".txt");
    }
    if (FILE* out = open_for_writing(file)) {
        std::fwrite(text, 1, length, out);
        std::fclose(out);
        std::snprintf(line, sizeof(line), "report %s", utf8_of(file.filename()).c_str());
        log_line("crash:", line);
    }
    if (!crash_dialog_wanted()) return;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --crash-report \"" + file.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#elif defined(NEBULA_HOST_SDL)
    const std::string_view shown(text, length);
    const std::size_t technical_at = shown.find("\nTechnical details:");
    const std::string message(shown.substr(0, technical_at));
    galaxy::sdl::show_fatal(message.c_str());
#endif
}

[[noreturn]] void die(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_line("fatal:", text);
    std::fflush(stdout);
    gather_crash_facts(galaxy::crash::Facts::Kind::Startup, text);
    report_crash_facts({});
    std::fflush(stdout);
    std::_Exit(3);
}

// The module's entry points.
struct Module {
    decltype(&nebula_game_api_fingerprint) api_fingerprint;  // null in a module from before fingerprints
    decltype(&nebula_game_attach) attach;
    decltype(&nebula_game_run) run;
    decltype(&nebula_game_sound_frame) audio_tick;
    decltype(&nebula_game_mii_valid) mii_valid;
    decltype(&nebula_game_mii_changed) mii_changed;
    void (*debug_dump)();  // optional: the module's thread report
};
Module gModule;

// Progress counters for the host's log.
std::atomic<uint32_t> gFileReads{0};
std::atomic<uint32_t> gSaveOps{0};
std::atomic<uint32_t> gSoundBlocks{0};
std::atomic<uint32_t> gSoundSubframes{0};
std::atomic<uint32_t> gMovieFrames{0};
std::atomic<uint64_t> gMovieDecodeNs{0};   // the movie decoder's time (svc_movie_video_decode)
std::atomic<uint32_t> gMovieSkipped{0};    // movie frames not decoded: a replacement video showed instead
std::atomic<uint32_t> gSceneBegins{0};

void* find_symbol(void* module, const char* name) {
#if defined(_WIN32)
    void* p = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    void* p = dlsym(module, name);
#endif
    if (p == nullptr) {
        die("the game module has no %s", name);
    }
    return p;
}

#if defined(_WIN32)
// GALAXY_PROFILE_GAME=<seconds>: the game thread (every game thread runs on it) sampled each millisecond from its
// start for that long, with its callers, into game-profile.txt beside the crash dumps: the modules loaded
// ("module <base> <size> <name>"), then a line a sample (return addresses, the innermost first).
// tools/perf/game_profile.py names them from the game module's link map.
namespace game_profile {

constexpr int kDepth = 24;
using Frames = std::array<DWORD64, kDepth>;

// The suspended thread's stack from `context`, without allocating (the thread may hold the heap's lock).
int walk(CONTEXT context, Frames& frames) noexcept {
    int depth = 0;
    __try {
        while (depth < kDepth && context.Rip != 0) {
            frames[depth++] = context.Rip;
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (function == nullptr) {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                void* handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &establisher,
                                 nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return depth;
}

void run(HANDLE thread, double seconds) {
    std::vector<std::pair<int, Frames>> samples;
    samples.reserve(static_cast<std::size_t>(seconds * 1200.0) + 16u);
    const auto until = Clock::now() + std::chrono::duration<double>(seconds);
    auto next = Clock::now();
    while (Clock::now() < until && samples.size() < samples.capacity()) {
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            Frames frames{};
            const int depth = GetThreadContext(thread, &context) ? walk(context, frames) : 0;
            ResumeThread(thread);
            if (depth > 0) samples.emplace_back(depth, frames);
        }
        next += std::chrono::milliseconds(1);
        sleep_until_precise(next);
    }
    std::FILE* file = _wfopen((crash_folder() / L"game-profile.txt").c_str(), L"wb");
    if (file == nullptr) return;
    std::array<HMODULE, 512> modules{};
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < modules.size(); ++i) {
            MODULEINFO info{};
            char name[MAX_PATH] = {};
            if (K32GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)) &&
                K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH) != 0) {
                std::fprintf(file, "module %llx %lx %s\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)),
                             static_cast<unsigned long>(info.SizeOfImage), name);
            }
        }
    }
    for (const auto& [depth, frames] : samples) {
        std::fputs("sample", file);
        for (int i = 0; i < depth; ++i) std::fprintf(file, " %llx", static_cast<unsigned long long>(frames[i]));
        std::fputc('\n', file);
    }
    std::fclose(file);
    char text[96];
    std::snprintf(text, sizeof(text), "%zu samples in game-profile.txt", samples.size());
    log_line("profile:", text);
}

}  // namespace game_profile
#endif

void load_module(const fs::path& path) {
#if defined(_WIN32)
    void* module = LoadLibraryW(path.wstring().c_str());
#else
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (module == nullptr) {
        die("cannot load the game module %s", utf8_of(path.filename()).c_str());
    }
#if defined(_WIN32)
    gModule.api_fingerprint = reinterpret_cast<decltype(gModule.api_fingerprint)>(
        GetProcAddress(static_cast<HMODULE>(module), "nebula_game_api_fingerprint"));
#else
    gModule.api_fingerprint =
        reinterpret_cast<decltype(gModule.api_fingerprint)>(dlsym(module, "nebula_game_api_fingerprint"));
#endif
    gModule.attach = reinterpret_cast<decltype(gModule.attach)>(find_symbol(module, "nebula_game_attach"));
    gModule.run = reinterpret_cast<decltype(gModule.run)>(find_symbol(module, "nebula_game_run"));
    gModule.audio_tick = reinterpret_cast<decltype(gModule.audio_tick)>(find_symbol(module, "nebula_game_sound_frame"));
    gModule.mii_valid = reinterpret_cast<decltype(gModule.mii_valid)>(find_symbol(module, "nebula_game_mii_valid"));
    gModule.mii_changed =
        reinterpret_cast<decltype(gModule.mii_changed)>(find_symbol(module, "nebula_game_mii_changed"));
#if defined(_WIN32)
    gModule.debug_dump = reinterpret_cast<void (*)()>(GetProcAddress(static_cast<HMODULE>(module), "nebula_game_debug_dump"));
#else
    gModule.debug_dump = reinterpret_cast<void (*)()>(dlsym(module, "nebula_game_debug_dump"));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void svc_memory_rewritten(void*, const void* address, uint32_t size);

#if defined(NEBULA_HOST_RENDERER)
// The images handed to the renderer (texture_image), by address: one handed over again unchanged needs no
// new record. Any write over one forgets it, as the renderer drops its copy then: the game handing the same bytes over
// after reloading a file must reach the renderer again.
std::mutex gHandedLock;
std::map<uint64_t, std::shared_ptr<const std::vector<std::byte>>> gHanded;

void forget_handed(uint64_t physical, uint32_t size) {
    const std::lock_guard guard(gHandedLock);
    auto it = gHanded.lower_bound(physical + size);
    while (it != gHanded.begin()) {
        --it;
        if (it->first + it->second->size() <= physical) {
            break;
        }
        it = gHanded.erase(it);
    }
}
#endif

// Host writes into game memory (disc DMA): images handed over there must be handed over again.
void mark_written(const void* dst, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    forget_handed(reinterpret_cast<uint64_t>(dst), size);
#else
    (void)dst, (void)size;
#endif
}
#endif

// The game: Setup's game.pak, the asset store (galaxy/disc_files.h).
struct Pak {
    char id[7] = {};  // the game id
    galaxy::disc::AssetStore store;
    fs::path store_file;
};
Pak gPak;

void open_pak(const fs::path& path) {
    gPak.store_file = path;
    if (!gPak.store.open(path)) {
        die("%s is not a game store: import the game again", utf8_of(path.filename()).c_str());
    }
    // The releases the module plays (NebulaRegion.hpp): North America, Europe and Japan.
    std::snprintf(gPak.id, sizeof(gPak.id), "%.6s", std::string(gPak.store.game_id()).c_str());
    if (std::strcmp(gPak.id, "RMGE01") != 0 && std::strcmp(gPak.id, "RMGP01") != 0 &&
        std::strcmp(gPak.id, "RMGJ01") != 0) {
        die("%s holds %.6s, not Super Mario Galaxy for North America, Europe or Japan", utf8_of(path.filename()).c_str(), gPak.id);
    }
}

// The calling thread's name, as profilers, debuggers and the CPU report show it.
void name_thread(const wchar_t* name) {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), name);
#else
    char narrow[16];
    std::snprintf(narrow, sizeof(narrow), "%ls", name);
#if defined(__APPLE__)
    pthread_setname_np(narrow);
#else
    pthread_setname_np(pthread_self(), narrow);
#endif
#endif
}

// A worker thread taking jobs in order.
class Worker {
public:
    explicit Worker(const wchar_t* name) : name_(name) {}

    template <typename F>
    void post(F&& job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.emplace_back(std::forward<F>(job));
        }
        ready_.notify_one();
    }

    void start() {
        thread_ = std::thread([this] {
            name_thread(name_);
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [this] { return !jobs_.empty(); });
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }
                job();
            }
        });
        thread_.detach();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
    const wchar_t* name_;
};

Worker gFs(L"Nebula Host Saves");

// The save folder's root: the game's paths under it, each checked.
fs::path gSaveRoot;

// The game's save folder under it (title/00010000/<the game's four-character code in hex>/data, as an install has it).
std::string gSaveDataPath;
// The co-op save being played (galaxy::coop::SaveStore): the game's save folder while set. The fs thread reads it.
std::mutex gCoopSaveMutex;
fs::path gCoopSaveFolder;

// Where the game's save folder is instead of the save root's: the co-op save's.
fs::path save_folder_override() {
    const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
    return gCoopSaveFolder;
}

// The native switches the enabled mods' code patches set (NEBULA_PATCH_*), for the session.
std::map<std::uint32_t, std::uint32_t> gModPatches;

int svc_code_patch(void*, uint32_t id, uint32_t* value) {
    const auto it = gModPatches.find(id);
    if (it == gModPatches.end()) {
        return 0;
    }
    if (value != nullptr) {
        *value = it->second;
    }
    return 1;
}

// A mod's own save folder (Riivolution's savegame): the game's save files go there instead.
fs::path gModSaveRoot;

bool nand_path(const char* path, fs::path* out) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    fs::path result = !gModSaveRoot.empty() && !gSaveDataPath.empty() && std::string_view(path).starts_with(gSaveDataPath)
                          ? gModSaveRoot
                          : gSaveRoot;
    // The co-op save's folder over a mod's (save_folder_override).
    fs::path saves;
    if (!gSaveDataPath.empty() && std::strncmp(path, gSaveDataPath.c_str(), gSaveDataPath.size()) == 0 &&
        (path[gSaveDataPath.size()] == '/' || path[gSaveDataPath.size()] == '\0') &&
        !(saves = save_folder_override()).empty()) {
        result = saves;
        path += gSaveDataPath.size();
        if (*path == '\0') {
            *out = result;
            return true;
        }
    }
    for (const fs::path& part : fs::path(path + 1)) {
        if (part == ".." || part == ".") {
            return false;
        }
        result /= part;
    }
    *out = result;
    return true;
}

// The audio clock: started by the game with its frame's length (in 32 kHz samples).
std::mutex gAiLock;
std::condition_variable gAiWake;
uint32_t gAudioFrameSamples = 0;
bool gAiRunning = false;

#if defined(NEBULA_HOST_RENDERER)
extern galaxy::host::NativeGameAudio* gAudio;
#endif

// The app out of sight (SDL hosts: Android's background): the display and audio clocks stop, so the game waits at
// its next frame; they restart from its return, with nothing to catch up.
std::mutex gSuspendLock;
std::condition_variable gSuspendWake;
bool gSuspended = false;  // guarded by gSuspendLock

// The start runs ahead: the game shows nothing until its first 3D scene (the title's, or a test's galaxy), and its
// loads advance a step each frame while it takes well under a millisecond a frame, so until then frames come every
// kBootPeriod and one frame a display period is presented. The first 3D scene, or kBootFrames frames, end it.
// GALAXY_BOOT_FAST=0 keeps 60 frames a second from the start.
constexpr auto kBootPeriod = std::chrono::microseconds(2000);
constexpr uint32_t kBootFrames = 600;
std::atomic<bool> gBootFast{[] {
    const char* value = ::galaxy::run_options().boot_fast;
    return value == nullptr || value[0] != '0';
}()};

void end_boot_fast(const char* why) {
    if (gBootFast.exchange(false, std::memory_order_relaxed)) {
        boot_mark(why);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_frame_end_presents(true);
#endif
    }
}

// GALAXY_TEST_DETERMINISTIC=1: deterministic test mode. The game's time is its frame count: the host's frame and
// audio clocks do not run; the game takes each frame when it has nothing to run (test_frame_step), disc reads
// complete inside their call, and the steady clock counts frames. Two runs of one build then match frame for frame.
bool deterministic_mode() {
    static const bool deterministic = [] {
        const char* value = ::galaxy::run_options().test_deterministic;
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return deterministic;
}
std::atomic<uint64_t> gDeterministicFrames{0};

// True when it waited.
bool wait_while_suspended() {
    std::unique_lock<std::mutex> lock(gSuspendLock);
    if (!gSuspended) {
        return false;
    }
    gSuspendWake.wait(lock, [] { return !gSuspended; });
    return true;
}

#if !defined(NEBULA_HOST_RENDERER)
// Headless, nothing is mixed: the game's sound logic runs on the clock.
void ai_thread() {
    if (deterministic_mode()) {
        return;  // the audio ticks come with each frame (svc_test_frame_step)
    }
    name_thread(L"Nebula Host Audio");
    for (;;) {
        uint32_t frame_samples = 0;
        {
            std::unique_lock<std::mutex> lock(gAiLock);
            gAiWake.wait(lock, [] { return gAiRunning && gAudioFrameSamples != 0; });
            frame_samples = gAudioFrameSamples;
        }
        gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
        gModule.audio_tick();
        std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(frame_samples * 1e9 / 32000.0)));
    }
}
#endif

// The device asks for the game's next frame of sound, on its thread.
void audio_frame_tick() {
    gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
    gModule.audio_tick();
}

#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
// The display's retraces by the compositor's clock, looked at twice a second.
std::optional<galaxy::host::Retraces> display_retraces() {
    static std::optional<galaxy::host::Retraces> known;
    static int64_t earlier_vblank_ns = 0;
    static uint32_t frames = 0;
    if (frames++ % 30u != 0u) return known;
    known.reset();
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.qpcRefreshPeriod != 0u) {
        const auto to_ns = [&](uint64_t qpc) {
            const uint64_t whole = qpc / static_cast<uint64_t>(frequency.QuadPart);
            const uint64_t part = qpc % static_cast<uint64_t>(frequency.QuadPart);
            return static_cast<int64_t>(whole * 1000000000ull + part * 1000000000ull / static_cast<uint64_t>(frequency.QuadPart));
        };
        const galaxy::host::Retraces now{to_ns(timing.qpcVBlank), to_ns(timing.qpcRefreshPeriod)};
        if (earlier_vblank_ns != 0 && galaxy::host::retraces_in_step(earlier_vblank_ns, now)) known = now;
        earlier_vblank_ns = now.vblank_ns;
    }
    return known;
}

// The next frame's start after the one due at `start` (frame_phase.h).
Clock::time_point next_frame_start(Clock::time_point start, std::chrono::nanoseconds period) {
    static galaxy::host::FramePhase phase;
    static int64_t previous_ns = 0;
    const int64_t start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count();
    const int64_t present_ns = galaxy::render::g_last_present_ns.load(std::memory_order_relaxed);
    const int64_t work_ns = previous_ns != 0 && present_ns > previous_ns ? present_ns - previous_ns : 0;
    previous_ns = start_ns;
    const int64_t next_ns = phase.next_start(start_ns, work_ns, period.count(), display_retraces());
    return start + std::chrono::nanoseconds(next_ns - start_ns);
}
#endif

// The display's frame at 60 Hz: 59.94 would repeat a frame every 17 s on a 60 Hz display. A late
// wake-up skips the fields it missed, as a display does, rather than delivering them back to back. On Android the
// frame follows the display's own vsync (every second one at 120 Hz), so frames never drift against it and each
// stays on screen for the same time; the host's clock takes over where the display gives none.
void svc_frame_wait(void*) {
    const auto display_period = std::chrono::nanoseconds(1000000000ull / 60);
    static auto period = gBootFast.load(std::memory_order_relaxed) ? std::chrono::nanoseconds(kBootPeriod) : display_period;
    static auto next = Clock::now() + period;
#if defined(NEBULA_HOST_SDL)
    static int64_t vsync_ns = 0;
    static uint32_t on_vsync = 0;
    static uint32_t frames = 0;
    static int64_t work_sum = 0;
    static int64_t work_max = 0;
#endif
    for (;;) {
#if defined(NEBULA_HOST_SDL)
        const bool vsync = galaxy::sdl::wait_display_retrace(period.count(), vsync_ns);
        if (!vsync) {
            sleep_until_precise(next);
        }
#else
        sleep_until_precise(next);
#endif
        if (wait_while_suspended()) {
            next = Clock::now() + period;
            continue;
        }
        // The last frame's work (the game and render threads' CPU time), to the performance hints (Android).
        [[maybe_unused]] const int64_t work = galaxy::host::perf_hint_retrace();
        if (period != display_period && !gBootFast.load(std::memory_order_relaxed)) {
            period = display_period;
        }
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
        next = next_frame_start(next, period);
#else
        next += period;
#endif
        const auto now = Clock::now();
        if (now >= next) {
            next = now + period;
        }
#if defined(NEBULA_HOST_SDL)
        if (vsync) {
            next = now + period;  // the host's clock continues from the display's
            ++on_vsync;
        }
        // Every 10 s: how the frames were paced, and the frame work (the busier of the game and render threads).
        work_sum += work;
        work_max = std::max(work_max, work);
        if (++frames == 600u) {
            std::fprintf(stderr, "[pacing] %u frames, %u on the display's vsync (%.1f Hz); frame work %.2f ms average, "
                         "%.2f ms longest\n", frames, on_vsync, galaxy::sdl::display_refresh_hz(),
                         static_cast<double>(work_sum) / frames / 1e6, static_cast<double>(work_max) / 1e6);
            frames = on_vsync = 0u;
            work_sum = work_max = 0;
        }
#endif
        return;
    }
}

// Deterministic mode's frame: the game has nothing to run. The audio ticks of one frame's length (the audio clock's
// period, without the device's correction) and the clock.
void svc_test_frame_step(void*) {
    static double due_ns = 0.0;
    uint32_t frame_samples = 0;
    {
        std::lock_guard<std::mutex> lock(gAiLock);
        frame_samples = gAiRunning ? gAudioFrameSamples : 0u;
    }
    if (frame_samples != 0u) {
        due_ns += 1e9 / 60.0;
        const double period_ns = static_cast<double>(frame_samples) * 1e9 / 32000.0;
        while (due_ns >= period_ns) {
            due_ns -= period_ns;
            gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
            gModule.audio_tick();
        }
    } else {
        due_ns = 0.0;
    }
    gDeterministicFrames.fetch_add(1, std::memory_order_relaxed);
}

void svc_log(void*, const char* text) {
    log_line("game:", text);
}

#if defined(_WIN32)
// GALAXY_TRACE_THREAD_CPU=1: each thread's CPU time and context switches over the last second, by name (or,
// unnamed, the module and offset it started at), busiest first; threads under 0.5 ms/s are counted in the total only.
// The report's own cost (a snapshot of every thread in the system) is left out of the reporting thread's figure.
void report_thread_cpu(double tsc_hz) {
    static std::unordered_map<DWORD, uint64_t> last;
    static std::unordered_map<DWORD, uint32_t> last_switches;
    static uint64_t report_cycles = 0;
    ULONG64 report_start = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_start);
    // The process's threads and their context switches: NtQuerySystemInformation(SystemProcessInformation). On x64
    // each process record is 0x100 bytes followed by 0x50-byte thread records (thread id at +0x30, switches at +0x40).
    using SystemQueryFn = LONG(WINAPI*)(int, void*, ULONG, ULONG*);
    static const auto system_query = reinterpret_cast<SystemQueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    static std::vector<uint8_t> buffer(4u << 20);
    struct ThreadSample {
        DWORD id;
        uint32_t switches;
    };
    std::vector<ThreadSample> threads;
    ULONG needed = 0;
    LONG status = system_query != nullptr ? system_query(5, buffer.data(), (ULONG)buffer.size(), &needed) : -1;
    if (status == static_cast<LONG>((3u << 30) | 4u) /* STATUS_INFO_LENGTH_MISMATCH */ && needed != 0) {
        buffer.resize(needed + (1u << 20));
        status = system_query(5, buffer.data(), (ULONG)buffer.size(), &needed);
    }
    if (status != 0) {
        return;
    }
    const DWORD pid = GetCurrentProcessId();
    for (size_t offset = 0;;) {
        const uint8_t* process = buffer.data() + offset;
        uint32_t next = 0, count = 0;
        uintptr_t process_id = 0;
        std::memcpy(&next, process, 4);
        std::memcpy(&count, process + 4, 4);
        std::memcpy(&process_id, process + 0x50, sizeof(process_id));
        if (process_id == pid) {
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* thread = process + 0x100 + i * 0x50;
                uintptr_t thread_id = 0;
                uint32_t switches = 0;
                std::memcpy(&thread_id, thread + 0x30, sizeof(thread_id));
                std::memcpy(&switches, thread + 0x40, 4);
                threads.push_back({(DWORD)thread_id, switches});
            }
            break;
        }
        if (next == 0) {
            break;
        }
        offset += next;
    }
    using QueryFn = LONG(WINAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    struct Row {
        double ms;
        std::string name;
        uint32_t switches;
    };
    std::vector<Row> rows;
    double total = 0.0;
    const DWORD self = GetCurrentThreadId();
    for (const ThreadSample& sample : threads) {
        const DWORD thread_id = sample.id;
        const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (thread == nullptr) {
            continue;
        }
        ULONG64 cycles = 0;
        QueryThreadCycleTime(thread, &cycles);
        if (thread_id == self) {
            cycles = report_start - report_cycles;
        }
        std::string name;
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr && description[0] != 0) {
            char narrow[64];
            WideCharToMultiByte(CP_UTF8, 0, description, -1, narrow, sizeof(narrow), nullptr, nullptr);
            name = narrow;
        }
        LocalFree(description);
        void* start = nullptr;
        HMODULE owner = nullptr;
        if (name.empty()) {
            // ThreadQuerySetWin32StartAddress (9).
            char where[160] = "?";
            if (query != nullptr && query(thread, 9, &start, sizeof(start), nullptr) == 0 &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCWSTR>(start), &owner)) {
                char path[MAX_PATH];
                const DWORD length = GetModuleFileNameA(owner, path, sizeof(path));
                const char* file = length != 0 ? std::strrchr(path, '\\') : nullptr;
                std::snprintf(where, sizeof(where), "%s+0x%llx", file != nullptr ? file + 1 : "?",
                              (unsigned long long)(static_cast<char*>(start) - reinterpret_cast<char*>(owner)));
            }
            name = "tid " + std::to_string(thread_id) + " " + where;
        }
        CloseHandle(thread);
        uint64_t& before = last[thread_id];
        const double ms = (double)(cycles - before) * 1000.0 / tsc_hz;
        before = cycles;
        total += ms;
        uint32_t& switches_before = last_switches[thread_id];
        const uint32_t switches = sample.switches - switches_before;
        switches_before = sample.switches;
        if (ms >= 0.5) {
            rows.push_back({ms, std::move(name), switches});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    std::string line = "total " + std::to_string((int)(total + 0.5)) + " ms/s";
    for (const Row& row : rows) {
        char item[160];
        std::snprintf(item, sizeof(item), "; %s %.1f (%u sw)", row.name.c_str(), row.ms, row.switches);
        line += item;
    }
    log_line("cpu:", line.c_str());
    ULONG64 report_end = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_end);
    report_cycles += report_end - report_start;
}
#endif

std::string gSceneStage;

#if defined(_WIN32)
// Crash reports. A fatal exception in the game module or the host, or the game's own fatal call, writes a minidump and
// the report (report_crash_facts) to the crash folder from a thread kept for it (the failing thread's stack may be what
// failed), logs where it happened and the game's threads, and ends the process.
using MiniDumpWriteDumpFn = decltype(&MiniDumpWriteDump);  // loaded at the crash: dbghelp.dll is not linked

HANDLE gCrashRequest;
HANDLE gCrashDone;
EXCEPTION_POINTERS* gCrashInfo;
DWORD gCrashThread;
std::atomic<int> gCrashing{0};

// Defined with the co-op session, below.
void leave_coop_room_at_crash();

void crash_writer() {
    for (;;) {
        WaitForSingleObject(gCrashRequest, INFINITE);
        const fs::path folder = crash_folder();
        std::error_code ignored;
        fs::create_directories(folder, ignored);
        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t path[MAX_PATH];
        std::swprintf(path, MAX_PATH, L"%ls\\nebula-native-%04u%02u%02u-%02u%02u%02u-%lu.dmp", folder.c_str(), now.wYear, now.wMonth,
                      now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        const auto write = dbghelp != nullptr
                               ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                               : nullptr;
        const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write != nullptr && file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{gCrashThread, gCrashInfo, FALSE};
            // Stacks and what they point at (the module's data segments would add its half-gigabyte image window).
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
            const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                  gCrashInfo != nullptr ? &exception : nullptr, nullptr, nullptr);
            char text[MAX_PATH + 32];
            std::snprintf(text, sizeof(text), "%s %ls", ok ? "minidump" : "minidump failed:", fs::path(path).filename().c_str());
            log_line("crash:", text);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        report_crash_facts(fs::path(path));
        // The dump and report first (what the crash left must not cost them): then the room hears this player go,
        // so the others don't wait for it to stop answering.
        leave_coop_room_at_crash();
        SetEvent(gCrashDone);
        // The dump and report are written: whatever the crashed thread still does (the module's thread report, a flush
        // behind a lock another thread held when it stopped) never keeps the process alive.
        Sleep(5000);
        TerminateProcess(GetCurrentProcess(), 4);
    }
}

void report_crash(EXCEPTION_POINTERS* info) {
    if (gCrashRequest == nullptr) {
        return;
    }
    gCrashInfo = info;
    gCrashThread = GetCurrentThreadId();
    SetEvent(gCrashRequest);
    WaitForSingleObject(gCrashDone, 60000);
    if (gModule.debug_dump != nullptr) {
        gModule.debug_dump();
    }
    std::fflush(stdout);
}

// The report, out of line: the filter below runs for every exception in the process and keeps its frame small.
[[noreturn]] __declspec(noinline) void report_fatal_exception(EXCEPTION_POINTERS* info, HMODULE owner) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const void* address = info->ExceptionRecord->ExceptionAddress;
    wchar_t name[MAX_PATH] = {};
    GetModuleFileNameW(owner, name, MAX_PATH);
    char text[MAX_PATH + 160];
    std::snprintf(text, sizeof(text), "exception 0x%08lX at %ls+0x%llx (thread %lu), address 0x%llx", code,
                  fs::path(name).filename().c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(owner)),
                  GetCurrentThreadId(),
                  static_cast<unsigned long long>(info->ExceptionRecord->NumberParameters >= 2
                                                      ? info->ExceptionRecord->ExceptionInformation[1]
                                                      : 0));
    log_line("crash:", text);
    log_line("crash: stage", gSceneStage.c_str());
    gather_crash_facts(galaxy::crash::Facts::Kind::Fault, text, owner == GetModuleHandleW(L"smg_game.dll"),
                       code == EXCEPTION_STACK_OVERFLOW);
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

// Set while describe_cpp_exception reads a record: a fault there belongs to its own __except, which the vectored
// handler would otherwise see first and report as the crash.
thread_local bool tDescribingException = false;

// A C++ exception's record (MSVC's 0xE06D7363: the object, its ThrowInfo, the image base its offsets start from):
// the thrown type and, for a std::exception, what(). A rethrow (`throw;`) has neither object nor ThrowInfo. Read
// under SEH: the record comes from a failing process.
void describe_cpp_exception(const EXCEPTION_RECORD* record, char* out, std::size_t size) {
    std::snprintf(out, size, "uncaught C++ exception");
    if (record->NumberParameters < 4u) {
        return;
    }
    if (record->ExceptionInformation[1] == 0u || record->ExceptionInformation[2] == 0u) {
        std::snprintf(out, size, "uncaught C++ exception (rethrown)");
        return;
    }
    tDescribingException = true;
    __try {
        const auto base = static_cast<uintptr_t>(record->ExceptionInformation[3]);
        const auto* object = reinterpret_cast<const char*>(record->ExceptionInformation[1]);
        // ThrowInfo: attributes, unwind, forward compat, catchable types (32-bit offsets from the image base).
        const auto* throw_info = reinterpret_cast<const int32_t*>(record->ExceptionInformation[2]);
        const auto* types = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(throw_info[3]));
        const char* thrown = nullptr;
        const char* what = nullptr;
        for (int32_t i = 0; i < types[0] && i < 16; ++i) {
            // CatchableType: properties, type descriptor, this displacement (mdisp, pdisp, vdisp), size, copy.
            const auto* type = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(types[1 + i]));
            const char* name = reinterpret_cast<const char*>(base + static_cast<uint32_t>(type[1])) + 16;
            if (thrown == nullptr) {
                thrown = name;
            }
            if (std::strcmp(name, ".?AVexception@std@@") == 0) {
                what = reinterpret_cast<const std::exception*>(object + type[2])->what();
            }
        }
        std::snprintf(out, size, "uncaught C++ exception %s: %s", thrown != nullptr ? thrown : "?",
                      what != nullptr ? what : "(not a std::exception)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    tDescribingException = false;
}

#if defined(__clang__)
__attribute__((no_sanitize("address", "undefined")))
#endif
LONG CALLBACK crash_handler(EXCEPTION_POINTERS* info) {
    if (tDescribingException) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // A C++ exception, logged where it is thrown (the first 32): one thrown on the game's own stacks cannot unwind
    // through them and ends the process before any filter runs. Handled or not, the line says what was thrown.
    if (code == 0xE06D7363u) {
        static std::atomic<unsigned> logged{0u};
        if (logged.fetch_add(1u, std::memory_order_relaxed) < 32u) {
            char text[512];
            describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
            char line[640];
            std::snprintf(line, sizeof(line), "thrown: %s (thread %lu)", text + std::strlen("uncaught "),
                          GetCurrentThreadId());
            log_line("exception:", line);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW &&
        code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Only faults in the game module or this executable: drivers and system libraries handle their own.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner) ||
        (owner != GetModuleHandleW(nullptr) && owner != GetModuleHandleW(L"smg_game.dll"))) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (gCrashing.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    report_fatal_exception(info, owner);
}

// abort(), which std::terminate calls for an uncaught C++ exception: no exception filter sees it. Logs what was
// thrown, when that is why, and writes the dump from the aborting thread's context.
void abort_handler(int) {
    const char* text = "abort";
    std::string what;
    if (const std::exception_ptr active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& error) {
            what = std::string("uncaught exception: ") + error.what();
            text = what.c_str();
        } catch (...) {
            text = "uncaught exception of unknown type";
        }
    }
    log_line("crash:", text);
    if (gCrashing.exchange(1) == 0) {
        gather_crash_facts(galaxy::crash::Facts::Kind::Abort, text);
        CONTEXT context{};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = 0xE0000003u;  // not a system code: marks the dump as an abort
        record.ExceptionAddress = _ReturnAddress();
        EXCEPTION_POINTERS pointers{&record, &context};
        report_crash(&pointers);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    std::_Exit(3);
}

// The last filter: an exception no frame handled, of any kind. A C++ exception that leaves a thread through C or
// system frames reaches no terminate handler, so without this filter the process would end with 0xE06D7363 and no
// line or dump.
LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
    if (gCrashing.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char text[512];
    const bool cpp = info->ExceptionRecord->ExceptionCode == 0xE06D7363u;
    if (cpp) {
        describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
    } else {
        std::snprintf(text, sizeof(text), "unhandled exception 0x%08lX at 0x%p", info->ExceptionRecord->ExceptionCode,
                      info->ExceptionRecord->ExceptionAddress);
    }
    char line[640];
    std::snprintf(line, sizeof(line), "%s (thread %lu)", text, GetCurrentThreadId());
    log_line("crash:", line);
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner);
    gather_crash_facts(cpp ? galaxy::crash::Facts::Kind::Exception : galaxy::crash::Facts::Kind::Fault, line,
                       !cpp && owner != nullptr && owner == GetModuleHandleW(L"smg_game.dll"),
                       info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW);
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

#if defined(NEBULA_HOST_RENDERER)
// NebulaNative --crash-report <file>: the report of an instance that stopped (report_crash_facts), shown by this new
// process. The technical details stay in the file, which the dialog offers to show.
// The language folder whose words the crash dialog uses: the player's setting, for Auto (or settings that do not load,
// the reason for some reports) the system's.
const char* crash_dialog_language() {
    galaxy::GameLanguage language = galaxy::GameLanguage::Auto;
    try {
        galaxy::load_runtime_settings_from_file();
        language = galaxy::get_runtime_settings().language;
    } catch (const std::exception&) {
    }
    const LANGID system = GetUserDefaultUILanguage();
    const auto region = [system](WORD sublanguage) { return SUBLANGID(system) == sublanguage; };
    const bool spain = PRIMARYLANGID(system) == LANG_SPANISH && region(SUBLANG_SPANISH_MODERN);
    const bool canada = PRIMARYLANGID(system) == LANG_FRENCH && region(SUBLANG_FRENCH_CANADIAN);
    switch (language) {
    case galaxy::GameLanguage::Japanese: return "JpJapanese";
    case galaxy::GameLanguage::English: return "UsEnglish";
    case galaxy::GameLanguage::German: return "EuGerman";
    case galaxy::GameLanguage::French: return canada ? "UsFrench" : "EuFrench";
    case galaxy::GameLanguage::Spanish: return spain ? "EuSpanish" : "UsSpanish";
    case galaxy::GameLanguage::Italian: return "EuItalian";
    case galaxy::GameLanguage::Dutch: return "EuDutch";
    case galaxy::GameLanguage::Auto: break;
    }
    switch (PRIMARYLANGID(system)) {
    case LANG_JAPANESE: return "JpJapanese";
    case LANG_GERMAN: return "EuGerman";
    case LANG_FRENCH: return canada ? "UsFrench" : "EuFrench";
    case LANG_SPANISH: return spain ? "EuSpanish" : "UsSpanish";
    case LANG_ITALIAN: return "EuItalian";
    case LANG_DUTCH: return "EuDutch";
    case LANG_PORTUGUESE: return "UsPortuguese";
    case LANG_KOREAN: return "KrKorean";
    case LANG_CHINESE:
        return region(SUBLANG_CHINESE_SIMPLIFIED) || region(SUBLANG_CHINESE_SINGAPORE) ? "CnSimpChinese" : "TwTradChinese";
    default: return "UsEnglish";
    }
}

std::wstring wide_of(std::string_view text) {
    std::wstring wide(static_cast<std::size_t>(
                          MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    return wide;
}

int show_crash_report_dialog() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) return 1;
    const fs::path file = count >= 3 ? fs::path(args[2]) : fs::path();
    LocalFree(args);
    std::string text;
    if (FILE* in = file.empty() ? nullptr : _wfopen(file.c_str(), L"rb")) {
        char buffer[4096];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) text.append(buffer, n);
        std::fclose(in);
    }
    if (text.empty()) return 1;
    if (const std::size_t technical = text.find("\nTechnical details:"); technical != std::string::npos) {
        text.resize(technical);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const char* language = crash_dialog_language();
    text = galaxy::crash::translated_report(text, language);
    text += "\n\n";
    text += galaxy::crash::translated("Open the folder with the report?", language);
    const std::wstring wide = wide_of(text);
    const std::wstring caption = wide_of(galaxy::crash::translated("Nebula stopped", language));
    if (MessageBoxW(nullptr, wide.c_str(), caption.c_str(), MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND | MB_TOPMOST) ==
        IDYES) {
        const std::wstring select = L"/select,\"" + file.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return 0;
}
#endif

void install_crash_reporter() {
    gCrashRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gCrashDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(crash_writer).detach();
    AddVectoredExceptionHandler(0, crash_handler);  // last: the module's own handlers (division, stack guards) first
    SetUnhandledExceptionFilter(unhandled_filter);  // whatever no frame handled, C++ exceptions included
    std::signal(SIGABRT, abort_handler);            // process-wide in the UCRT, unlike set_terminate
}
#endif

#if defined(_WIN32)
// The stack that called a fatal, as module + offset (no folder names).
void log_fatal_stack() {
    void* frames[32];
    const USHORT count = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
    for (USHORT i = 0; i < count; i++) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<const char*>(frames[i]), &module)) {
            K32GetModuleBaseNameA(GetCurrentProcess(), module, name, MAX_PATH);
        }
        char line[MAX_PATH + 32];
        std::snprintf(line, sizeof(line), "%s+0x%llx", name,
                      static_cast<unsigned long long>(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(module)));
        log_line("fatal stack:", line);
    }
}
#endif

void svc_fatal(void*, const char* text) {
    log_line("fatal:", text);
#if defined(_WIN32)
    log_fatal_stack();
#endif
    gather_crash_facts(galaxy::crash::Facts::Kind::GameFatal, text);
#if defined(_WIN32)
    if (gCrashing.exchange(1) == 0) {
        report_crash(nullptr);
    }
#else
    report_crash_facts({});
#endif
    std::fflush(stdout);
    std::_Exit(3);
}

#if defined(NEBULA_HOST_RENDERER)
// The renderer: the game's records reach it in frames (render_frame at the game's frame end and, while the start runs
// ahead, at the frame, which also presents). All of this runs on the simulation thread, where the game makes these
// calls.
using galaxy::RenderLogLevel;
using galaxy::RenderServices;

RenderServices gNative{};
std::atomic<uint64_t> gSubmissions{0};  // render_frame calls with commands: the renderer's frame index

void native_log(void*, RenderLogLevel, const char* text) {
    log_line("renderer:", text);
}

galaxy::host::NativeGameAudio* gAudio = nullptr;

void submit_frame_unchecked(bool present, bool black) {
    galaxy::render::render_frame(&gNative, present, black);
    gSubmissions.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void finish_and_exit(bool restart);

// The graphics driver reset the card (E-18): a new process starts the game again from its save, twice in a row at most;
// the third time is a fault to report.
bool restart_after_gpu_loss(const char* what) {
#if defined(_WIN32)
    if (!galaxy::crash::is_gpu_lost(what)) {
        return false;
    }
    const char* before = std::getenv("NEBULA_GPU_RESTARTS");
    const int count = before != nullptr ? std::atoi(before) : 0;
    if (count >= 2) {
        return false;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%d", count + 1);
    _putenv_s("NEBULA_GPU_RESTARTS", text);
    log_line("host: the graphics card was reset:", what);
    return true;
#else
    (void)what;
    return false;
#endif
}

// The card has no memory free at the start: another program holds it for a moment. A new process tries again, up to
// five times, two seconds apart; after that it is a fault to report.
bool retry_start_for_gpu_memory(const std::string& reason) {
#if defined(_WIN32)
    const char* before = std::getenv("NEBULA_GPU_START_RETRIES");
    const int count = before != nullptr ? std::atoi(before) : 0;
    char out_of_memory[16];
    std::snprintf(out_of_memory, sizeof(out_of_memory), "0x%08lX", static_cast<unsigned long>(E_OUTOFMEMORY));
    const bool wanted = reason.find(out_of_memory) != std::string::npos || reason.find("VK_ERROR_OUT_OF") != std::string::npos ||
                        reason.find("VkResult -1") != std::string::npos || reason.find("VkResult -2") != std::string::npos;
    if (!wanted || count >= 5) {
        return false;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%d", count + 1);
    _putenv_s("NEBULA_GPU_START_RETRIES", text);
    log_line("host: no graphics memory free at the start, trying again:", reason.c_str());
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
#else
    (void)reason;
    return false;
#endif
}

// The renderer's hard failures end the game with their line and a dump. Caught here: thrown on the game's stacks they
// could not unwind through its frames.
void submit_frame(bool present, bool black) {
    try {
        submit_frame_unchecked(present, black);
    } catch (const std::exception& error) {
        if (restart_after_gpu_loss(error.what())) {
            finish_and_exit(true);
        }
        char text[1024];
        std::snprintf(text, sizeof(text), "the renderer stopped: %s", error.what());
        svc_fatal(nullptr, text);
    }
}
#endif

// The stage the game reports before each 3D scene (game_stage); the scene's begin marker carries it.

void svc_game_stage(void*, const char* name) {
    if (name != nullptr && gSceneStage != name) {
        gSceneStage = name;
    }
}

// Online co-op (docs/COOP_PLAN.md): the local player as the game last reported it (coop_player), for the netcode to
// send; a change of instance (stage or scenario) is logged.
std::mutex gCoopLock;
NebulaCoopPlayer gCoopLocal{};
bool gCoopLocalValid = false;

void svc_coop_player(void*, const NebulaCoopPlayer* player) {
    if (player == nullptr) {
        return;
    }
    NebulaCoopPlayer copy = *player;
    copy.stage[sizeof(copy.stage) - 1] = '\0';
    copy.animation[sizeof(copy.animation) - 1] = '\0';
    copy.group[sizeof(copy.group) - 1] = '\0';
    bool moved = false;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        moved = !gCoopLocalValid || std::strcmp(gCoopLocal.stage, copy.stage) != 0 ||
                gCoopLocal.scenario != copy.scenario;
        gCoopLocal = copy;
        gCoopLocalValid = true;
    }
    if (moved) {
        std::fprintf(stderr, "[coop] instance %s, scenario %d\n", copy.stage, static_cast<int>(copy.scenario));
    }
}

// The Discord application Nebula's status shows under ("Super Mario Galaxy"; GALAXY_DISCORD_CLIENT_ID replaces it).
constexpr const char* kDiscordClientId = "1562359210463727617";

// The game's message text (UTF-16, its tags skipped: 0x1A, then the tag's size in bytes in the next unit's high byte)
// as UTF-8.
std::string message_utf8(const uint16_t* text) {
    std::string out;
    if (text == nullptr) return out;
    for (size_t i = 0; i < 256u && text[i] != 0u; ++i) {
        uint32_t c = text[i];
        if (c == 0x1Au) {
            const uint32_t units = (static_cast<uint32_t>(text[i + 1u]) >> 8u) / 2u;
            if (units < 2u) break;
            i += units - 1u;
            continue;
        }
        if (c >= 0xD800u && c <= 0xDBFFu && text[i + 1u] >= 0xDC00u && text[i + 1u] <= 0xDFFFu) {
            c = 0x10000u + ((c - 0xD800u) << 10u) + (text[i + 1u] - 0xDC00u);
            ++i;
        }
        if (c < 0x20u) c = ' ';
        if (c < 0x80u) {
            out += static_cast<char>(c);
        } else if (c < 0x800u) {
            out += static_cast<char>(0xC0u | (c >> 6u));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else if (c < 0x10000u) {
            out += static_cast<char>(0xE0u | (c >> 12u));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (c >> 18u));
            out += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// What the player is doing, for Discord: the galaxy (or the file select) and the star, else the Power Stars.
void svc_presence(void*, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars) {
    galaxy::discord::Activity activity;
    if (power_stars < 0) {
        activity.details = "Choosing a file";
    } else {
        activity.details = message_utf8(galaxy);
        activity.state = message_utf8(star);
        if (activity.details.empty()) {
            activity.details = "At the title screen";  // the title's stage has no galaxy name
            activity.state.clear();
            static bool reached = false;
            if (!reached) {
                reached = true;
                boot_mark("title screen");
            }
        } else if (activity.state.empty()) {
            activity.state = std::to_string(power_stars) + (power_stars == 1 ? " Power Star" : " Power Stars");
        }
    }
    static std::string last;
    const std::string line = activity.details + " | " + activity.state;
    if (line != last) {
        last = line;
        log_line("presence:", line.c_str());
    }
    galaxy::discord::set_activity(activity);
}

void report_coop_achievement(const char* id);

// An achievement the save data meets: unlocked once (achievements.ini), with its English name, and in a room the
// save's for every player in it. The game names it in the played language itself (NebulaText). The headless host
// keeps no achievements.
int svc_achievement(void*, const char* id, uint16_t* name, uint32_t capacity) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id, (void)name, (void)capacity;
    return 0;
#else
    const galaxy::achievements::Info* info = id != nullptr ? galaxy::achievements::find(id) : nullptr;
    if (info == nullptr || !galaxy::achievements::unlock(id)) return 0;
    uint32_t i = 0;
    for (; name != nullptr && i + 1u < capacity && info->name[i] != '\0'; ++i) {
        name[i] = static_cast<unsigned char>(info->name[i]);
    }
    if (name != nullptr && capacity != 0u) name[i] = 0u;
    log_line("achievement:", info->name);
    report_coop_achievement(id);
    return 1;
#endif
}

int32_t svc_achievement_date(void*, const char* id) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id;
    return 0;
#else
    return id != nullptr ? galaxy::achievements::unlocked_date(id) : 0;
#endif
}

// The all-time counters the achievement board shows (stats.ini). The headless host keeps none.
void svc_stat_add(void*, const char* name, uint64_t delta) {
#if defined(NEBULA_HOST_RENDERER)
    if (name != nullptr) galaxy::stats::add(name, delta);
#else
    (void)name, (void)delta;
#endif
}

uint64_t svc_stat_get(void*, const char* name) {
#if defined(NEBULA_HOST_RENDERER)
    return name != nullptr ? galaxy::stats::get(name) : 0u;
#else
    (void)name;
    return 0u;
#endif
}

// UTF-8 text into a fixed field, cut at a whole character, always terminated.
template <std::size_t N>
void copy_settings_text(char (&field)[N], const std::string& text) {
    std::size_t length = std::min(text.size(), N - 1u);
    while (length > 0u && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
        --length;
    }
    std::memcpy(field, text.data(), length);
    field[length] = '\0';
}

#if defined(NEBULA_HOST_RENDERER)
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Leave) == NEBULA_SETTINGS_STATUS_LEAVE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsRestartAsk) == NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::KeepDisplay) == NEBULA_SETTINGS_STATUS_KEEP_DISPLAY &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::JoyCon) == NEBULA_SETTINGS_STATUS_JOYCON &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::RtRestartAsk) ==
                  NEBULA_SETTINGS_STATUS_RT_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::DiagnosticsAsk) ==
                  NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK &&
              galaxy::render::kJoyConLeftBit == NEBULA_JOYCON_LEFT && galaxy::render::kJoyConRightBit == NEBULA_JOYCON_RIGHT &&
              galaxy::render::kJoyConLeftHeldBit == NEBULA_JOYCON_LEFT_HELD &&
              galaxy::render::kJoyConRightHeldBit == NEBULA_JOYCON_RIGHT_HELD &&
              galaxy::render::kJoyConPairedBit == NEBULA_JOYCON_PAIRED &&
              galaxy::render::kJoyConLeftPressesShift == NEBULA_JOYCON_LEFT_PRESSES_SHIFT &&
              galaxy::render::kJoyConRightPressesShift == NEBULA_JOYCON_RIGHT_PRESSES_SHIFT,
              "the settings statuses are numbered alike");
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Open) == NEBULA_SETTINGS_OPEN &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Revert) == NEBULA_SETTINGS_REVERT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::MoveMod) == NEBULA_SETTINGS_MOVE_MOD &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::SetValue) == NEBULA_SETTINGS_SET_VALUE,
              "the settings inputs are numbered alike");
#endif

// The settings menu for the game's own (NebulaSettingsMenu): the overlay's state, which the game draws from then on.
// The headless host has no settings menu: always closed.
// The online co-op main menu (coop_menu), in place of the title and the file select. Its Options opens Nebula's
// settings menu where this host has one (every renderer's: the Windows window's, and SDL's on Linux and Android); the
// main menu waits under it.
#if defined(NEBULA_HOST_RENDERER)
constexpr bool kHostSettingsMenu = true;
#else
constexpr bool kHostSettingsMenu = false;
#endif
galaxy::coop::MainMenu gCoopMenu{kHostSettingsMenu};
std::vector<galaxy::coop::RoomMod> coop_room_mods();
// The co-op saves, under the NAND root (coop_saves/): made once the NAND root is known.
std::optional<galaxy::coop::SaveStore> gCoopSaves;
unsigned gCoopSaveFile = 1u;  // the user file the played save plays

// The character the player wants (NEBULA_COOP_MENU_CHARACTER): Options' PLAYER page's, as Host or Join left the menu,
// then as the page changes in game (renderer builds). The game plays it from its next stage start, and a switch in
// play reloads the player's spot as the other character (NebulaCharacter).
std::atomic<bool> gCoopLuigi{false};

// The room's chat (galaxy/coop_chat.h), over the session below (which goes first, its thread giving the chat lines),
// and its commands (galaxy/coop_commands.h).
galaxy::coop::Chat gCoopChat;
// The pop-ups of who joined, left, went where, died or got a star (galaxy/coop_popups.h), for the game's corner boxes
// (coop_popup_take).
galaxy::coop::Popups gCoopPopups;
// Online play (galaxy/coop_session.h): the room this player hosts or joined. Host opens it as its save starts; Join
// connects from the main menu and plays the host's save once let in; the pause menu's Quit (back at the main menu)
// leaves it.
galaxy::coop::Session gCoopSession;

#if defined(_WIN32)
void leave_coop_room_at_crash() {
    // Never waits: the thread that crashed may be inside the session.
    if (!gCoopSession.leave_if_free()) log_line("crash:", "the co-op room could not be told (the session was busy)");
}
#endif
void keep_coop_settings();
void keep_coop_list(bool ban, const std::string& identity);
// The game as online play compares it: a player with another is refused, as is one with another disc
// (gCoopDisc); one playing other mods gets the room's (galaxy/coop_mods.h).
// The co-op fingerprint (cmake/nebula_fingerprint.cmake) stands for the version: builds whose online play differs in
// any way refuse each other, and no change bumps a number by hand.
const std::string kCoopGameVersion = "nebula-coop-" NEBULA_COOP_FINGERPRINT;
galaxy::coop::DiscInfo gCoopDisc;
// This start played a room's mods for a join (the join Nebula started again for): the room's list.
std::optional<std::vector<galaxy::coop::SharedMod>> gCoopStartedRoomMods;
// Why the room ended, shown on the main menu as it opens again.
std::mutex gCoopNoticeLock;
std::string gCoopNotice;
// The command line's --host <port> or --join <address>: done once, as the main menu first opens.
enum class CoopAutoStart { None, Host, Join };
CoopAutoStart gCoopAutoStart = CoopAutoStart::None;
std::uint16_t gCoopAutoPort = 0u;
std::string gCoopAutoAddress;

// The other players' snapshots (galaxy/coop_snapshot.h), for their copies in this player's instance; and this
// player's, sent 30 times a second.
galaxy::coop::RemotePlayers gCoopRemote;
std::uint32_t gCoopSnapshotSequence = 0u;
std::chrono::steady_clock::time_point gCoopSnapshotSent{};

// The shared world (galaxy/coop_world.h): the host's record of each instance and the session's save, which the game
// reports to and is told from (coop_world_report, coop_world_take).
galaxy::coop::World gCoopWorld;

// Achievements another player unlocked in the room's save, now this player's too, for the game's pop-ups
// (coop_achievement_take), in order.
std::deque<std::string> gCoopAchievementsWaiting;

// This player's game unlocked an achievement: in a room, the save's, so the host keeps it and tells every player.
void report_coop_achievement(const char* id) {
#if defined(NEBULA_HOST_RENDERER)
    const std::int64_t when = galaxy::achievements::unlocked_time(id);
    gCoopWorld.report_save({galaxy::coop::SaveKind::Achievement, id, 0, static_cast<std::uint32_t>(when)});
#else
    (void)id;
#endif
}

// The room's achievements as they come (each scene frame): one this player's save doesn't have yet is unlocked on the
// day it was unlocked in the room, and waits for its pop-up.
void take_coop_achievements() {
    for (const galaxy::coop::SaveChange& change : gCoopWorld.take_achievements()) {
#if defined(NEBULA_HOST_RENDERER)
        const galaxy::achievements::Info* info = galaxy::achievements::find(change.name);
        if (info == nullptr || !galaxy::achievements::unlock_at(change.name, change.value)) continue;
        log_line("achievement (the room's):", info->name);
        gCoopAchievementsWaiting.push_back(change.name);
#else
        (void)change;
#endif
    }
}

// The others' snapshots as they come (on the session's thread), and the world's messages. Set before hosting or
// joining.
void listen_coop_snapshots() {
    galaxy::coop::listen_snapshots(gCoopSession, gCoopRemote);
    gCoopWorld.attach(gCoopSession);
}

// This player's snapshot to the others in its instance, at most kSnapshotInterval apart: the game's last report of it.
void send_coop_snapshot() {
    const auto now = std::chrono::steady_clock::now();
    if (now - gCoopSnapshotSent < galaxy::coop::kSnapshotInterval) return;
    NebulaCoopPlayer local{};
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (!gCoopLocalValid) return;
        local = gCoopLocal;
    }
    gCoopSnapshotSent = now;
    galaxy::coop::PlayerSnapshot snapshot;
    snapshot.sequence = ++gCoopSnapshotSequence;
    snapshot.stage = local.stage;
    snapshot.scenario = local.scenario;
    for (std::size_t i = 0; i < 3u; ++i) {
        snapshot.position[i] = local.position[i];
        snapshot.rotation[i] = local.rotation[i];
        snapshot.front[i] = local.front[i];
        snapshot.up[i] = local.up[i];
        snapshot.gravity[i] = local.gravity[i];
        snapshot.velocity[i] = local.velocity[i];
    }
    snapshot.group = local.group;
    snapshot.animation = local.animation;
    snapshot.frame = local.animation_frame;
    snapshot.rate = local.animation_rate;
    for (std::size_t i = 0; i < snapshot.weights.size(); ++i) snapshot.weights[i] = local.track_weights[i];
    snapshot.health = local.health;
    snapshot.mode = local.mode;
    snapshot.flags = local.flags;
    snapshot.area = local.area;
    (void)galaxy::coop::send_snapshot(gCoopSession, snapshot);
}

void set_coop_notice(std::string text) {
    const std::lock_guard<std::mutex> lock(gCoopNoticeLock);
    gCoopNotice = std::move(text);
}

std::string take_coop_notice() {
    const std::lock_guard<std::mutex> lock(gCoopNoticeLock);
    return std::exchange(gCoopNotice, {});
}

// The host's save as a joiner gets it: the played save folder's files (the game's GameData.bin and the rest, and the
// save's achievements), read as they join, and the user file it plays.
galaxy::coop::RoomSave coop_room_save() {
#if defined(NEBULA_HOST_RENDERER)
    // The achievements unlocked a moment ago, in the file too.
    galaxy::achievements::flush();
#endif
    galaxy::coop::RoomSave save;
    save.file = gCoopSaveFile;
    fs::path folder;
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        folder = gCoopSaveFolder;
    }
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) return save;
    std::size_t total = 0u;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error)) continue;
        const std::string name = entry.path().filename().string();
        // The save's name and when it was played stay the host's; its achievements are every player's.
        if (name == "save.ini") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        total += bytes.size();
        if (total > galaxy::coop::kMaxSaveBytes) break;
        save.files.push_back({name, std::move(bytes)});
    }
    return save;
}

// The host's save into this player's folder for it (coop_joined/ under the NAND root, emptied first), for the game
// to play. False when it can't be written.
bool write_joined_save(const galaxy::coop::RoomSave& save, fs::path& folder) {
    folder = gSaveRoot / "coop_joined";
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    if (error) return false;
    for (const galaxy::coop::SaveFile& file : save.files) {
        // A plain file name: nothing that leaves the folder.
        const fs::path name = fs::path(file.name).filename();
        if (name.empty() || name != fs::path(file.name) || name == "." || name == "..") continue;
        std::ofstream out(folder / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        if (!out) return false;
    }
    return true;
}

// Where the room's mods download (galaxy::coop::ModCache): coop_mods/ beside the mods folder. Empty without one.
fs::path coop_mods_cache() {
    const fs::path mods = galaxy::mods::mods_folder();
    return mods.empty() ? fs::path{} : mods.parent_path() / "coop_mods";
}

// The join that waits for Nebula to start again with a room's mods: in the cache, or a second game's own folder
// (galaxy/game_instance.h), as the games share the cache but each restarts to join on its own.
fs::path coop_next_join_file() {
    const fs::path cache = coop_mods_cache();
    if (cache.empty()) return {};
    const fs::path& instance = galaxy::game_instance_folder();
    return instance.empty() ? cache / ".next_join" : instance / ".next_join";
}

// The mods this game started with that change it (the room's, for a host; what a joiner plays now), with their files'
// hashes: read once, as they stay until Nebula starts again.
const std::vector<galaxy::coop::SharedMod>& coop_session_mods() {
    static const std::vector<galaxy::coop::SharedMod> mods = [] {
        std::vector<galaxy::coop::SharedMod> out;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
            if (!mod.enabled || !galaxy::mods::changes_disc(mod)) continue;
            galaxy::coop::SharedMod shared;
            shared.id = mod.id;
            shared.folder = mod.path;
            shared.own_text = mod.own_text;
            for (const galaxy::mods::ModOption& option : mod.options) {
                shared.options.emplace_back(option.key, option.selected);
            }
            if (!galaxy::coop::describe_mod(shared)) {
                std::fprintf(stderr, "[coop] the mod %s could not be read to share\n", mod.id.c_str());
            }
            out.push_back(std::move(shared));
        }
        return out;
    }();
    return mods;
}

// A joiner's own copy of a room's mod: the mods folder's of its name, when it has the same files.
std::optional<fs::path> find_own_mod(const galaxy::coop::SharedMod& mod) {
    const fs::path folder = galaxy::mods::mods_folder();
    if (folder.empty()) return std::nullopt;
    galaxy::coop::SharedMod own = mod;
    own.folder = folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(mod.id.data()), mod.id.size()));
    std::error_code error;
    if (!fs::is_directory(own.folder, error) || !galaxy::coop::describe_mod(own) || own.hash != mod.hash) {
        return std::nullopt;
    }
    return own.folder;
}

// The room's mods are all here (downloaded or the player's own): Nebula starts again with them and joins again. False
// (and why on the menu) when it doesn't start again now.
bool restart_for_room_mods(const std::vector<galaxy::coop::SharedMod>& mods, const std::string& address) {
    if (gCoopStartedRoomMods && galaxy::coop::same_mods(*gCoopStartedRoomMods, mods)) {
        // Started with these already: they didn't apply as the host plays them.
        std::fprintf(stderr, "[coop] the room's mods (%s) did not apply\n", galaxy::coop::mods_text(mods).c_str());
        gCoopMenu.set_message("COULD NOT JOIN: THE ROOM'S MODS DID NOT APPLY (" + galaxy::coop::mods_text(mods) + ")");
        return false;
    }
    const fs::path file = coop_next_join_file();
    if (file.empty() || !galaxy::coop::write_next_join(file, {address, mods})) {
        gCoopMenu.set_message("COULD NOT JOIN: THE ROOM'S MODS COULD NOT BE SET UP");
        return false;
    }
    std::fprintf(stderr, "[coop] the room's mods are ready (%s): starting again to join %s\n",
                 galaxy::coop::mods_text(mods).c_str(), address.c_str());
    bool restarting = false;
#if defined(NEBULA_HOST_RENDERER)
    restarting = galaxy::render::request_restart();
#endif
    gCoopMenu.set_message(restarting ? "NEBULA STARTS AGAIN WITH THE ROOM'S MODS, THEN JOINS"
                                     : "THE ROOM'S MODS ARE READY: CLOSE NEBULA AND START IT AGAIN TO JOIN");
    return restarting;
}

// The player's name as the room shows it: Options' PLAYER page's, PLAYER (as the page shows it) when none was given.
std::string coop_player_name() {
    const std::string& name = gCoopMenu.room().player_name;
    return name.empty() ? std::string("PLAYER") : name;
}

// The Online page's view of the room (how many play, the host's settings for a joiner).
void note_coop_room() {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::CoopRoomStatus status;
    status.players = static_cast<unsigned>(std::max<std::size_t>(gCoopSession.players().size(), 1u));
    status.joined = gCoopSession.state() == galaxy::coop::SessionState::Joined;
    if (status.joined) status.room = gCoopSession.room();
    galaxy::render::set_coop_room_status(status);
#endif
}

// Where this player plays (the player list's galaxy and star), as the game last reported it: the session tells the
// others when it changes. Nowhere once back at the main menu.
void note_coop_place() {
    std::string stage;
    int scenario = 0;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (gCoopLocalValid) {
            stage = gCoopLocal.stage;
            scenario = gCoopLocal.scenario;
        }
    }
    gCoopSession.set_place(stage, scenario);
}

// The room's news since the last call, on the game's thread: logged and shown, and a room that ended under the player
// (its host quit, crashed or stopped answering, or this player was removed) takes them back to the main menu.
void handle_coop_session_events() {
    using Kind = galaxy::coop::SessionEvent::Kind;
    bool changed = false;
    for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
        switch (event.kind) {
        case Kind::PlayerJoined:
            std::fprintf(stderr, "[coop] %s joined\n", event.name.c_str());
            gCoopChat.notice(galaxy::coop::ChatLine::Kind::Joined, event.name, galaxy::coop::chat_clock_ms());
            changed = true;
            break;
        case Kind::PlayerLeft:
            std::fprintf(stderr, "[coop] %s left%s%s\n", event.name.c_str(), event.text.empty() ? "" : ": ",
                         event.text.c_str());
            gCoopChat.notice(galaxy::coop::ChatLine::Kind::Left, event.name, galaxy::coop::chat_clock_ms());
            changed = true;
            break;
        case Kind::RoomChanged: changed = true; break;
        case Kind::Moderator: gCoopChat.say("CoopCmdModeratorGranted"); break;
        case Kind::KeepBan:
        case Kind::KeepModerator: keep_coop_list(event.kind == Kind::KeepBan, event.text); break;
        case Kind::Closed:
            std::fprintf(stderr, "[coop] the room ended: %s\n", event.text.empty() ? "left" : event.text.c_str());
            gCoopChat.clear();
            gCoopPopups.clear();
            if (!event.text.empty()) {
                set_coop_notice(event.text);
#if defined(NEBULA_HOST_RENDERER)
                if (galaxy::render::coop_in_game()) galaxy::render::request_leave_game();
#endif
            }
            changed = true;
            break;
        default: break;
        }
    }
    if (changed) note_coop_room();
}

// What happened in the room since the last call, on the game's thread: logged, and a room that ended while playing
// takes the player back to the main menu. The host's room settings (the Online page's) and the player's name (Options'
// PLAYER page's) reach the others as they change.
void pump_coop_session() {
    note_coop_place();
    if (!gCoopSession.active()) {
        if (gCoopWorld.active()) gCoopWorld.stop();
        gCoopAchievementsWaiting.clear();
        gCoopPopups.clear();
        // The room may have just ended on the session's own thread (the host left, or stopped answering): the session
        // is idle already, but its Closed event still waits here to say why and take the player to the main menu.
        if (!gCoopMenu.joining()) handle_coop_session_events();
        return;
    }
    // A join connecting from the main menu: its events are poll_coop_join's (Joined above all, which plays the save).
    if (gCoopMenu.joining()) return;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state == galaxy::coop::SessionState::Hosting || state == galaxy::coop::SessionState::Joined) {
        send_coop_snapshot();
        gCoopWorld.update();
        take_coop_achievements();
#if defined(NEBULA_HOST_RENDERER)
        gCoopPopups.set_options(galaxy::get_runtime_settings().coop_popups);
#endif
        gCoopPopups.update(gCoopSession.players(), galaxy::coop::chat_clock_ms());
    }
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::RuntimeCoopSettings settings = galaxy::get_runtime_settings().coop;
    if (gCoopSession.is_host()) gCoopSession.set_room(settings);
    gCoopSession.set_name(settings.player_name);
#endif
    // The character the game plays now (its snapshot's, which changes as a switch reloads the player's spot), not
    // the one wanted: the others' nametags, list and chat follow the model they see.
    {
        bool valid = false, luigi = false;
        {
            const std::lock_guard<std::mutex> lock(gCoopLock);
            valid = gCoopLocalValid;
            luigi = (gCoopLocal.flags & NEBULA_COOP_PLAYER_LUIGI) != 0u;
        }
        if (valid) gCoopSession.set_character(luigi ? galaxy::CoopCharacter::Luigi : galaxy::CoopCharacter::Mario);
    }
    handle_coop_session_events();
}

#if defined(NEBULA_HOST_RENDERER)
bool host_settings_open();

// The chat's line, each scene frame: T opens it while a room's game shows with no menu over it (the window takes the
// key, galaxy/render/galaxy_select.h), the keyboard's typing goes to it, and it closes as it is sent or dropped, or when
// a menu comes over the game.
void pump_coop_chat() {
    const bool available = gCoopSession.active() && galaxy::render::coop_in_game() && !host_settings_open() &&
                           !gCoopMenu.is_open();
    galaxy::render::set_coop_chat_available(available);
    // Whether the window's typing is the chat's (the main menu's text rows have it otherwise).
    static bool s_typing_here = false;
    if (galaxy::render::take_coop_chat_open()) {
        s_typing_here = true;
        if (available) (void)gCoopChat.open();
    }
    if (!available) {
        gCoopChat.close();
    } else if (gCoopChat.typing()) {
        const std::uint64_t now = galaxy::coop::chat_clock_ms();
        for (const char32_t key : galaxy::render::take_coop_text_input()) gCoopChat.type(key, now);
    }
    // Sent, dropped, or ended with the room: the keys play again.
    if (s_typing_here && !gCoopChat.typing()) {
        galaxy::render::set_coop_text_entry(false);
        s_typing_here = false;
    }
}
#endif

// Host: the room opens on the Host page's port as the save starts. False (and why on the menu) when it can't.
bool open_coop_room() {
    galaxy::coop::HostConfig config;
    config.room = gCoopMenu.room();
    config.port = config.room.port;
    config.game_version = kCoopGameVersion;
    config.name = coop_player_name();
    config.character = config.room.character;
    config.save = coop_room_save;
    config.disc = gCoopDisc;
    config.mods = coop_session_mods();
    config.bans = galaxy::get_runtime_settings().coop_bans;
    config.moderators = galaxy::get_runtime_settings().coop_moderators;
    listen_coop_snapshots();
    if (!gCoopSession.host(std::move(config))) {
        std::fprintf(stderr, "[coop] the room could not open on port %u\n", static_cast<unsigned>(gCoopMenu.room().port));
        gCoopMenu.set_message("PORT " + std::to_string(gCoopMenu.room().port) +
                              " COULD NOT BE OPENED: ANOTHER PROGRAM HAS IT. PICK ANOTHER PORT");
        return false;
    }
    (void)gCoopSession.take_events();
    gCoopWorld.start();
    std::fprintf(stderr, "[coop] room open on UDP port %u, %s, mods: %s\n", static_cast<unsigned>(gCoopSession.port()),
                 galaxy::coop::disc_text(gCoopDisc).c_str(), galaxy::coop::mods_text(coop_session_mods()).c_str());
    note_coop_room();
    return true;
}

// Join: connects to the Join page's address. How it goes comes to poll_coop_join.
void start_coop_join() {
    keep_coop_settings();
    const galaxy::RuntimeCoopSettings& room = gCoopMenu.room();
    galaxy::coop::JoinConfig config;
    if (!galaxy::parse_coop_endpoint(room.join_address, config.address, config.port)) {
        gCoopMenu.set_message("THAT ISN'T AN ADDRESS: HOST, HOST:PORT OR [IPV6]:PORT");
        return;
    }
    config.password = room.password;
    config.game_version = kCoopGameVersion;
    config.name = coop_player_name();
    config.character = room.character;
    config.disc = gCoopDisc;
    config.mods = coop_session_mods();
    config.mods_cache = coop_mods_cache();
    config.find_mod = find_own_mod;
    std::fprintf(stderr, "[coop] joining %s port %u\n", config.address.c_str(), static_cast<unsigned>(config.port));
    listen_coop_snapshots();
    if (!gCoopSession.join(std::move(config))) {
        std::string why = "THE ADDRESS COULD NOT BE REACHED";
        for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
            if (event.kind == galaxy::coop::SessionEvent::Kind::JoinFailed) why = event.text;
        }
        gCoopMenu.set_message("COULD NOT JOIN: " + why);
        return;
    }
    gCoopMenu.set_joining(true);
}

// While a join connects: true once the host let this player in and its save is ready to play.
bool poll_coop_join() {
    if (!gCoopMenu.joining()) return false;
    using Kind = galaxy::coop::SessionEvent::Kind;
    for (const galaxy::coop::SessionEvent& event : gCoopSession.take_events()) {
        if (event.kind == Kind::DownloadingMods) {
            std::fprintf(stderr, "[coop] downloading the room's mods: %s\n", event.text.c_str());
            continue;
        }
        if (event.kind == Kind::ModsReady) {
            gCoopMenu.set_joining(false);
            const std::optional<std::vector<galaxy::coop::SharedMod>> mods = gCoopSession.take_room_mods();
            if (mods) (void)restart_for_room_mods(*mods, gCoopMenu.room().join_address);
            return false;
        }
        if (event.kind == Kind::JoinFailed || event.kind == Kind::Closed) {
            gCoopMenu.set_joining(false);
            std::fprintf(stderr, "[coop] the join failed: %s\n", event.text.c_str());
            gCoopMenu.set_message("COULD NOT JOIN: " + (event.text.empty() ? std::string("CANCELLED") : event.text));
            return false;
        }
        if (event.kind != Kind::Joined) continue;
        const std::optional<galaxy::coop::RoomSave> save = gCoopSession.take_room_save();
        fs::path folder;
        if (!save || !write_joined_save(*save, folder)) {
            gCoopMenu.set_joining(false);
            gCoopSession.leave();
            (void)gCoopSession.take_events();
            gCoopMenu.set_message("COULD NOT JOIN: THE HOST'S SAVE COULD NOT BE WRITTEN");
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
            gCoopSaveFolder = folder;
        }
        gCoopSaveFile = save->file;
#if defined(NEBULA_HOST_RENDERER)
        // The save's achievements (its achievements.ini came with it), before the room's start coming.
        galaxy::achievements::set_file(folder / "achievements.ini");
#endif
        gCoopWorld.start();
        gCoopLuigi.store(gCoopMenu.room().character == galaxy::CoopCharacter::Luigi, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_coop_in_game(true);
#endif
        std::fprintf(stderr, "[coop] joined: the host's save (file %u, %zu file(s)), %zu player(s)\n", save->file,
                     save->files.size(), gCoopSession.players().size());
        gCoopMenu.finish_join("JOINED! LOADING THE HOST'S SAVE...");
        // A moderator the host kept is one again (the rest of this join's events went with Joined).
        if (gCoopSession.is_moderator()) gCoopChat.say("CoopCmdModeratorGranted");
        note_coop_room();
        return true;
    }
    // The joining screen: what the join waits for, and how far (the mods' download by its bytes).
    using Step = galaxy::coop::JoinStep;
    switch (gCoopSession.join_step()) {
    case Step::Reaching: gCoopMenu.set_join_status("CONNECTING TO THE HOST...", {}, 10u); break;
    case Step::Asking: gCoopMenu.set_join_status("WAITING FOR THE HOST TO LET YOU IN...", {}, 30u); break;
    case Step::DownloadingMods: {
        const galaxy::coop::DownloadProgress progress = gCoopSession.download_progress();
        const unsigned percent =
            progress.total == 0u ? 100u : static_cast<unsigned>(progress.done * 100u / progress.total);
        gCoopMenu.set_join_status("DOWNLOADING THE ROOM'S MODS: " + std::to_string(percent) + "%", progress.mod,
                                  30u + percent * 60u / 100u);
        break;
    }
    case Step::None: break;
    }
    return false;
}

// The menu's settings (the room's, the player's, the last address) as the runtime settings, saved as they change.
void keep_coop_settings() {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
    // The player's name and character are Options' (its PLAYER page): the menu takes them, never gives them back.
    if (gCoopMenu.room().player_name != settings.coop.player_name ||
        gCoopMenu.room().character != settings.coop.character) {
        galaxy::RuntimeCoopSettings room = gCoopMenu.room();
        room.player_name = settings.coop.player_name;
        room.character = settings.coop.character;
        gCoopMenu.set_room(room);
    }
    if (settings.coop == gCoopMenu.room()) return;
    settings.coop = gCoopMenu.room();
    galaxy::set_runtime_settings(settings);
    (void)galaxy::save_runtime_settings_to_file();
#endif
}

// /permban and /moderator: who they named, kept with the player's settings for every room it hosts.
void keep_coop_list(bool ban, const std::string& identity) {
    galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
    std::vector<std::string>& list = ban ? settings.coop_bans : settings.coop_moderators;
    if (identity.empty() || std::find(list.begin(), list.end(), identity) != list.end()) return;
    list.push_back(identity);
    galaxy::set_runtime_settings(settings);
    (void)galaxy::save_runtime_settings_to_file();
}

// The mods folder's mods for the Host page: mods.ini's on and off (the room last hosted), the ones on in their order.
std::vector<galaxy::coop::RoomMod> coop_room_mods() {
    std::vector<galaxy::coop::RoomMod> room;
    if (galaxy::mods::mods_folder().empty()) return room;
    for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods()) room.push_back({mod.id, mod.enabled});
    return room;
}

// The Host page's mods to mods.ini (in its order, the ones on first); texture packs and movies apply at once. True when
// the disc's mods now differ from the ones this session started with: a restart applies them.
bool keep_coop_mods() {
    if (galaxy::mods::mods_folder().empty()) return false;
    std::vector<galaxy::mods::ModInfo> scanned = galaxy::mods::scan_mods();
    if (gCoopMenu.mods_changed()) {
        std::vector<galaxy::mods::ModInfo> ordered;
        for (const galaxy::coop::RoomMod& room : gCoopMenu.room_mods()) {
            const auto it = std::find_if(scanned.begin(), scanned.end(),
                                         [&room](const galaxy::mods::ModInfo& mod) { return mod.id == room.id; });
            if (it == scanned.end()) continue;
            it->enabled = room.on;
            ordered.push_back(std::move(*it));
            scanned.erase(it);
        }
        // Mods new since the page read the folder stay as mods.ini has them, after the room's.
        for (galaxy::mods::ModInfo& mod : scanned) ordered.push_back(std::move(mod));
        std::stable_partition(ordered.begin(), ordered.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; });
        for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i].order = static_cast<unsigned>(i + 1u);
        if (!galaxy::mods::save_mod_settings(ordered)) {
            std::fprintf(stderr, "[coop] mods.ini could not be written\n");
        }
        scanned = std::move(ordered);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_texture_pack_mod_folders(galaxy::mods::texture_folders(scanned));
        galaxy::render::set_movie_mod_folders(galaxy::mods::movie_folders(scanned));
#endif
    }
    return galaxy::mods::mods_differ_from_session(scanned);
}

// Host's save, as Play leaves the menu: made when new, then the game's save folder (save_folder_override), with its
// achievements; the room's settings and mods are kept. When the room's mods change the disc, Nebula starts again to
// apply them and plays the save once back at the menu. False when it doesn't play now.
bool start_coop_save() {
    keep_coop_settings();
    if (!gCoopSaves) return false;
    std::string id = gCoopMenu.chosen_save();
    if (id.empty()) id = gCoopSaves->create();
    const std::optional<galaxy::coop::SaveInfo> save = gCoopSaves->find(id);
    if (!save) {
        std::fprintf(stderr, "[coop] the save could not be made in %s\n", gCoopSaves->root().string().c_str());
        return false;
    }
    if (keep_coop_mods()) {
        bool restarting = false;
#if defined(NEBULA_HOST_RENDERER)
        restarting = gCoopSaves->set_next_start(id) && galaxy::render::request_restart();
#endif
        if (restarting) {
            std::fprintf(stderr, "[coop] restarting for the room's mods, then hosting %s\n", id.c_str());
            gCoopMenu.set_message("NEBULA STARTS AGAIN WITH THE ROOM'S MODS, THEN HOSTS");
            return false;
        }
        (void)gCoopSaves->take_next_start();
        gCoopMenu.set_message("THE ROOM'S MODS APPLY WHEN NEBULA STARTS AGAIN: CLOSE IT AND START IT AGAIN TO HOST");
        return false;
    }
    if (!open_coop_room()) return false;
    (void)gCoopSaves->mark_played(id, static_cast<std::int64_t>(std::time(nullptr)));
    const fs::path folder = gCoopSaves->folder(id);
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        gCoopSaveFolder = folder;
    }
    gCoopSaveFile = save->file;
    gCoopLuigi.store(gCoopMenu.room().character == galaxy::CoopCharacter::Luigi, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::set_file(folder / "achievements.ini");
    galaxy::render::set_coop_in_game(true);
#endif
    std::fprintf(stderr, "[coop] hosting save %s (%s, file %u) as %s\n", save->id.c_str(), save->name.c_str(),
                 save->file, galaxy::coop_character_key(gCoopMenu.room().character));
    return true;
}

// The old saves (the NAND's, before co-op): once, a co-op save from each user file they hold, with the achievements
// of then. Not after a co-op save played: the game's files are its then.
void convert_old_saves(unsigned files) {
    {
        const std::lock_guard<std::mutex> lock(gCoopSaveMutex);
        if (!gCoopSaveFolder.empty()) return;
    }
    if (!gCoopSaves || gCoopSaves->converted() || gSaveDataPath.empty()) return;
    const fs::path old_saves = gSaveRoot / gSaveDataPath.substr(1);
    fs::path achievements;
#if defined(NEBULA_HOST_RENDERER)
    const fs::path settings_folder = galaxy::runtime_settings_folder();
    if (!settings_folder.empty()) achievements = settings_folder / "achievements.ini";
#endif
    if (gCoopSaves->convert(old_saves, files & 0x3fu, achievements)) {
        std::fprintf(stderr, "[coop] the old saves' files (0x%02x) are co-op saves now\n", files & 0x3fu);
    }
}

bool host_settings_open() {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::native_settings_view().open;
#else
    return false;
#endif
}

void fill_coop_menu_view(NebulaSettingsView* view) {
#if defined(NEBULA_HOST_RENDERER)
    // What the keyboard typed into a text row; while one is edited the window's keys type instead of playing.
    for (const char32_t key : galaxy::render::take_coop_text_input()) gCoopMenu.type(key);
    galaxy::render::set_coop_text_entry(gCoopMenu.editing());
#endif
    const galaxy::coop::MenuView menu = gCoopMenu.view();
    view->kind = NEBULA_SETTINGS_KIND_MAIN_MENU;
    view->open = menu.open ? 1u : 0u;
    view->page = menu.page;
    view->page_count = static_cast<uint32_t>(std::min<std::size_t>(menu.pages.size(), NEBULA_SETTINGS_PAGES));
    for (uint32_t i = 0; i < view->page_count; ++i) {
        copy_settings_text(view->pages[i], menu.pages[i]);
    }
    view->total_rows = static_cast<uint32_t>(std::min<std::size_t>(menu.rows.size(), NEBULA_SETTINGS_ROWS));
    view->row_count = view->total_rows;
    for (uint32_t i = 0; i < view->row_count; ++i) {
        copy_settings_text(view->rows[i].label, menu.rows[i].label);
        copy_settings_text(view->rows[i].value, menu.rows[i].value);
        view->rows[i].flags = (menu.rows[i].buttons ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_BUTTONS) : 0u) |
                              (menu.rows[i].changes ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_CHANGES) : 0u) |
                              (menu.rows[i].name ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_NAME) : 0u) |
                              (menu.rows[i].text ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_TEXT) : 0u) |
                              (menu.rows[i].label_name ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_LABEL_NAME) : 0u) |
                              (menu.rows[i].bar_steps != 0u ? static_cast<uint32_t>(NEBULA_SETTINGS_ROW_SLIDER) : 0u);
        // A loading bar: a slider that doesn't change (no NEBULA_SETTINGS_ROW_CHANGES).
        view->rows[i].slider_steps = static_cast<uint16_t>(menu.rows[i].bar_steps);
        view->rows[i].slider_at = static_cast<uint16_t>(menu.rows[i].bar_at);
    }
    view->selected = menu.selected;
    view->selected_button = menu.selected_button;
    view->back = menu.back ? 1u : 0u;
    view->editing = menu.editing ? 1u : 0u;
    if (menu.selected < menu.rows.size()) {
        copy_settings_text(view->help, menu.rows[menu.selected].help);
    }
}

void svc_settings_view(void*, NebulaSettingsView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::render::NativeSettingsView source = galaxy::render::native_settings_view();
    if (!source.open && gCoopMenu.is_open()) {
        fill_coop_menu_view(view);
        // The hints show what the player last used (or the chosen button icons), as the settings' do: a controller's
        // buttons, not the keyboard's keys, once a controller moves the menu.
        view->input = source.input;
        return;
    }
    view->open = source.open ? 1u : 0u;
    view->page = source.page;
    view->page_count = static_cast<uint32_t>(std::min<std::size_t>(source.pages.size(), NEBULA_SETTINGS_PAGES));
    for (uint32_t i = 0; i < view->page_count; ++i) {
        copy_settings_text(view->pages[i], source.pages[i]);
    }
    view->controls_page = source.controls_page;
    view->input = source.input;
    // A long page (many mods) is sent in part: the rows around the selection, more than the menu shows at once.
    const std::size_t total = source.rows.size();
    const std::size_t selected = std::min<std::size_t>(source.selected, total == 0u ? 0u : total - 1u);
    const std::size_t first = total <= NEBULA_SETTINGS_ROWS
                                  ? 0u
                                  : std::min(selected - std::min<std::size_t>(selected, NEBULA_SETTINGS_ROWS / 2u),
                                             total - NEBULA_SETTINGS_ROWS);
    view->total_rows = static_cast<uint32_t>(total);
    view->first_row = static_cast<uint32_t>(first);
    view->row_count = static_cast<uint32_t>(std::min<std::size_t>(total - first, NEBULA_SETTINGS_ROWS));
    for (uint32_t i = 0; i < view->row_count; ++i) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[first + i];
        copy_settings_text(view->rows[i].label, row.label);
        copy_settings_text(view->rows[i].value, row.value);
        view->rows[i].flags = (row.heading ? NEBULA_SETTINGS_ROW_HEADING : 0u) |
                              (row.changes ? NEBULA_SETTINGS_ROW_CHANGES : 0u) | (row.name ? NEBULA_SETTINGS_ROW_NAME : 0u) |
                              (row.active ? NEBULA_SETTINGS_ROW_ACTIVE : 0u) |
                              (row.label_name ? NEBULA_SETTINGS_ROW_LABEL_NAME : 0u) |
                              (row.buttons ? NEBULA_SETTINGS_ROW_BUTTONS : 0u) |
                              (row.mod_list == 1u ? NEBULA_SETTINGS_ROW_MOD_OFF : 0u) |
                              (row.mod_list == 2u ? NEBULA_SETTINGS_ROW_MOD_ON : 0u) | (row.held ? NEBULA_SETTINGS_ROW_HELD : 0u) |
                              (row.slider_steps != 0u ? NEBULA_SETTINGS_ROW_SLIDER : 0u) |
                              (row.inputs ? NEBULA_SETTINGS_ROW_BINDING : 0u) |
                              (row.shared ? NEBULA_SETTINGS_ROW_SHARED : 0u) |
                              (row.text ? NEBULA_SETTINGS_ROW_TEXT : 0u);
        view->rows[i].slider_steps = static_cast<uint16_t>(row.slider_steps);
        view->rows[i].slider_at = static_cast<uint16_t>(row.slider_at);
    }
    view->selected = static_cast<uint32_t>(selected);
    view->selected_button = source.selected_button;
    view->editing = source.editing ? 1u : 0u;
    for (std::size_t r = 0; r < total; ++r) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[r];
        if (row.mod_list == 0u || row.mod_list > 2u || row.heading) continue;
        if (r < first) ++view->mod_list_first[row.mod_list - 1u];
        ++view->mod_list_count[row.mod_list - 1u];
    }
    copy_settings_text(view->help, source.help);
    view->status = static_cast<uint32_t>(source.status);
    view->status_number = source.status_number;
    copy_settings_text(view->status_text, source.status_text);
#else
    if (gCoopMenu.is_open()) fill_coop_menu_view(view);
#endif
}

// The pointer's input to the settings menu: NEBULA_SETTINGS_* are galaxy::render::NativeSettingsInput's values.
void svc_settings_input(void*, uint32_t action, int32_t arg) {
    if (gCoopMenu.is_open() && !host_settings_open()) {
        switch (action) {
        case NEBULA_SETTINGS_SELECT_PAGE: gCoopMenu.select_page(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_SELECT_ROW: gCoopMenu.select_row(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_MOVE: gCoopMenu.move(arg); break;
        case NEBULA_SETTINGS_CHANGE: gCoopMenu.change(arg); break;
        case NEBULA_SETTINGS_PRESS: gCoopMenu.press(static_cast<unsigned>(std::max(arg, 0))); break;
        case NEBULA_SETTINGS_CLOSE: gCoopMenu.back(); break;
        case NEBULA_SETTINGS_REVERT: gCoopMenu.reset(); break;
        default: break;  // nothing to apply or revert: the main menu stays until a choice leaves it
        }
        return;
    }
#if defined(NEBULA_HOST_RENDERER)
    if (action >= NEBULA_SETTINGS_OPEN && action <= NEBULA_SETTINGS_SET_VALUE) {
        galaxy::render::native_settings_input(static_cast<galaxy::render::NativeSettingsInput>(action), arg);
    }
#else
    (void)action, (void)arg;
#endif
}

// The chat as the game draws it: the lines showing now and the line typed, only in a room's game.
void svc_coop_chat(void*, NebulaCoopChatView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    if (!gCoopSession.active() || !galaxy::render::coop_in_game()) return;
#endif
    const galaxy::coop::ChatView chat = gCoopChat.view(galaxy::coop::chat_clock_ms());
    const auto copy = [](char* out, std::size_t size, const std::string& text) {
        std::snprintf(out, size, "%s", text.c_str());
    };
    for (const galaxy::coop::ChatView::Line& shown : chat.lines) {
        if (view->line_count >= NEBULA_COOP_CHAT_LINES) break;
        NebulaCoopChatLine& line = view->lines[view->line_count++];
        line.id = shown.line.id;
        switch (shown.line.kind) {
        case galaxy::coop::ChatLine::Kind::Joined: line.kind = NEBULA_COOP_CHAT_JOINED; break;
        case galaxy::coop::ChatLine::Kind::Left: line.kind = NEBULA_COOP_CHAT_LEFT; break;
        case galaxy::coop::ChatLine::Kind::Notice: line.kind = NEBULA_COOP_CHAT_NOTICE; break;
        default: line.kind = NEBULA_COOP_CHAT_MESSAGE; break;
        }
        line.character = shown.line.character == galaxy::CoopCharacter::Luigi ? 1u : 0u;
        line.alpha = static_cast<uint32_t>(std::clamp(shown.alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        copy(line.name, sizeof(line.name), shown.line.name);
        copy(line.text, sizeof(line.text), shown.line.text);
    }
    view->typing = chat.typing ? 1u : 0u;
    view->input_serial = chat.input_serial;
    view->cursor = static_cast<uint32_t>(std::min<std::size_t>(chat.cursor, sizeof(view->input) - 1u));
    copy(view->input, sizeof(view->input), chat.input);
}

int32_t svc_coop_menu(void*, uint32_t what) {
    switch (what) {
    case NEBULA_COOP_MENU_OPEN:
#if defined(NEBULA_HOST_RENDERER)
        if (!gCoopMenu.is_open()) gCoopMenu.set_room(galaxy::get_runtime_settings().coop);
        galaxy::render::set_coop_in_game(false);
#endif
        if (!gCoopMenu.is_open()) {
            // Back at the main menu: the game ended, and with it the room.
            if (gCoopSession.active()) gCoopSession.leave();
            (void)gCoopSession.take_events();
            gCoopChat.clear();
            gCoopPopups.clear();
            {
                const std::lock_guard<std::mutex> lock(gCoopLock);
                gCoopLocalValid = false;
            }
            note_coop_place();
            note_coop_room();
            gCoopMenu.set_joining(false);
            gCoopMenu.open();
            if (std::string notice = take_coop_notice(); !notice.empty()) gCoopMenu.set_message(notice);
            // Back after a restart for the room's mods: its save plays at once.
            std::string next = gCoopSaves ? gCoopSaves->take_next_start() : std::string();
            if (!next.empty()) {
                gCoopMenu.play(std::move(next));
            } else if (const CoopAutoStart start = std::exchange(gCoopAutoStart, CoopAutoStart::None);
                       start == CoopAutoStart::Host) {
                // --host: the last played save, on the port given.
                galaxy::RuntimeCoopSettings room = gCoopMenu.room();
                room.port = gCoopAutoPort;
                gCoopMenu.set_room(room);
                const std::vector<galaxy::coop::SaveInfo> saves =
                    gCoopSaves ? gCoopSaves->list() : std::vector<galaxy::coop::SaveInfo>{};
                gCoopMenu.play(saves.empty() ? std::string() : saves.front().id);
            } else if (start == CoopAutoStart::Join) {
                galaxy::RuntimeCoopSettings room = gCoopMenu.room();
                room.join_address = gCoopAutoAddress;
                gCoopMenu.set_room(room);
                start_coop_join();
            }
        }
        return 0;
    case NEBULA_COOP_MENU_CLOSE:
        gCoopMenu.close();
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_coop_text_entry(false);
#endif
        return 0;
    case NEBULA_COOP_MENU_CHARACTER:
#if defined(NEBULA_HOST_RENDERER)
        // Live: Options' PLAYER page may switch it mid-game (sm64coopdx's live character change). Not in the main
        // menu, where Host and Join take the menu's own.
        if (!gCoopMenu.is_open()) {
            gCoopLuigi.store(galaxy::get_runtime_settings().coop.character == galaxy::CoopCharacter::Luigi,
                             std::memory_order_relaxed);
        }
#endif
        return gCoopLuigi.load(std::memory_order_relaxed) ? 1 : 0;
    case NEBULA_COOP_MENU_JOINED: return gCoopSession.active() && !gCoopSession.is_host() ? 1 : 0;
    case NEBULA_COOP_MENU_LEAVE:
        // Each game scene frame: the room's news, the chat's line, and the room ending under the player.
        pump_coop_session();
#if defined(NEBULA_HOST_RENDERER)
        pump_coop_chat();
        if (galaxy::render::take_leave_game()) {
            // The room ended (the host quit, or this player was let go): out of it, then the main menu.
            if (gCoopSession.active()) {
                std::fprintf(stderr, "[coop] leaving the room\n");
                gCoopSession.leave();
                (void)gCoopSession.take_events();
                note_coop_room();
            }
            gCoopChat.clear();
            return 1;
        }
#endif
        return 0;
    case NEBULA_COOP_MENU_TITLE: {
        // The title once, as Nebula starts; not in front of a room the menu starts by itself as it opens.
        static bool shown = false;
        if (std::exchange(shown, true)) return 0;
        const bool starting = gCoopAutoStart != CoopAutoStart::None || (gCoopSaves && gCoopSaves->has_next_start());
        return starting ? 0 : 1;
    }
    case NEBULA_COOP_MENU_POLL:
        // What the player changed is kept as they change it, a text once kept.
        if (!gCoopMenu.editing()) keep_coop_settings();
        if (poll_coop_join()) return NEBULA_COOP_MENU_PLAY;
        switch (gCoopMenu.take_command()) {
        case galaxy::coop::MenuCommand::Play:
            if (gCoopMenu.joining()) return NEBULA_COOP_MENU_NONE;
            return start_coop_save() ? NEBULA_COOP_MENU_PLAY : NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::Join:
            if (!gCoopMenu.joining()) start_coop_join();
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::CancelJoin:
            gCoopMenu.set_joining(false);
            gCoopSession.leave();
            (void)gCoopSession.take_events();
            std::fprintf(stderr, "[coop] the join was cancelled\n");
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::Quit: return NEBULA_COOP_MENU_QUIT;
        case galaxy::coop::MenuCommand::Settings:
#if defined(NEBULA_HOST_RENDERER)
            galaxy::render::native_settings_input(galaxy::render::NativeSettingsInput::Open, 0);
#endif
            return NEBULA_COOP_MENU_NONE;
        case galaxy::coop::MenuCommand::None: break;
        }
        return NEBULA_COOP_MENU_NONE;
    case NEBULA_COOP_MENU_SAVE_FILE: return static_cast<int32_t>(gCoopSaveFile);
    case NEBULA_COOP_MENU_KEYS:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<int32_t>(galaxy::render::coop_keys_held());
#else
        return 0;
#endif
    default:
        if ((what & 0xffu) == NEBULA_COOP_MENU_OLD_FILES) convert_old_saves(what >> 8u);
        return 0;
    }
}

// The room's built-in Personal Star Counter mod (the Host page's Mods, the Online tab): the room's as the host set it,
// else this player's as they last hosted.
bool coop_star_counter_on() {
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state == galaxy::coop::SessionState::Hosting || state == galaxy::coop::SessionState::Joined) {
        return gCoopSession.room().star_counter;
    }
    return galaxy::get_runtime_settings().coop.star_counter;
}

// /psc [on|off] (sm64coopdx's Personal Star Counter mod's command): the host turns the counter on or off for the whole
// room, as the Host page's Mods does, and keeps it for the rooms it hosts later. Without a word, it flips it.
void add_coop_star_counter_command() {
    galaxy::coop::ChatCommand psc;
    psc.name = "psc";
    psc.help = "CoopCmdPscHelp";
    psc.options = {"on", "off"};
    psc.shown = [] { return gCoopSession.is_host(); };
    psc.run = [](std::string_view words) {
        if (!gCoopSession.is_host()) {
            gCoopChat.say("CoopCmdHostOnly");
            return true;
        }
        galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
        std::string word(words);
        for (char& c : word) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        if (!word.empty() && word != "on" && word != "off") return false;
        const bool on = word.empty() ? !coop_star_counter_on() : word == "on";
        settings.coop.star_counter = on;
        galaxy::set_runtime_settings(settings);
        (void)galaxy::save_runtime_settings_to_file();
        // The room's now, not at the session's next pump: the players' lists show it at once.
        gCoopSession.set_room(settings.coop);
        gCoopChat.say(on ? "CoopCmdPscOn" : "CoopCmdPscOff");
        return true;
    };
    (void)gCoopChat.commands().add(std::move(psc));
}

// The player list (Tab or the controller's Share held): the room's players by number, each one's place and round trip.
uint32_t svc_coop_list(void*, NebulaCoopListPlayer* players, uint32_t capacity, uint32_t* room_size) {
    if (room_size != nullptr) *room_size = 0u;
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return 0u;
    const std::vector<galaxy::coop::SessionPlayer> list = gCoopSession.players();
    const galaxy::RuntimeCoopSettings room = gCoopSession.room();
    if (room_size != nullptr) *room_size = room.max_players;
    const bool stars = room.star_counter;
    const std::size_t count = std::min<std::size_t>(list.size(), players != nullptr ? capacity : 0u);
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::coop::SessionPlayer& from = list[i];
        NebulaCoopListPlayer& to = players[i];
        to = {};
        std::snprintf(to.name, sizeof(to.name), "%s", from.name.c_str());
        std::snprintf(to.stage, sizeof(to.stage), "%s", from.stage.c_str());
        to.scenario = from.scenario;
        to.number = from.index + 1u;
        to.ping_ms = from.ping_ms;
        to.stars = from.stars;
        to.flags = (from.character == galaxy::CoopCharacter::Luigi ? NEBULA_COOP_LIST_LUIGI : 0u) |
                   (from.host ? NEBULA_COOP_LIST_HOST : 0u) | (from.local ? NEBULA_COOP_LIST_YOU : 0u) |
                   (stars ? NEBULA_COOP_LIST_STARS : 0u);
    }
    return static_cast<uint32_t>(list.size());
}

// What the game is told and hasn't taken yet (coop_world_take), in order.
std::deque<galaxy::coop::WorldEvent> gCoopWorldWaiting;
// The others' objects' states as they came, the game's next to take (coop_objects_take).
std::deque<galaxy::coop::ObjectState> gCoopStatesWaiting;

// The shared world: what the game's play did (galaxy::coop::World settles it, or sends it to the host to).
void svc_coop_world_report(void*, const NebulaCoopWorldEvent* event) {
    if (event == nullptr || !gCoopWorld.active()) return;
    NebulaCoopWorldEvent copy = *event;
    copy.name[sizeof(copy.name) - 1] = '\0';
    using galaxy::coop::SaveKind;
    switch (copy.kind) {
    case NEBULA_COOP_WORLD_ENTER:
        std::fprintf(stderr, "[coop] world: %s, scenario %d placed\n", copy.name, static_cast<int>(copy.scenario));
        // Nothing waiting from the last scene is for this one (the save comes again whole).
        gCoopWorldWaiting.clear();
        gCoopStatesWaiting.clear();
        gCoopWorld.enter({copy.name, copy.scenario});
        return;
    case NEBULA_COOP_WORLD_GONE: gCoopWorld.report_gone({copy.object, copy.value}); return;
    case NEBULA_COOP_WORLD_STAR: gCoopWorld.report_save({SaveKind::Star, copy.name, copy.scenario, 0u}); return;
    case NEBULA_COOP_WORLD_FLAG: gCoopWorld.report_save({SaveKind::Flag, copy.name, 0, 0u}); return;
    case NEBULA_COOP_WORLD_VALUE: gCoopWorld.report_save({SaveKind::Value, copy.name, 0, copy.value}); return;
    case NEBULA_COOP_WORLD_STAR_GOT:
        gCoopWorld.report_star({copy.scenario, (copy.value & NEBULA_COOP_STAR_GRAND) != 0u});
        return;
    case NEBULA_COOP_WORLD_HIT:
        if (copy.value > 0xffffu || copy.scenario < 0 || copy.scenario > 255) return;
        gCoopWorld.report_hit({copy.object, static_cast<std::uint16_t>(copy.value),
                               static_cast<std::uint8_t>(copy.scenario), 0u});
        return;
    case NEBULA_COOP_WORLD_SWITCH: gCoopWorld.report_switch({copy.object, copy.value != 0u}); return;
    case NEBULA_COOP_WORLD_MISS:
        std::fprintf(stderr, "[coop] world: a miss, the instance goes on without this player until its scene starts over\n");
        gCoopWorld.report_miss();
        return;
    default: return;
    }
}

// What the game is told, in order: what it can't take this call waits for the next.
uint32_t svc_coop_world_take(void*, NebulaCoopWorldEvent* events, uint32_t capacity) {
    std::deque<galaxy::coop::WorldEvent>& waiting = gCoopWorldWaiting;
    if (!gCoopWorld.active()) {
        waiting.clear();
        return 0u;
    }
    for (galaxy::coop::WorldEvent& event : gCoopWorld.take()) waiting.push_back(std::move(event));
    uint32_t count = 0u;
    while (events != nullptr && count < capacity && !waiting.empty()) {
        const galaxy::coop::WorldEvent& from = waiting.front();
        NebulaCoopWorldEvent& to = events[count++];
        to = {};
        using Kind = galaxy::coop::WorldEvent::Kind;
        if (from.kind == Kind::Ready) {
            to.kind = NEBULA_COOP_WORLD_READY;
        } else if (from.kind == Kind::Gone) {
            to.kind = NEBULA_COOP_WORLD_GONE;
            to.object = from.gone.object;
            to.value = from.gone.how;
        } else if (from.kind == Kind::StarGot) {
            to.kind = NEBULA_COOP_WORLD_STAR_GOT;
            to.scenario = from.star.star;
            to.value = from.star.grand ? NEBULA_COOP_STAR_GRAND : 0u;
        } else if (from.kind == Kind::Hit) {
            to.kind = NEBULA_COOP_WORLD_HIT;
            to.object = from.hit.object;
            to.value = from.hit.message;
            to.scenario = from.hit.sensor;
            to.player = from.hit.player;
        } else if (from.kind == Kind::Switch) {
            to.kind = NEBULA_COOP_WORLD_SWITCH;
            to.object = from.change.id;
            to.value = from.change.on ? 1u : 0u;
        } else {
            using galaxy::coop::SaveKind;
            to.kind = from.save.kind == SaveKind::Star   ? NEBULA_COOP_WORLD_STAR
                      : from.save.kind == SaveKind::Flag ? NEBULA_COOP_WORLD_FLAG
                                                         : NEBULA_COOP_WORLD_VALUE;
            to.scenario = from.save.scenario;
            to.value = from.save.value;
            std::snprintf(to.name, sizeof(to.name), "%s", from.save.name.c_str());
        }
        waiting.pop_front();
    }
    return count;
}

// Shared objects (galaxy::coop::ObjectState): the ones this game moves this frame, which the host lets it or not.
void svc_coop_objects_report(void*, const NebulaCoopObjectState* states, uint32_t count) {
    if (states == nullptr || count == 0u || !gCoopWorld.active()) return;
    std::vector<galaxy::coop::ObjectState> report;
    report.reserve(std::min<uint32_t>(count, 512u));
    for (uint32_t i = 0; i < count && i < 512u; ++i) {
        const NebulaCoopObjectState& from = states[i];
        galaxy::coop::ObjectState& to = report.emplace_back();
        to.object = from.object;
        to.nerve = from.nerve;
        to.step = static_cast<std::uint16_t>(std::min<uint32_t>(from.step, 0xffffu));
        to.flags = static_cast<std::uint8_t>(from.flags & (galaxy::coop::kObjectHitClaim | galaxy::coop::kObjectHitNerve));
        std::copy(std::begin(from.position), std::end(from.position), to.position.begin());
        std::copy(std::begin(from.velocity), std::end(from.velocity), to.velocity.begin());
        std::copy(std::begin(from.rotation), std::end(from.rotation), to.rotation.begin());
        to.distance = from.distance;
    }
    gCoopWorld.report_states(report);
}

// The others' objects as they came (the latest of each): what the game can't take this call waits for the next.
uint32_t svc_coop_objects_take(void*, NebulaCoopObjectState* states, uint32_t capacity) {
    std::deque<galaxy::coop::ObjectState>& waiting = gCoopStatesWaiting;
    if (!gCoopWorld.active()) {
        waiting.clear();
        return 0u;
    }
    for (const galaxy::coop::ObjectState& state : gCoopWorld.take_states()) waiting.push_back(state);
    uint32_t count = 0u;
    while (states != nullptr && count < capacity && !waiting.empty()) {
        const galaxy::coop::ObjectState& from = waiting.front();
        NebulaCoopObjectState& to = states[count++];
        to = {};
        to.object = from.object;
        to.nerve = from.nerve;
        to.step = from.step;
        to.flags = from.flags;
        std::copy(from.position.begin(), from.position.end(), to.position);
        std::copy(from.velocity.begin(), from.velocity.end(), to.velocity);
        std::copy(from.rotation.begin(), from.rotation.end(), to.rotation);
        to.distance = from.distance;
        to.player = from.player;
        waiting.pop_front();
    }
    return count;
}

// The Personal Star Counter (sm64coopdx's mod): the Power Stars this player got in the room, as the host counted them
// (galaxy::coop::World credits each star new to the session's save to who got it).
int32_t svc_coop_star_count(void*) {
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return -1;
    return static_cast<int32_t>(gCoopSession.stars());
}

// An achievement another player unlocked in the room's save, for its pop-up: 1 with its id, 0 when none waits.
int svc_coop_achievement_take(void*, char* id, uint32_t capacity) {
    if (gCoopAchievementsWaiting.empty() || id == nullptr || capacity == 0u) return 0;
    std::snprintf(id, capacity, "%s", gCoopAchievementsWaiting.front().c_str());
    gCoopAchievementsWaiting.pop_front();
    return 1;
}

// The oldest pop-up waiting, for the game's corner boxes: 1 with it, 0 when none waits. Only in a room's game, as the
// chat.
int svc_coop_popup_take(void*, NebulaCoopPopup* popup) {
    if (popup == nullptr || !gCoopSession.active()) return 0;
#if defined(NEBULA_HOST_RENDERER)
    if (!galaxy::render::coop_in_game()) return 0;
#endif
    const std::optional<galaxy::coop::Popup> next = gCoopPopups.take(galaxy::coop::chat_clock_ms());
    if (!next) return 0;
    *popup = {};
    using Kind = galaxy::coop::Popup::Kind;
    switch (next->kind) {
    case Kind::Joined: popup->kind = NEBULA_COOP_POPUP_JOINED; break;
    case Kind::Left: popup->kind = NEBULA_COOP_POPUP_LEFT; break;
    case Kind::Observatory: popup->kind = NEBULA_COOP_POPUP_OBSERVATORY; break;
    case Kind::Galaxy: popup->kind = NEBULA_COOP_POPUP_GALAXY; break;
    case Kind::EnteredHere: popup->kind = NEBULA_COOP_POPUP_ENTERED_HERE; break;
    case Kind::LeftHere: popup->kind = NEBULA_COOP_POPUP_LEFT_HERE; break;
    case Kind::Died: popup->kind = NEBULA_COOP_POPUP_DIED; break;
    case Kind::Star: popup->kind = NEBULA_COOP_POPUP_STAR; break;
    }
    popup->character = next->character == galaxy::CoopCharacter::Luigi ? 1u : 0u;
    popup->scenario = next->scenario;
    std::snprintf(popup->name, sizeof(popup->name), "%s", next->name.c_str());
    std::snprintf(popup->stage, sizeof(popup->stage), "%s", next->stage.c_str());
    return 1;
}

// The other players in this player's instance, for their copies: as they were kSnapshotDelay ago, by number. Who
// left the room is forgotten.
uint32_t svc_coop_remote(void*, NebulaCoopRemotePlayer* players, uint32_t capacity) {
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return 0u;
    std::string stage;
    int scenario = 0;
    {
        const std::lock_guard<std::mutex> lock(gCoopLock);
        if (!gCoopLocalValid) return 0u;
        stage = gCoopLocal.stage;
        scenario = gCoopLocal.scenario;
    }
    const std::vector<galaxy::coop::SessionPlayer> room = gCoopSession.players();
    std::vector<galaxy::coop::RemotePlayer> remote = gCoopRemote.sample(stage, scenario);
    std::erase_if(remote, [&](const galaxy::coop::RemotePlayer& player) {
        const bool here = std::any_of(room.begin(), room.end(), [&](const galaxy::coop::SessionPlayer& in) {
            return in.id == player.id && !in.local;
        });
        if (!here) gCoopRemote.remove(player.id);
        return !here;
    });
    // Who died here since the last frame, for the pop-ups.
    std::vector<galaxy::coop::InstancePlayer> instance;
    instance.reserve(remote.size());
    for (const galaxy::coop::RemotePlayer& player : remote) {
        instance.push_back({player.id, player.name, player.character,
                            (player.snapshot.flags & NEBULA_COOP_PLAYER_DEAD) != 0u});
    }
    gCoopPopups.update_instance(instance, galaxy::coop::chat_clock_ms());
    const std::size_t count = std::min<std::size_t>(remote.size(), players != nullptr ? capacity : 0u);
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::coop::RemotePlayer& from = remote[i];
        const galaxy::coop::PlayerSnapshot& snapshot = from.snapshot;
        NebulaCoopRemotePlayer& to = players[i];
        to = {};
        std::snprintf(to.name, sizeof(to.name), "%s", from.name.c_str());
        to.number = from.index + 1u;
        NebulaCoopPlayer& player = to.player;
        std::snprintf(player.stage, sizeof(player.stage), "%s", snapshot.stage.c_str());
        player.scenario = snapshot.scenario;
        for (std::size_t j = 0; j < 3u; ++j) {
            player.position[j] = snapshot.position[j];
            player.rotation[j] = snapshot.rotation[j];
            player.front[j] = snapshot.front[j];
            player.up[j] = snapshot.up[j];
            player.gravity[j] = snapshot.gravity[j];
            player.velocity[j] = snapshot.velocity[j];
        }
        std::snprintf(player.animation, sizeof(player.animation), "%s", snapshot.animation.c_str());
        std::snprintf(player.group, sizeof(player.group), "%s", snapshot.group.c_str());
        player.animation_frame = snapshot.frame;
        player.animation_rate = snapshot.rate;
        for (std::size_t j = 0; j < 4u; ++j) player.track_weights[j] = snapshot.weights[j];
        player.health = snapshot.health;
        player.mode = snapshot.mode;
        // The character is the one their game plays now (their snapshot's), so a live switch shows at once.
        player.flags = snapshot.flags;
        player.area = snapshot.area;
        // Their snapshot came half a round trip ago and shows kSnapshotDelay late; what they saw of this player had
        // come and shown as late. Two joiners reach each other through the host: each one's ping is to it.
        const auto them = std::find_if(room.begin(), room.end(),
                                       [&](const galaxy::coop::SessionPlayer& in) { return in.id == from.id; });
        const bool both_joiners = !gCoopSession.is_host() && them != room.end() && !them->host;
        const unsigned round_trip = them != room.end() ? them->ping_ms * (both_joiners ? 2u : 1u) : 0u;
        to.rewind_ms = round_trip + 2u * static_cast<unsigned>(galaxy::coop::kSnapshotDelay.count());
    }
    return static_cast<uint32_t>(remote.size());
}

// The room's player collision options, the host's as every player has them.
void svc_coop_rules(void*, NebulaCoopRules* rules) {
    if (rules == nullptr) return;
    *rules = {};
    const galaxy::coop::SessionState state = gCoopSession.state();
    if (state != galaxy::coop::SessionState::Hosting && state != galaxy::coop::SessionState::Joined) return;
    const galaxy::RuntimeCoopSettings room = gCoopSession.room();
    static_assert(static_cast<unsigned>(galaxy::CoopPlayerInteraction::Solid) == NEBULA_COOP_INTERACTION_SOLID &&
                  static_cast<unsigned>(galaxy::CoopPlayerInteraction::FriendlyFire) == NEBULA_COOP_INTERACTION_FRIENDLY_FIRE &&
                  static_cast<unsigned>(galaxy::CoopPvpMode::Revamped) == NEBULA_COOP_PVP_REVAMPED);
    rules->interaction = static_cast<uint32_t>(room.player_interaction);
    rules->knockback = room.knockback_strength;
    rules->pvp = static_cast<uint32_t>(room.pvp_mode);
    rules->bubble = room.bubble_death ? 1u : 0u;
}

// Nebula's screens with small text (sharp_2d): the 2D layer at the window's resolution while they show.
void svc_sharp_2d(void*, uint32_t on) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::request_sharp_2d(on != 0u);
#else
    (void)on;
#endif
}

void svc_movie_frame(void*, const char* name, int32_t frame, float frame_rate, const void* y, const void* u, const void* v,
                     uint32_t width, uint32_t height) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_movie_frame(name, frame, frame_rate, reinterpret_cast<uint64_t>(y), reinterpret_cast<uint64_t>(u),
                                   reinterpret_cast<uint64_t>(v), width, height);
#else
    (void)name, (void)frame, (void)frame_rate, (void)y, (void)u, (void)v, (void)width, (void)height;
#endif
}

void svc_scene_marker(void*, uint32_t marker, const float* view) {
    if (marker == NEBULA_SCENE_3D_BEGIN) {
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        end_boot_fast("first 3D scene");
    }
#if defined(NEBULA_HOST_RENDERER)
    if (marker == NEBULA_SCENE_3D_BEGIN && view != nullptr) {
        galaxy::render::attach_scene_view(view, view[15] != 0.0f ? view + 12 : nullptr,
                                      gSceneStage.empty() ? nullptr : gSceneStage.c_str());
    }
    if (marker == NEBULA_SCENE_3D_OPAQUE_END && view != nullptr) {
        galaxy::render::attach_scene_opaque(view);
    }
    static constexpr galaxy::render::SceneMarker kMarkers[] = {
        galaxy::render::SceneMarker::SceneBegin, galaxy::render::SceneMarker::SceneEnd,
        galaxy::render::SceneMarker::Scene2DBegin, galaxy::render::SceneMarker::Scene2DEnd,
        galaxy::render::SceneMarker::SceneOpaqueEnd};
    if (marker < std::size(kMarkers)) {
        galaxy::render::attach_scene_marker(kMarkers[marker]);
    }
#else
    (void)marker;
    (void)view;
#endif
}

void svc_stripe_particle(void*, uint64_t particle, uint32_t age_lifetime, uint8_t strip) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_stripe_particle(0, particle, age_lifetime, strip);
#else
    (void)particle;
    (void)age_lifetime;
    (void)strip;
#endif
}

// The game's steady clock: nanoseconds since the process's first read, plus one (0 means "never" to its users).
uint64_t svc_clock_ns(void*) {
    if (deterministic_mode()) {
        return gDeterministicFrames.load(std::memory_order_relaxed) * 1000000000ull / 60u + 1u;
    }
    static const Clock::time_point start = Clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) + 1u;
}

int64_t svc_calendar_ns(void*) {
    if (deterministic_mode()) {
        // A fixed date (2026-01-01 from 2000-01-01), then the frames' time.
        constexpr int64_t kFixedDate = 820454400;
        return kFixedDate * 1000000000ll + static_cast<int64_t>(gDeterministicFrames.load(std::memory_order_relaxed)) * 1000000000ll / 60;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
    const std::time_t fields = _mkgmtime(&local);
#else
    localtime_r(&seconds, &local);
    const std::time_t fields = timegm(&local);
#endif
    const int64_t unix_to_2000 = 946684800;
    const int64_t fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() %
        1000000000;
    return (static_cast<int64_t>(fields) - unix_to_2000) * 1000000000 + fraction;
}

std::atomic<int32_t> gStarPointerMode{-1};

void svc_button_icons(void*, NebulaButtonIcons* icons) {
    *icons = NebulaButtonIcons{};
#if defined(NEBULA_HOST_RENDERER)
    static_assert(galaxy::input::kButtonIconCount == NEBULA_BUTTON_ICONS &&
                  static_cast<int>(galaxy::input::ButtonIconStyle::Keyboard) == NEBULA_ICON_STYLE_KEYBOARD &&
                  static_cast<int>(galaxy::input::ButtonGlyph::Key) == NEBULA_ICON_KEY &&
                  static_cast<int>(galaxy::input::ButtonGlyph::StickAxis) == NEBULA_ICON_STICK_AXIS &&
                  static_cast<int>(galaxy::input::ButtonIconModel::GameCube) == NEBULA_ICON_MODEL_GAMECUBE &&
                  galaxy::input::kKeyIconLayoutFirst == NEBULA_KEY_LAYOUT_FIRST &&
                  galaxy::input::kKeyIconLayoutCount == NEBULA_KEY_LAYOUT_KEYS &&
                  galaxy::input::kKeyIconMouseLeft == NEBULA_KEY_MOUSE_LEFT &&
                  galaxy::input::kKeyIconOther == NEBULA_KEY_OTHER && galaxy::input::kKeyIconCount == NEBULA_KEYS &&
                  NEBULA_KEY_SPACE == 75 && NEBULA_KEY_RIGHT_WIN == 106 && NEBULA_KEYS < galaxy::input::kKeyIconNone);
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    const bool menu = mode >= 0 && static_cast<uint32_t>(mode) < galaxy::input::kStarPointerModeCount &&
                      galaxy::input::play_context_from_star_pointer_mode(static_cast<uint32_t>(mode)) !=
                          galaxy::input::PlayContext::Play;
    const galaxy::input::ButtonIconSpec spec = galaxy::host::button_icons(menu);
    icons->style = static_cast<uint8_t>(spec.style);
    icons->model = static_cast<uint8_t>(spec.model);
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        icons->glyphs[i] = static_cast<uint8_t>(spec.glyphs[i]);
        icons->places[i] = spec.places[i];
    }
    for (size_t i = 0; i < 4u; ++i) icons->pad_keys[i] = spec.pad_keys[i];
    for (size_t i = 0; i < NEBULA_KEY_LAYOUT_KEYS; ++i) icons->layout[i] = spec.layout[i];
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        std::memcpy(icons->other[i], spec.other[i].data(), NEBULA_KEY_NAME_CHARS);
        icons->other[i][NEBULA_KEY_NAME_CHARS - 1] = '\0';
    }
#endif
#if defined(NEBULA_REAL_WIIMOTE)
    icons->wii_remote = galaxy::host::real_wiimote_in_use() ? 1u : 0u;
#endif
}

// GALAXY_EXPORT_ART=<folder>: Nebula's own pictures (NebulaIcons) as a texture pack, pack/RMG/tex1_..._5.png at their
// HD size, named by the texture the game draws; previews in native/ and hd/; pack-names.txt pairs them.
// The scale the renderer last drew the 2D layer at (render_core: the scene target in use when the scene ends).
uint32_t svc_layer_2d_lines(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::layer_2d_lines();
#else
    return 0u;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The settings' galaxy select: its galaxies for the game to name, and the names it gives.
int svc_galaxy_select_stage(void*, uint32_t index, char* id, uint32_t capacity, uint32_t* stars) {
    if (index >= galaxy::render::kGalaxyStageCount || id == nullptr || capacity == 0u) {
        return 0;
    }
    std::snprintf(id, capacity, "%s", galaxy::render::kGalaxyStages[index].id);
    if (stars != nullptr) *stars = galaxy::render::kGalaxyStages[index].stars;
    return 1;
}

int svc_galaxy_select_names_wanted(void*) {
    return galaxy::render::take_galaxy_select_names_wanted() ? 1 : 0;
}

void svc_galaxy_select_name(void*, uint32_t index, uint32_t star, const uint16_t* name) {
    galaxy::render::set_galaxy_select_name(index, star, message_utf8(name));
}
#else
int svc_galaxy_select_stage(void*, uint32_t, char*, uint32_t, uint32_t*) { return 0; }
int svc_galaxy_select_names_wanted(void*) { return 0; }
void svc_galaxy_select_name(void*, uint32_t, uint32_t, const uint16_t*) {}
#endif

// A loaded model's triangles: the analysis its curved surfaces are rounded by, kept by its position array's address
// (galaxy/render/curved_surfaces.h).
void svc_curved_surface(void*, uint64_t key, const float* positions, uint32_t position_count, const float* normals,
                        uint32_t normal_count, const uint16_t* corners, uint32_t triangle_count) {
#if defined(NEBULA_HOST_RENDERER)
    namespace curved = galaxy::render::curved;
    if (positions == nullptr || normals == nullptr || corners == nullptr || triangle_count == 0u) {
        return;
    }
    std::vector<curved::Vec3> p(position_count);
    std::vector<curved::Vec3> n(normal_count);
    std::vector<curved::Corner> c(static_cast<std::size_t>(triangle_count) * 3u);
    std::memcpy(p.data(), positions, p.size() * sizeof(curved::Vec3));
    std::memcpy(n.data(), normals, n.size() * sizeof(curved::Vec3));
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = curved::Corner{corners[2u * i], corners[2u * i + 1u]};
    }
    curved::register_model_later(key, std::move(p), std::move(n), std::move(c));
#else
    (void)key, (void)positions, (void)position_count, (void)normals, (void)normal_count, (void)corners,
        (void)triangle_count;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// Native shape arrays and groups (galaxy/render/native_shapes.h): the game registers them from its loading threads too,
// so they wait here and join the stream ahead of the next draw record (which the game makes after them).
std::mutex gShapeLock;
std::vector<galaxy::render::shapes::Record> gShapePending;
std::atomic<bool> gShapePendingAny{false};

void queue_shape_record(galaxy::render::shapes::Record record) {
    std::lock_guard guard(gShapeLock);
    gShapePending.push_back(std::move(record));
    gShapePendingAny.store(true, std::memory_order_release);
}
#endif

void svc_shape_array(void*, const NebulaShapeArray* array) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (array == nullptr || (array->values == nullptr && array->count != 0u)) {
        return;
    }
    auto values = std::make_shared<shapes::Array>();
    values->count = array->count;
    std::size_t floats = 0;
    switch (array->kind) {
    case NEBULA_SHAPE_ARRAY_POSITION: values->kind = shapes::ArrayKind::Position; floats = 3u; break;
    case NEBULA_SHAPE_ARRAY_NORMAL: values->kind = shapes::ArrayKind::Normal; floats = 3u; break;
    case NEBULA_SHAPE_ARRAY_NBT: values->kind = shapes::ArrayKind::Nbt; floats = 9u; break;
    case NEBULA_SHAPE_ARRAY_TEXCOORD: values->kind = shapes::ArrayKind::TexCoord; floats = 2u; break;
    case NEBULA_SHAPE_ARRAY_COLOR: values->kind = shapes::ArrayKind::Color; break;
    default: die("a shape array of unknown kind %u", array->kind);
    }
    if (values->kind == shapes::ArrayKind::Color) {
        const auto* colors = static_cast<const uint32_t*>(array->values);
        values->colors.assign(colors, colors + array->count);
    } else {
        const auto* f = static_cast<const float*>(array->values);
        values->floats.assign(f, f + static_cast<std::size_t>(array->count) * floats);
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::Array;
    record.key = array->key;
    record.array = std::move(values);
    queue_shape_record(std::move(record));
#else
    (void)array;
#endif
}

void svc_shape_forget(void*, uint64_t start, uint64_t end) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    forget_handed(start, static_cast<uint32_t>(end - start));
    shapes::Record record;
    record.kind = shapes::Record::Kind::Forget;
    record.key = start;
    record.position = end;
    queue_shape_record(std::move(record));
#else
    (void)start, (void)end;
#endif
}

void svc_shape_geometry(void*, const NebulaShapeGeometry* geometry) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (geometry == nullptr || (geometry->corner_count != 0u && geometry->corners == nullptr) ||
        (geometry->index_count != 0u && geometry->indices == nullptr)) {
        return;
    }
    auto g = std::make_shared<shapes::Geometry>();
    g->attributes = geometry->attributes;
    for (std::size_t i = 0; i < g->arrays.size(); ++i) {
        g->arrays[i] = geometry->arrays[i];
    }
    g->corners.resize(geometry->corner_count);
    for (uint32_t i = 0; i < geometry->corner_count; ++i) {
        const NebulaShapeCorner& in = geometry->corners[i];
        shapes::Corner& out = g->corners[i];
        out.position = in.position;
        out.normal = in.normal;
        out.tangent = in.tangent;
        out.binormal = in.binormal;
        out.color = {in.color[0], in.color[1]};
        for (unsigned k = 0; k < 8u; ++k) {
            out.texcoord[k] = in.texcoord[k];
            out.texture_matrix[k] = in.texture_matrix[k];
        }
        out.position_matrix = in.position_matrix;
    }
    g->indices.assign(geometry->indices, geometry->indices + geometry->index_count);
    for (const uint16_t index : g->indices) {
        if (index >= geometry->corner_count) {
            die("a shape group's triangle names corner %u of %u", index, geometry->corner_count);
        }
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::Geometry;
    record.key = geometry->key;
    record.geometry = std::move(g);
    queue_shape_record(std::move(record));
#else
    (void)geometry;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// A record the game makes now runs in the order the game makes it, after the arrays and groups it registered before it
// (which join the records here).
void attach_pending_records() {
    if (gShapePendingAny.load(std::memory_order_acquire)) {
        std::vector<galaxy::render::shapes::Record> pending;
        {
            std::lock_guard guard(gShapeLock);
            pending.swap(gShapePending);
            gShapePendingAny.store(false, std::memory_order_relaxed);
        }
        for (galaxy::render::shapes::Record& record : pending) {
            galaxy::render::attach_shape_record(std::move(record));
        }
    }
}

#endif

// A group drawn.
#if defined(NEBULA_HOST_RENDERER)
// A draw's matrices (NebulaDrawMatrix) as the renderer's records hold them.
void copy_draw_matrices(const NebulaDrawMatrix* matrices, uint32_t count, galaxy::render::shapes::Record& record) {
    namespace shapes = galaxy::render::shapes;
    const uint32_t total = matrices != nullptr ? count : 0u;
    shapes::Matrix* out = galaxy::render::reserve_record_matrices(record, total);
    for (uint32_t i = 0; i < total; ++i) {
        const NebulaDrawMatrix& in = matrices[i];
        if (in.kind > NEBULA_DRAW_MATRIX_POST_TEXTURE) {
            die("a draw's matrix of unknown kind %u", in.kind);
        }
        const unsigned places = in.kind <= NEBULA_DRAW_MATRIX_NORMAL ? 10u
                                : in.kind == NEBULA_DRAW_MATRIX_POST_TEXTURE ? 21u : 11u;
        if (in.place >= places) {
            die("a draw's matrix of kind %u at place %u", in.kind, in.place);
        }
        shapes::Matrix& m = out[i];
        m.kind = static_cast<shapes::MatrixKind>(in.kind);
        m.place = in.place;
        m.model_matrix = in.model_matrix;
        std::memcpy(m.values.data(), in.values, sizeof(in.values));
    }
}
#endif

void svc_shape_draw(void*, uint64_t shape, uint64_t position, uint64_t normal, uint64_t color,
                    const NebulaDrawMatrix* matrices, uint32_t matrix_count, const NebulaShapeCurrent* current,
                    const NebulaShapeTexGen* texgens, uint32_t texgen_count, const NebulaPassState* pass) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    attach_pending_records();
    shapes::Record record;
    record.kind = shapes::Record::Kind::Draw;
    std::memcpy(&record.pass, pass, sizeof(record.pass));
    record.key = shape;
    record.position = position;
    record.normal = normal;
    record.color = color;
    copy_draw_matrices(matrices, matrix_count, record);
    for (uint32_t i = 0; texgens != nullptr && i < texgen_count; ++i) {
        const NebulaShapeTexGen& g = texgens[i];
        if (record.texgen_count < record.texgens.size()) {
            record.texgens[record.texgen_count++] = {g.coord, g.type, g.source, g.normalize, g.post_matrix};
        }
    }
    if (current != nullptr) {
        record.has_current = true;
        record.current[0] = current->position;
        std::memcpy(record.current.data() + 1, current->texture, 8);
    }
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)shape, (void)position, (void)normal, (void)color, (void)matrices, (void)matrix_count,
        (void)current, (void)texgens, (void)texgen_count, (void)pass;
#endif
}

// An image the game hands over, named by its address in the game's memory.
void svc_texture_image(void*, const NebulaTextureImage* image) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (image == nullptr || image->bytes == nullptr || image->size == 0u) {
        return;
    }
    const auto* first = static_cast<const std::byte*>(image->bytes);
    const uint64_t address = reinterpret_cast<uint64_t>(image->bytes);
    // The copies the renderer has (gHanded): an image handed over again unchanged needs nothing. Loading threads hand
    // images over too, so the record joins the stream ahead of the next one, as shape registrations do.
    std::shared_ptr<const std::vector<std::byte>> copy;
    // Linear texels the game wrote (nebula_cooked_texture.h): kept as a texture record, its header first.
    const bool linear = image->format >= NEBULA_TEXTURE_LINEAR_L8 && image->format <= NEBULA_TEXTURE_LINEAR_RGBA8;
    const std::size_t header = linear ? NEBULA_TEXTURE_HEADER : 0u;
    // A record the game made (NEBULA_TEXTURE_MADE) in rows of texels: named below by them, as linear texels are; its
    // header in our copy differs from the game's, so only its texels are compared.
    const NebulaTexture* made = image->format == NEBULA_TEXTURE_IMAGE ? nebula_texture_record(image->bytes) : nullptr;
    if (made != nullptr && (made->disc_format != NEBULA_TEXTURE_MADE || made->layout == NEBULA_TEXTURE_BLOCK4 ||
                            image->size < NEBULA_TEXTURE_HEADER)) {
        made = nullptr;
    }
    const std::size_t same_from = made != nullptr ? NEBULA_TEXTURE_HEADER : 0u;
    {
        std::lock_guard guard(gHandedLock);
        auto& last = gHanded[address];
        if (last != nullptr && last->size() == header + image->size &&
            std::memcmp(last->data() + header + same_from, first + same_from, image->size - same_from) == 0) {
            return;
        }
        auto bytes = std::make_shared<std::vector<std::byte>>(header + image->size);
        if (linear) {
            nebula_texture_init(bytes->data(), image->width, image->height, 1u,
                                nebula_texture_linear_layout(image->format),
                                NEBULA_TEXTURE_FILTER_AT_UPLOAD);
        }
        std::memcpy(bytes->data() + header, first, image->size);
        if (linear) {
            // Texture packs name it by its own texels.
            auto* record = reinterpret_cast<NebulaTexture*>(bytes->data());
            record->disc_format = image->format;
            record->pack_hash = galaxy::render::texture_pack_hash(reinterpret_cast<const uint8_t*>(first), image->size);
        } else if (made != nullptr) {
            auto* record = reinterpret_cast<NebulaTexture*>(bytes->data());
            record->disc_format = nebula_texture_linear_code(made->layout);
            record->pack_hash = galaxy::render::texture_pack_hash(
                reinterpret_cast<const uint8_t*>(first + NEBULA_TEXTURE_HEADER),
                std::min<std::size_t>(image->size - NEBULA_TEXTURE_HEADER,
                                      nebula_texture_level_bytes(made->width, made->height, 1u, made->layout)));
        }
        last = std::move(bytes);
        copy = last;
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::TextureImage;
    record.key = address;
    record.image = std::move(copy);
    queue_shape_record(std::move(record));
#else
    (void)image;
#endif
}

void svc_texture_hd(void*, const void* image, const void* hd) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (image == nullptr) {
        return;
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::TextureHd;
    record.key = reinterpret_cast<uint64_t>(image);
    if (const NebulaTexture* picture = nebula_texture_record(hd)) {
        const auto* first = static_cast<const std::byte*>(hd);
        record.image = std::make_shared<const std::vector<std::byte>>(first, first + picture->size);
    }
    queue_shape_record(std::move(record));
#else
    (void)image;
    (void)hd;
#endif
}


// The Mii library's file (mii_import.h) in the data folder; a save folder's database (or an earlier Nebula's) is
// where it starts the first time, so the Miis it holds stay in their slots. The game reads the slots as typed records.
fs::path mii_library_path() {
    return gSaveRoot / "nebula-miis.txt";
}
fs::path console_mii_database_path() {
    return gSaveRoot / "shared2/menu/FaceLib/RFL_DB.dat";
}

NebulaMii nebula_mii(const galaxy::host::MiiFields& f) {
    NebulaMii m{};
    m.sex = f.sex;
    m.birth_month = f.birth_month;
    m.birth_day = f.birth_day;
    m.favorite_color = f.favorite_color;
    m.favorite = f.favorite;
    m.local_only = f.local_only;
    m.height = f.height;
    m.build = f.build;
    m.face_type = f.face_type;
    m.face_color = f.face_color;
    m.face_texture = f.face_texture;
    m.hair_type = f.hair_type;
    m.hair_color = f.hair_color;
    m.hair_flip = f.hair_flip;
    m.eye_type = f.eye_type;
    m.eye_color = f.eye_color;
    m.eye_scale = f.eye_scale;
    m.eye_rotate = f.eye_rotate;
    m.eye_x = f.eye_x;
    m.eye_y = f.eye_y;
    m.eyebrow_type = f.eyebrow_type;
    m.eyebrow_color = f.eyebrow_color;
    m.eyebrow_scale = f.eyebrow_scale;
    m.eyebrow_rotate = f.eyebrow_rotate;
    m.eyebrow_x = f.eyebrow_x;
    m.eyebrow_y = f.eyebrow_y;
    m.nose_type = f.nose_type;
    m.nose_scale = f.nose_scale;
    m.nose_y = f.nose_y;
    m.mouth_type = f.mouth_type;
    m.mouth_color = f.mouth_color;
    m.mouth_scale = f.mouth_scale;
    m.mouth_y = f.mouth_y;
    m.mustache = f.mustache;
    m.beard_type = f.beard_type;
    m.beard_color = f.beard_color;
    m.beard_scale = f.beard_scale;
    m.beard_y = f.beard_y;
    m.glass_type = f.glass_type;
    m.glass_color = f.glass_color;
    m.glass_scale = f.glass_scale;
    m.glass_y = f.glass_y;
    m.mole_type = f.mole_type;
    m.mole_scale = f.mole_scale;
    m.mole_x = f.mole_x;
    m.mole_y = f.mole_y;
    std::copy(f.name.begin(), f.name.end(), m.name);
    std::copy(f.creator.begin(), f.creator.end(), m.creator);
    std::copy(f.create_id.begin(), f.create_id.end(), m.create_id);
    return m;
}

int32_t svc_mii_record(void*, uint32_t slot, NebulaMii* mii) {
    const std::optional<galaxy::host::MiiFields> fields = galaxy::host::mii_library_slot(slot);
    if (!fields) {
        return 0;
    }
    if (mii != nullptr) {
        *mii = nebula_mii(*fields);
    }
    return 1;
}

void svc_immediate(void*, const NebulaImmediate* draw) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (draw == nullptr || draw->count == 0u || draw->position == nullptr) {
        return;
    }
    const uint32_t n = draw->count;
    static_assert(shapes::kQuads == NEBULA_PRIMITIVE_QUADS && shapes::kPoints == NEBULA_PRIMITIVE_POINTS &&
                  shapes::kTriangleFan == NEBULA_PRIMITIVE_TRIANGLE_FAN && shapes::kLineStrip == NEBULA_PRIMITIVE_LINE_STRIP);
    shapes::Record record;
    record.kind = shapes::Record::Kind::Immediate;
    static_assert(sizeof(record.pass) == sizeof(draw->pass));
    std::memcpy(&record.pass, &draw->pass, sizeof(record.pass));
    record.primitive = draw->primitive;
    copy_draw_matrices(draw->matrices, draw->matrix_count, record);
    attach_pending_records();
    // Its values are its own: geometry the game builds as it draws.
    const bool has_normal = (draw->attributes & NEBULA_SHAPE_NORMAL) != 0u && draw->normal != nullptr;
    const bool has_color[2] = {(draw->attributes & NEBULA_SHAPE_COLOR0) != 0u && draw->color[0] != nullptr,
                               (draw->attributes & NEBULA_SHAPE_COLOR1) != 0u && draw->color[1] != nullptr};
    uint32_t attributes = (draw->attributes & ~static_cast<uint32_t>(NEBULA_SHAPE_POSITION_MATRIX)) |
                          (draw->position_matrix != nullptr ? static_cast<uint32_t>(NEBULA_SHAPE_POSITION_MATRIX) : 0u) |
                          NEBULA_SHAPE_OWN_POSITIONS;
    if (!has_normal) {
        attributes &= ~static_cast<uint32_t>(NEBULA_SHAPE_NORMAL);
    }
    if (!has_color[0]) {
        attributes &= ~static_cast<uint32_t>(NEBULA_SHAPE_COLOR0);
    }
    if (!has_color[1]) {
        attributes &= ~static_cast<uint32_t>(NEBULA_SHAPE_COLOR1);
    }
    bool has_uv[8];
    for (unsigned t = 0; t < 8u; ++t) {
        const uint32_t bit = static_cast<uint32_t>(NEBULA_SHAPE_TEXCOORD0) << t;
        has_uv[t] = (draw->attributes & bit) != 0u && draw->texcoord[t] != nullptr;
        if (!has_uv[t]) {
            attributes &= ~bit;
        }
    }
    record.attributes = attributes;
    // Lines and points become four corners each (the vertex stage widens them): made here, then widened into the pool.
    const uint32_t widened = shapes::widened_count(draw->primitive, n);
    static thread_local std::vector<galaxy::render::RenderVertex> gLinePointVertices;
    if (widened != 0u) {
        gLinePointVertices.resize(n);
    }
    galaxy::render::RenderVertex* out = widened != 0u ? gLinePointVertices.data() : galaxy::render::reserve_record_vertices(record, n);
    std::memset(out, 0, sizeof(galaxy::render::RenderVertex) * n);
    for (uint32_t i = 0; i < n; ++i) {
        galaxy::render::RenderVertex& v = out[i];
        std::memcpy(v.position, draw->position + static_cast<size_t>(i) * 3u, sizeof(v.position));
        if (has_normal) {
            std::memcpy(v.normal, draw->normal + static_cast<size_t>(i) * 3u, sizeof(v.normal));
        }
        if (has_color[0]) {
            v.color0 = draw->color[0][i];
        }
        if (has_color[1]) {
            v.color1 = draw->color[1][i];
        }
        for (unsigned t = 0; t < 8u; ++t) {
            if (has_uv[t]) {
                v.uv[t][0] = draw->texcoord[t][static_cast<size_t>(i) * 2u];
                v.uv[t][1] = draw->texcoord[t][static_cast<size_t>(i) * 2u + 1u];
            }
        }
        // Vertices without a place of their own take the draw's.
        v.mtx_indices[0] = 3u * (draw->position_matrix != nullptr ? draw->position_matrix[i] : draw->pass.position_matrix % 10u) & 0xFFu;
        // Texture matrices named by the attributes: place 0's row.
        for (unsigned t = 0; t < 8u; ++t) {
            if ((attributes & (shapes::kTextureMatrix0 << t)) != 0u) {
                const uint32_t row = 30u;
                if (t < 3u) {
                    v.mtx_indices[0] |= row << (8u * (t + 1u));
                } else if (t < 7u) {
                    v.mtx_indices[1] |= row << (8u * (t - 3u));
                } else {
                    v.mtx_indices[2] |= row;
                }
            }
        }
    }
    if (widened != 0u) {
        shapes::widen_lines_points(draw->primitive, n, out, galaxy::render::reserve_record_vertices(record, widened));
        record.primitive = shapes::kWideQuads;
        record.pass.cull = NEBULA_CULL_NONE;  // culling is for polygons
    }
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)draw;
#endif
}

void svc_screen_probe(void*, uint32_t channel, uint32_t what, const NebulaScreenProbe* probes, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    static_assert(sizeof(NebulaScreenProbe) == sizeof(galaxy::render::ScreenProbe) &&
                  NEBULA_SCREEN_PROBES_MAX == galaxy::render::kScreenProbesMax);
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::Readback;
    attach_pending_records();
    record.key = channel;
    record.marker_aux = what;
    std::memcpy(galaxy::render::reserve_record_probes(record, std::min(count, NEBULA_SCREEN_PROBES_MAX)), probes,
                std::min(count, NEBULA_SCREEN_PROBES_MAX) * sizeof(NebulaScreenProbe));
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)channel, (void)what, (void)probes, (void)count;
#endif
}

void svc_marker(void*, uint32_t kind, uint32_t aux, uint64_t a, uint64_t b) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::Marker;
    attach_pending_records();
    record.key = kind;
    record.marker_aux = aux;
    record.marker = {a, b};
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)kind, (void)aux, (void)a, (void)b;
#endif
}

// The game's frame end: the renderer takes the frame's records.
void svc_frame_end(void*) {
#if defined(NEBULA_HOST_RENDERER)
    submit_frame(false, false);
#endif
}

uint32_t svc_screen_probe_results(void*, uint32_t channel, NebulaScreenProbe* probes, uint32_t capacity) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::screen_probe_results(channel, reinterpret_cast<galaxy::render::ScreenProbe*>(probes), capacity);
#else
    (void)channel, (void)probes, (void)capacity;
    return 0;
#endif
}

// A pass into a texture target (E-07): its start and end as records in draw order.
void svc_target_begin_over_frame(void*, uint64_t key, uint32_t width, uint32_t height, uint32_t clear_rgba, uint64_t frame_key,
                                 uint32_t frame_channels, uint64_t depth_key) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::TargetBegin;
    record.key = key;
    record.target_width = width;
    record.target_height = height;
    record.target_clear_rgba = clear_rgba;
    record.target_frame_key = frame_key;
    record.target_channels = static_cast<std::uint8_t>(frame_channels);
    record.target_depth_key = depth_key;
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key, (void)width, (void)height, (void)clear_rgba, (void)frame_key, (void)frame_channels, (void)depth_key;
#endif
}

void svc_target_begin(void*, uint64_t key, uint32_t width, uint32_t height, uint32_t clear_rgba) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::TargetBegin;
    record.key = key;
    record.target_width = width;
    record.target_height = height;
    record.target_clear_rgba = clear_rgba;
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key, (void)width, (void)height, (void)clear_rgba;
#endif
}

void svc_target_end(void*, uint64_t key, uint32_t channels) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::TargetEnd;
    record.key = key;
    record.target_channels = static_cast<std::uint8_t>(channels);
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key, (void)channels;
#endif
}

void svc_target_clear(void*, uint32_t clear_rgba, uint32_t channels) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::TargetClear;
    record.target_clear_rgba = clear_rgba;
    record.target_channels = static_cast<std::uint8_t>(channels & 7u);
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)clear_rgba, (void)channels;
#endif
}

void svc_frame_pass(void*, uint64_t key, uint32_t channels) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::FramePass;
    record.key = key;
    record.target_channels = static_cast<std::uint8_t>(channels);
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key, (void)channels;
#endif
}

void svc_frame_take(void*, uint64_t key) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::FrameTake;
    record.key = key;
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key;
#endif
}

// A clear of the frame's rectangle.
void svc_frame_clear(void*, uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t clear_rgba,
                     uint32_t channels) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::FrameClear;
    record.target_x = left;
    record.target_y = top;
    record.target_width = width;
    record.target_height = height;
    record.target_clear_rgba = clear_rgba;
    record.target_channels = static_cast<std::uint8_t>(channels & 7u);
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)left, (void)top, (void)width, (void)height, (void)clear_rgba, (void)channels;
#endif
}

// The frame shown (its display copy, host-made).
void svc_frame_show(void*) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::FrameShow;
    attach_pending_records();
    galaxy::render::attach_shape_record(std::move(record));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void attach_state_ops(const galaxy::render::state::Op* ops, uint32_t count) {
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::State;
    attach_pending_records();
    std::memcpy(galaxy::render::reserve_record_ops(record, count), ops, sizeof(galaxy::render::state::Op) * count);
    galaxy::render::attach_shape_record(std::move(record));
}
#endif

void svc_view_projection(void*, int orthographic, const float terms[6]) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op op{};
    op.kind = galaxy::render::state::kProjection;
    op.a[0] = orthographic != 0 ? 1u : 0u;
    std::memcpy(op.f, terms, sizeof(op.f));
    attach_state_ops(&op, 1u);
#else
    (void)orthographic, (void)terms;
#endif
}

void svc_view_viewport(void*, const float viewport[6]) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op op{};
    op.kind = galaxy::render::state::kViewport;
    std::memcpy(op.f, viewport, sizeof(op.f));
    attach_state_ops(&op, 1u);
#else
    (void)viewport;
#endif
}

void svc_view_scissor(void*, float left, float top, float width, float height) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op op{};
    op.kind = galaxy::render::state::kScissor;
    op.f[0] = left;
    op.f[1] = top;
    op.f[2] = width;
    op.f[3] = height;
    attach_state_ops(&op, 1u);
#else
    (void)left, (void)top, (void)width, (void)height;
#endif
}

void svc_light(void*, uint32_t index, const NebulaLight* light) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op ops[2]{};
    ops[0].kind = galaxy::render::state::kLightAttenuation;
    ops[0].a[0] = static_cast<std::uint8_t>(index & 7u);
    std::memcpy(ops[0].a + 1, light->color, 4u);
    ops[1].kind = galaxy::render::state::kLightPosition;
    ops[1].a[0] = static_cast<std::uint8_t>(index & 7u);
    for (unsigned i = 0u; i < 3u; ++i) {
        ops[0].f[i] = light->angle[i];
        ops[0].f[3u + i] = light->distance[i];
        ops[1].f[i] = light->position[i];
        ops[1].f[3u + i] = light->direction[i];
    }
    attach_state_ops(ops, 2u);
#else
    (void)index, (void)light;
#endif
}

void svc_frame_alpha(void*, int keep) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op op{};
    op.kind = galaxy::render::state::kFrameAlpha;
    op.a[0] = 1u;
    op.a[1] = keep != 0 ? 1u : 0u;
    attach_state_ops(&op, 1u);
#else
    (void)keep;
#endif
}

void svc_texture_coordinate(void*, uint32_t coord, uint32_t from, uint32_t matrix_slot) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::state::Op op{};
    op.kind = galaxy::render::state::kTexCoordGen;
    op.a[0] = static_cast<std::uint8_t>(coord);
    op.a[2] = static_cast<std::uint8_t>((5u + from) | 5u << 4u);  // source row 5 + n, no emboss source
    op.a[4] = 61u;                                                // the identity post-transform
    op.a[5] = 1u;
    op.a[6] = static_cast<std::uint8_t>(matrix_slot);
    attach_state_ops(&op, 1u);
#else
    (void)coord, (void)from, (void)matrix_slot;
#endif
}

void svc_native_material(void*, const NebulaNativeMaterial* native) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    static_assert(sizeof(NebulaNativeMaterial) == sizeof(galaxy::render::material::Native),
                  "the renderer's copy of NebulaNativeMaterial");
    if (native == nullptr || (native->shader >= NEBULA_SHADER_COUNT && native->shader < NEBULA_SHADER_J3D_FIRST)) {
        return;
    }
    // NEBULA_SHADER_NONE: the draws that follow go back to the drawing state (a record without a material).
    shapes::Record record;
    record.kind = shapes::Record::Kind::NativeMaterial;
    attach_pending_records();
    if (native->shader != NEBULA_SHADER_NONE) {
        record.native_index = galaxy::render::reserve_record_native(native, sizeof(NebulaNativeMaterial));
    }
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)native;
#endif
}

// The wheel's turn the window gathered since the game last asked.
int32_t svc_pointer_wheel(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::take_pointer_wheel();
#else
    return 0;
#endif
}

uint32_t svc_art_export(void*, const NebulaArtExport*) {
    return 0u;
}
std::atomic<uint32_t> gTiltSection{0};
std::atomic<bool> gMotor{false};

void svc_game_context(void*, uint32_t what, int32_t value) {
    char text[64];
    if (what == NEBULA_CONTEXT_STAR_POINTER_MODE) {
        if (gStarPointerMode.exchange(value, std::memory_order_relaxed) != value) {
            std::snprintf(text, sizeof(text), "star pointer mode %d", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_TILT_SECTION) {
        if (gTiltSection.exchange(static_cast<uint32_t>(value), std::memory_order_relaxed) != static_cast<uint32_t>(value)) {
            std::snprintf(text, sizeof(text), "tilt section %d", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_FRAME_GENERATION) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_frame_generation_allowed(value != 0);
#endif
    } else if (what == NEBULA_CONTEXT_CAMERA_WATER) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_camera_water(value > 0 ? static_cast<float>(value - 1) : -1.0f);
#endif
    }
}

void svc_memory_rewritten(void*, const void* address, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    forget_handed(reinterpret_cast<uint64_t>(address), size);
#else
    (void)address;
    (void)size;
#endif
}

const galaxy::disc::AssetStore& disc_table() {
    return gPak.store;
}

int disc_entry_out(const std::optional<galaxy::disc::Entry>& entry, NebulaAssetEntry* out) {
    if (!entry) {
        return 0;
    }
    *out = {};
    out->index = entry->index;
    out->length = static_cast<uint32_t>(entry->length);
    out->position = entry->position;
    out->children = entry->children;
    out->folder = entry->folder ? 1 : 0;
    out->name = entry->name;
    return 1;
}

int svc_asset_find(void*, const char* path, NebulaAssetEntry* out) {
    return path != nullptr ? disc_entry_out(disc_table().find(path), out) : 0;
}

int svc_asset_entry(void*, int32_t index, NebulaAssetEntry* out) {
    return disc_entry_out(disc_table().entry(index), out);
}

int svc_asset_child(void*, int32_t folder, uint32_t child, NebulaAssetEntry* out) {
    return disc_entry_out(disc_table().child(folder, child), out);
}

// The game's memory. GALAXY_TEST_MEMORY_AT places the first arena (the next follow), far from the file views and across a 4 GB
// line, so an address squeezed through 32 bits fails in every run.
void* svc_memory_reserve(void*, uint64_t size) {
#if defined(_WIN32)
    static uintptr_t at = [] {
        const char* value = ::galaxy::run_options().test_memory_at;
        return value != nullptr ? static_cast<uintptr_t>(std::strtoull(value, nullptr, 16)) : uintptr_t{0};
    }();
    void* const block = VirtualAlloc(reinterpret_cast<void*>(at), static_cast<SIZE_T>(size), MEM_RESERVE, PAGE_NOACCESS);
    if (at != 0u) {
        at += static_cast<uintptr_t>(size);
    }
    return block;
#else
    void* const block = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return block != MAP_FAILED ? block : nullptr;
#endif
}

int svc_memory_commit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    return VirtualAlloc(start, static_cast<SIZE_T>(size), MEM_COMMIT, PAGE_READWRITE) != nullptr ? 1 : 0;
#else
    (void)start, (void)size;
    return 1;
#endif
}

void svc_memory_decommit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    VirtualFree(start, static_cast<SIZE_T>(size), MEM_DECOMMIT);
#else
    madvise(start, static_cast<size_t>(size), MADV_DONTNEED);
#endif
}

void svc_memory_release(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    (void)size;
    VirtualFree(start, 0, MEM_RELEASE);
#else
    munmap(start, static_cast<size_t>(size));
#endif
}

}  // namespace

// The game's threads: each sleeps on its own turn; only the one whose turn it is runs.
struct NebulaHostThread {
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    bool turn = false;
    bool cancelled = false;
    std::mutex lock;
    std::condition_variable woken;
};

namespace {

#if defined(_WIN32)
// Where each thread last went to sleep, for the dump: a hang shows who waits for what.
constexpr int kWaitThreads = 32;
constexpr int kWaitFrames = 10;
struct WaitRecord {
    DWORD id;
    void* frames[kWaitFrames];
    USHORT count;
};
WaitRecord gWaits[kWaitThreads];
std::atomic<int> gWaitCount{0};
thread_local int tWaitSlot = -1;

void note_wait() {
    if (tWaitSlot < 0) {
        const int slot = gWaitCount.fetch_add(1, std::memory_order_relaxed);
        if (slot >= kWaitThreads) {
            return;
        }
        tWaitSlot = slot;
        gWaits[slot].id = GetCurrentThreadId();
    }
    gWaits[tWaitSlot].count = RtlCaptureStackBackTrace(1, kWaitFrames, gWaits[tWaitSlot].frames, nullptr);
}
#else
void note_wait() {}
#endif

void svc_threads_dump(void*, const void* in_module) {
#if defined(_WIN32)
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<const char*>(in_module), &module);
    for (int i = 0; i < gWaitCount.load(std::memory_order_relaxed) && i < kWaitThreads; i++) {
        char text[256];
        int used = std::snprintf(text, sizeof(text), "thread %lu sleeps at", static_cast<unsigned long>(gWaits[i].id));
        for (USHORT k = 0; k < gWaits[i].count && used < 230; k++) {
            used += std::snprintf(text + used, sizeof(text) - used, " +%llx",
                                  static_cast<unsigned long long>(static_cast<const char*>(gWaits[i].frames[k]) - reinterpret_cast<const char*>(module)));
        }
        log_line("threads:", text);
    }
#else
    (void)in_module;
#endif
}

void svc_thread_pass(void*, NebulaHostThread* to) {
    {
        const std::lock_guard<std::mutex> guard(to->lock);
        to->turn = true;
    }
    to->woken.notify_one();
}

void svc_thread_exit(void*, NebulaHostThread* self) {
    // Game threads end only with the game.
    std::unique_lock<std::mutex> guard(self->lock);
    self->woken.wait(guard, [] { return false; });
}

void svc_thread_wait(void* user, NebulaHostThread* self) {
    note_wait();
    {
        std::unique_lock<std::mutex> guard(self->lock);
        self->woken.wait(guard, [self] { return self->turn; });
        self->turn = false;
    }
    if (self->cancelled) {
        svc_thread_exit(user, self);
    }
}

void svc_thread_cancel(void* user, NebulaHostThread* thread) {
    thread->cancelled = true;
    svc_thread_pass(user, thread);
}

NebulaHostThread* svc_thread_adopt(void*) {
    return new NebulaHostThread();
}

NebulaHostThread* svc_thread_create(void* user, void (*entry)(void*), void* arg) {
    auto* const thread = new NebulaHostThread();
    thread->entry = entry;
    thread->arg = arg;
    std::thread([user, thread] {
        name_thread(L"Nebula Game Thread");
        svc_thread_wait(user, thread);
        thread->entry(thread->arg);
    }).detach();
    return thread;
}

std::mutex gGameLocks[2];

void svc_lock(void*, int which) {
    gGameLocks[which].lock();
}

void svc_unlock(void*, int which) {
    gGameLocks[which].unlock();
}

// GALAXY_TEST_DISC_DELAY_US: a slow drive (each way to the store takes this long), so waits on loads are exercised.
void test_drive_delay() {
    static const uint32_t delay_us = [] {
        const char* value = ::galaxy::run_options().test_disc_delay_us;
        return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0u;
    }();
    if (delay_us != 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
}

const void* svc_asset_map(void*, int32_t file) {
    test_drive_delay();
    const std::optional<galaxy::disc::Entry> entry = disc_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    return gPak.store.map(entry->position, static_cast<std::size_t>(entry->length));
}

void* svc_asset_map_private(void*, int32_t file) {
    test_drive_delay();
    const std::optional<galaxy::disc::Entry> entry = disc_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    void* const data = gPak.store.map_private(entry->position, static_cast<std::size_t>(entry->length));
    static const bool trace_reads = ::galaxy::run_options().trace_file_reads != nullptr;
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d mapped size %llu %s at %p\n", static_cast<unsigned long long>(gDeterministicFrames.load()), file,
                     static_cast<unsigned long long>(entry->length), entry->name != nullptr ? entry->name : "?", data);
    }
    return data;
}

void svc_asset_unmap(void*, void* data, uint64_t size) {
    static const bool trace_reads = ::galaxy::run_options().trace_file_reads != nullptr;
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu unmapped %p size %llu\n", static_cast<unsigned long long>(gDeterministicFrames.load()), data, static_cast<unsigned long long>(size));
    }
    gPak.store.unmap_private(data, static_cast<std::size_t>(size));
}

int32_t svc_asset_read(void*, int32_t file, uint64_t file_offset, uint32_t size, void* dst) {
    test_drive_delay();
    gFileReads.fetch_add(1, std::memory_order_relaxed);
    // GALAXY_TRACE_FILE_READS=1: each read with the frame it came on: two runs of deterministic mode compare by it.
    static const bool trace_reads = ::galaxy::run_options().trace_file_reads != nullptr;
    const std::optional<galaxy::disc::Entry> entry = disc_table().entry(file);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d at %llu size %u %s\n", static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)),
                     file, static_cast<unsigned long long>(file_offset), size, entry && entry->name != nullptr ? entry->name : "?");
    }
    if (!entry || file_offset > entry->length || entry->length - file_offset < size ||
        !gPak.store.read(entry->position + file_offset, size, dst)) {
        return -1;
    }
#if defined(NEBULA_HOST_RENDERER)
    // A read writes memory the GPU sees.
    mark_written(dst, size);
#endif
    return static_cast<int32_t>(size);
}

const char* svc_game_id(void*) {
    return gPak.id;
}

std::atomic<uint32_t> gGameFrames{0};
std::atomic<uint64_t> gSceneFrames{0};  // svc_test_scene_frame's count (GALAXY_TEST_SCRIPT_CLOCK=frames)
void svc_frame_begun(void*, int black, uint32_t frame) {
    gGameFrames.store(frame, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    // GALAXY_TRACE_FRAME_TIMES=1: each frame's time, where a scripted press begins (press-to-present measurements).
    static const bool trace_vi = [] {
        const char* value = ::galaxy::run_options().trace_frame_times;
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    if (trace_vi) std::fprintf(stderr, "[latency] retrace vi=%u qpc=%lld\n", frame, static_cast<long long>(galaxy::qpc_now()));
#endif
#if defined(NEBULA_HOST_RENDERER)
#if defined(_WIN32)
    galaxy::timeline::record(galaxy::timeline::Event::GameFrame, frame, 0u);
#endif
    if (frame >= kBootFrames) {
        end_boot_fast("start's frames run out");
    }
    {
        // While the start runs ahead, one frame a display period goes to the screen.
        bool present = true;
        if (gBootFast.load(std::memory_order_relaxed) && !deterministic_mode()) {
            static Clock::time_point shown{};
            const Clock::time_point now = Clock::now();
            present = now - shown >= std::chrono::milliseconds(16);
            if (present) shown = now;
        }
        // The renderer's newest picture, or black while the game shows black.
        submit_frame(present, black != 0);
    }
    // Miis imported from the settings while the game runs: the game reads its slots again.
    if (const auto placed = galaxy::host::take_mii_installs(); !placed.empty()) {
        gModule.mii_changed();
        std::fprintf(stderr, "[mii] %zu imported while running\n", placed.size());
    }
    // The output's shape (the window's, or the chosen aspect) is latched once this frame's picture is presented:
    // queued frames finish first, so a picture is never fitted with the next frame's camera and layouts.
    {
        const auto client = galaxy::render::aspect_latch_extent();
        if (client.width > 1 && client.height > 1 &&
            galaxy::experimental_dynamic_aspect_change_pending(client.width, client.height)) {
            galaxy::render::wait_for_render_idle();
            const auto settled = galaxy::render::aspect_latch_extent();
            if (settled.width > 1 && settled.height > 1) {
                galaxy::experimental_latch_dynamic_aspect(settled.width, settled.height);
            }
        }
    }
#else
    (void)black;
#endif
}

// The shape the game draws for (display_native.c turns it into the game's screen width and camera aspect): the
// latched output shape; headless, 16:9.
uint32_t svc_display_extent(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return static_cast<uint32_t>(galaxy::g_experimental_dynamic_aspect_word.load(std::memory_order_acquire));
#else
    return 0;
#endif
}

// The game's lights (NEBULA_LIGHT_*) into the renderer's light registry (game_lights.cpp), which the ray tracer reads.
void svc_game_light(void*, uint32_t kind, uint64_t id, const float* v) {
#if defined(NEBULA_HOST_RENDERER)
    switch (kind) {
    case NEBULA_LIGHT_SUN:
        galaxy::render::set_game_sun({v[0], v[1], v[2]}, v[3]);
        break;
    case NEBULA_LIGHT_STAR_CORE:
        galaxy::render::set_star_core({v[0], v[1], v[2]}, v[3]);
        break;
    case NEBULA_LIGHT_POINT:
        galaxy::render::note_game_point_light(id, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6]);
        break;
    case NEBULA_LIGHT_POINT_REQUEST:
        galaxy::render::note_game_point_request(id, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6]);
        break;
    case NEBULA_LIGHT_WORLD:
        galaxy::render::note_game_world_light({v[0], v[1], v[2]}, {v[3], v[4], v[5]});
        break;
    case NEBULA_LIGHT_AREA:
    case NEBULA_LIGHT_PLAYER: {
        galaxy::render::GameAreaLight light{};
        light.ambient = {v[0], v[1], v[2]};
        for (unsigned i = 0; i < 2u; ++i) {
            const float* l = v + 3 + 7 * i;
            light.lights[i].color = {l[0], l[1], l[2]};
            light.lights[i].position = {l[3], l[4], l[5]};
            light.lights[i].follows_camera = l[6] != 0.0f;
        }
        galaxy::render::set_game_area_light(kind == NEBULA_LIGHT_AREA ? galaxy::render::GameAreaLightKind::Stage
                                                                  : galaxy::render::GameAreaLightKind::Player,
                                        light);
        break;
    }
    default:
        break;
    }
#else
    (void)kind;
    (void)id;
    (void)v;
#endif
}

uint32_t svc_option(void*, uint32_t what) {
    switch (what) {
    case NEBULA_OPTION_TEXT_SPEED:
#if defined(NEBULA_HOST_RENDERER)
        return std::clamp(galaxy::get_runtime_settings().text_speed, 1u, 4u);
#else
        return 1;
#endif
    case NEBULA_OPTION_DRAW_IDENTITIES:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::draw_identity_wanted() ? 1u : 0u;
#else
        return 0;
#endif
    case NEBULA_OPTION_INPUT_MODE:
#if defined(NEBULA_HOST_RENDERER)
        switch (galaxy::get_runtime_input_mode_state().mode) {
        case galaxy::RuntimeInputMode::Controller: return NEBULA_INPUT_MODE_CONTROLLER;
        case galaxy::RuntimeInputMode::RealWiimote: return NEBULA_INPUT_MODE_WII_REMOTE;
        case galaxy::RuntimeInputMode::Auto: return NEBULA_INPUT_MODE_AUTO;
        case galaxy::RuntimeInputMode::KeyboardMouse: break;
        }
#endif
        return NEBULA_INPUT_MODE_KEYBOARD_MOUSE;
    case NEBULA_OPTION_BLOOM:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::bloom_route();
#else
        return 0;
#endif
    case NEBULA_OPTION_STAR_COUNTER:
        return coop_star_counter_on() ? 1u : 0u;
    case NEBULA_OPTION_RT_PREPARATION:
    case NEBULA_OPTION_RT_PREPARE_PROGRESS:
        // The ray tracer is parked (no shaders to prepare): the game's preparation screen never shows.
        return 0;
    case NEBULA_OPTION_TEST_ARENA_FILL: {
        const char* value = ::galaxy::run_options().test_arena_fill;
        return value != nullptr ? 0x100u | (static_cast<uint32_t>(std::strtoul(value, nullptr, 16)) & 0xFFu) : 0u;
    }
    case NEBULA_OPTION_TEST_TRACE_LOADS:
        return ::galaxy::run_options().trace_loads != nullptr ? 1u : 0u;
    default:
        return 0;
    }
}

static_assert(int{galaxy::saves::kDone} == NEBULA_SAVE_DONE && int{galaxy::saves::kNoFile} == NEBULA_SAVE_NO_FILE &&
                  int{galaxy::saves::kTooLarge} == NEBULA_SAVE_TOO_LARGE &&
                  int{galaxy::saves::kFailed} == NEBULA_SAVE_FAILED,
              "save results are the game's");

// A save file operation on the save worker (the game polls the status): galaxy/save_files.h in the game's save folder
// (gSaveDataPath under the save folder, or a mod's save folder).
std::atomic<int> gSavesPending{0};

void svc_save_request(void*, const NebulaSaveRequest* request) {
    gSaveOps.fetch_add(1, std::memory_order_relaxed);
    *request->status = NEBULA_SAVE_PENDING;
    gSavesPending.fetch_add(1, std::memory_order_acq_rel);
    const NebulaSaveRequest r = *request;
    const std::string name = r.name != nullptr ? r.name : "";
    auto operation = [r, name] {
        fs::path folder;
        galaxy::saves::Result result = galaxy::saves::kFailed;
        if (nand_path(gSaveDataPath.c_str(), &folder)) {
            switch (r.op) {
            case NEBULA_SAVE_READ:
                result = galaxy::saves::read(folder, name, r.buffer, r.size, r.length);
                break;
            case NEBULA_SAVE_WRITE:
                result = galaxy::saves::write(folder, name, r.buffer, r.size);
                break;
            case NEBULA_SAVE_DELETE:
                result = galaxy::saves::remove(folder, name);
                break;
            }
        }
#if defined(__cpp_lib_atomic_ref)
        std::atomic_ref<int32_t>(*const_cast<int32_t*>(r.status)).store(result, std::memory_order_release);
#else  // libc++ without atomic_ref (Android NDK r28): the compiler's builtin, the same release store.
        __atomic_store_n(const_cast<int32_t*>(r.status), result, __ATOMIC_RELEASE);
#endif
        gSavesPending.fetch_sub(1, std::memory_order_acq_rel);
    };
    // Deterministic test mode: done before the call returns, as disc reads are, so the game's poll sees the same
    // status on the same frame every run.
    if (deterministic_mode()) {
        operation();
    } else {
        gFs.post(operation);
    }
}

// The settings the game asks the system for. The pointer mapping (native_input's virtual sensor bar) assumes the bar above
// the screen.
uint32_t svc_system_setting(void*, uint32_t item) {
    switch (item) {
    case NEBULA_SETTING_LANGUAGE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::game_language_for_boot());  // the language setting
#else
        return 1;  // English
#endif
    case NEBULA_SETTING_SOUND_MODE:
        return 1;  // stereo
    case NEBULA_SETTING_WIDESCREEN:
        return 1;
    default:
        return 0;
    }
}

// Player 1: Nebula's controls (the bindings, keyboard and mouse, controllers; a real Wii Remote while it is in use)
// as a remote with a Nunchuk, connected from the start. The host takes a sample every 10 ms and one each time the
// game reads (svc_input_read), when it starts its 10 ms anew; the game gets every sample since its last read.
// Headless, the remote lies still: one sample per read, nothing pressed.
constexpr uint32_t kInputSamples = 120;  // a read takes at most the newest 1.2 s

NebulaInputSample rest_input_sample() {
    NebulaInputSample sample{};
    sample.roll[0] = 1.0f;
    sample.distance = (0.2f / 0.383864f) / 0.3125f;
    sample.gravity[2] = 1.0f;
    sample.nunchuk_gravity[2] = 1.0f;
    return sample;
}

#if defined(NEBULA_HOST_RENDERER)
static_assert(NEBULA_BUTTON_A == galaxy::input::kButtonA && NEBULA_BUTTON_B == galaxy::input::kButtonB &&
              NEBULA_BUTTON_1 == galaxy::input::kButton1 && NEBULA_BUTTON_2 == galaxy::input::kButton2 &&
              NEBULA_BUTTON_PLUS == galaxy::input::kButtonPlus && NEBULA_BUTTON_MINUS == galaxy::input::kButtonMinus &&
              NEBULA_BUTTON_HOME == galaxy::input::kButtonHome && NEBULA_BUTTON_UP == galaxy::input::kButtonUp &&
              NEBULA_BUTTON_DOWN == galaxy::input::kButtonDown && NEBULA_BUTTON_LEFT == galaxy::input::kButtonLeft &&
              NEBULA_BUTTON_RIGHT == galaxy::input::kButtonRight && NEBULA_BUTTON_C == galaxy::input::kButtonC &&
              NEBULA_BUTTON_Z == galaxy::input::kButtonZ,
              "the host's buttons are the game's");

NebulaInputSample input_sample(const galaxy::input::PlayerInput& input) {
    NebulaInputSample sample{};
    sample.buttons = galaxy::input::button_bits(input.buttons);
    sample.stick[0] = input.stick_x;
    sample.stick[1] = input.stick_y;
    sample.pointer_on = input.pointer_on ? 1u : 0u;
    sample.pointer[0] = input.pointer_x;
    sample.pointer[1] = input.pointer_y;
    sample.roll[0] = input.roll_x;
    sample.roll[1] = input.roll_y;
    sample.distance = input.distance;
    for (int i = 0; i < 3; ++i) {
        sample.gravity[i] = input.gravity[i];
        sample.nunchuk_gravity[i] = input.nunchuk_gravity[i];
    }
    sample.shake = static_cast<uint8_t>((input.shake ? NEBULA_SHAKE_REMOTE : 0u) |
                                        (input.nunchuk_shake ? NEBULA_SHAKE_NUNCHUK : 0u));
    return sample;
}

// Player 1's samples not yet read, oldest first; past kInputSamples the oldest go.
class InputSamples {
public:
    void push(const NebulaInputSample& sample) {
        const std::lock_guard<std::mutex> lock(lock_);
        ring_[(head_ + count_) % kInputSamples] = sample;
        if (count_ < kInputSamples) {
            ++count_;
        } else {
            head_ = (head_ + 1) % kInputSamples;
        }
    }
    uint32_t take(NebulaInputSample* out, uint32_t max) {
        const std::lock_guard<std::mutex> lock(lock_);
        const uint32_t count = std::min(count_, max);
        const uint32_t skip = count_ - count;
        for (uint32_t i = 0; i < count; ++i) {
            out[i] = ring_[(head_ + skip + i) % kInputSamples];
        }
        head_ = (head_ + count_) % kInputSamples;
        count_ = 0;
        return count;
    }

private:
    std::mutex lock_;
    std::array<NebulaInputSample, kInputSamples> ring_{};
    uint32_t head_ = 0;
    uint32_t count_ = 0;
};

InputSamples gInputSamples;
// One sample is taken at a time: on the input thread every 10 ms and on the game's thread when it reads.
std::mutex gInputSampleLock;
std::atomic<int64_t> gInputReadNs{std::numeric_limits<int64_t>::min() / 2};  // the game's last read, steady clock
std::atomic<bool> gInputRead{false};  // the game has read its input (the 10 ms samples start then)
std::atomic<bool> gControlsInput{true};  // player 1 is the controls (not a real remote)
std::atomic<uint8_t> gInputNunchuk{1};
std::atomic<uint8_t> gInputBattery{4};
std::atomic<uint8_t> gInputConnected{1};

// The chosen input mode has nothing to play with: the game shows its message (GameSystemErrorWatcher) until that changes.
// Looked at ten times a second.
bool input_device_missing(bool wii_remote) {
    static unsigned tick = 0;
    static bool missing = false;
    if (++tick % 10u != 0u) {
        return missing;
    }
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    const bool controller = galaxy::host::connected_controllers(connected) != 0u;
    missing = galaxy::input_missing(galaxy::get_runtime_input_mode_state().mode, controller, wii_remote,
                                    galaxy::host::keyboard_or_mouse_attached());
    return missing;
}

// The controls now (under gInputSampleLock). Each sample renews the motor's state (a stale one stops).
NebulaInputSample controls_sample() {
    static const bool frame_clock = [] {
        const char* value = ::galaxy::run_options().test_script_clock;
        return value != nullptr && std::strcmp(value, "frames") == 0;
    }();
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    galaxy::host::native_game_rumble(gMotor.load(std::memory_order_relaxed), mode);
    return input_sample(galaxy::host::native_game_input(
        frame_clock ? gSceneFrames.load(std::memory_order_relaxed) : gGameFrames.load(std::memory_order_relaxed), mode,
        static_cast<uint8_t>(gTiltSection.load(std::memory_order_relaxed))));
}
#endif

uint32_t svc_input_read(void*, uint32_t player, NebulaInputDevice* device, NebulaInputSample* samples, uint32_t max) {
    if (device != nullptr) {
        *device = {};
#if defined(NEBULA_HOST_RENDERER)
        // Player 2, when someone plays it (co-star mode): the pointer and its buttons, no Nunchuk.
        if (player == 1) {
            galaxy::input::PlayerInput co_star{};
            if (galaxy::host::native_game_co_star(&co_star)) {
                device->connected = 1;
                device->battery = 4;
                if (samples != nullptr && max != 0) {
                    samples[0] = input_sample(co_star);
                    return 1;
                }
            }
            return 0;
        }
#endif
        if (player == 0) {
            device->connected = 1;
#if defined(NEBULA_HOST_RENDERER)
            device->connected = gInputConnected.load(std::memory_order_relaxed);
            device->nunchuk = gInputNunchuk.load(std::memory_order_relaxed);
            device->battery = gInputBattery.load(std::memory_order_relaxed);
#else
            device->nunchuk = 1;
            device->battery = 4;
#endif
        }
    }
    if (player != 0 || samples == nullptr || max == 0) {
        return 0;
    }
#if defined(NEBULA_HOST_RENDERER)
    gInputRead.store(true, std::memory_order_release);
    if (gControlsInput.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        gInputSamples.push(controls_sample());
        gInputReadNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(),
                           std::memory_order_release);
    }
    return gInputSamples.take(samples, max);
#else
    samples[0] = rest_input_sample();
    return 1;
#endif
}

void svc_input_control(void*, uint32_t player, uint32_t what, uint32_t value) {
    if (player != 0 || what != NEBULA_INPUT_RUMBLE) {
        return;  // player 1's controller is never let go; there are no others
    }
    gMotor.store(value != 0, std::memory_order_relaxed);
#if defined(NEBULA_REAL_WIIMOTE)
    if (!galaxy::host::real_wiimote_in_use()) {
        galaxy::host::native_game_rumble(value != 0, gStarPointerMode.load(std::memory_order_relaxed));
    }
    galaxy::host::real_wiimote_set_rumble(value != 0);
#elif defined(NEBULA_HOST_RENDERER)
    galaxy::host::native_game_rumble(value != 0, gStarPointerMode.load(std::memory_order_relaxed));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void input_thread() {
    if (deterministic_mode()) {
        return;  // one sample per read, taken by the read (svc_input_read)
    }
    name_thread(L"Nebula Host Input");
    while (!gInputRead.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto period = std::chrono::milliseconds(10);
    auto next = Clock::now() + period;
#if defined(NEBULA_REAL_WIIMOTE)
    galaxy::host::ensure_real_wiimote_started();
    // The remote's data reports, IR camera on: with its Nunchuk, buttons, accelerometer, IR and extension; without.
    galaxy::host::real_wiimote_set_report(0x37);
    galaxy::host::real_wiimote_set_ir(true);
    // A real Wii Remote: `remote` while one is connected (its calibration), `real` while the game plays with it.
    std::optional<galaxy::host::RealWiimoteConnection> remote;
    galaxy::input::wiimote::Reader reader;
    bool real = false;
    std::optional<galaxy::input::DeviceKey> device_when_real;
    const auto use_remote = [&](bool on) {
        if (on == real || (on && !remote.has_value())) return;
        real = on;
        if (on) {
            galaxy::host::set_input_missing_shown(false);
            gInputConnected.store(1u, std::memory_order_relaxed);
        }
        galaxy::host::real_wiimote_set_active(on);
        gControlsInput.store(!on, std::memory_order_release);
        gInputNunchuk.store(on ? (remote->nunchuk ? 1u : 0u) : 1u, std::memory_order_relaxed);
        gInputBattery.store(on ? remote->battery : 4u, std::memory_order_relaxed);
        device_when_real = galaxy::host::last_input_device();
        next = Clock::now() + period;
        log_line("[wiimote]", on ? "in use" : "keyboard, mouse and controllers in use");
    };
#else
    [[maybe_unused]] constexpr bool real = false;  // no real Wii Remote on this host
#endif
    for (;;) {
#if defined(NEBULA_REAL_WIIMOTE)
        // A real Wii Remote's reports become samples as they arrive: in the Wii Remote input mode while it is
        // connected, in Auto from its first button press until a key, a mouse button or a controller is used. The
        // keyboard, mouse and controllers are read every 10 ms meanwhile (for that, and the settings menu).
        std::array<uint8_t, 22> real_report{};
        std::size_t real_size = 0;
        galaxy::host::RealWiimoteConnection real_connection{};
        const auto until_tick =
            std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
        const galaxy::host::RealWiimoteEvent event = galaxy::host::real_wiimote_wait(
            real ? static_cast<uint32_t>(std::clamp<long long>(until_tick, 0, 10)) : 0u, real_report, real_size,
            real_connection);
        const bool wii_remote_mode =
            galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::RealWiimote;
        if (event == galaxy::host::RealWiimoteEvent::Connected) {
            remote = real_connection;
            reader.connect(real_connection.eeprom, real_connection.extension_calibration, real_connection.nunchuk);
            galaxy::host::real_wiimote_set_report(real_connection.nunchuk ? 0x37 : 0x33);
            if (real) {
                gInputNunchuk.store(real_connection.nunchuk ? 1u : 0u, std::memory_order_relaxed);
                gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            } else if (wii_remote_mode) {
                use_remote(true);
            }
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Disconnected) {
            use_remote(false);
            remote.reset();
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Battery) {
            if (remote.has_value()) remote->battery = real_connection.battery;
            if (real) gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Report) {
            const std::span<const uint8_t> bytes(real_report.data(), real_size);
            if (!real && (wii_remote_mode || galaxy::host::real_wiimote_buttons_held(bytes))) use_remote(true);
            galaxy::input::PlayerInput input{};
            if (real && reader.read(bytes, input)) gInputSamples.push(input_sample(input));
            continue;
        }
        if (real && Clock::now() < next) {
            continue;
        }
#endif
        sleep_until_precise(next);
        // The game read within the last 10 ms: the next sample 10 ms after its.
        const Clock::time_point read{std::chrono::nanoseconds(gInputReadNs.load(std::memory_order_acquire))};
        if (!real && read + period > next) {
            next = read + period;
            continue;
        }
        next += period;
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        const NebulaInputSample sample = controls_sample();
#if defined(NEBULA_REAL_WIIMOTE)
        if (real) {
            // Another device used (in Auto): back to the keyboard, mouse and controllers, with this sample.
            if (galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::RealWiimote ||
                galaxy::host::last_input_device() == device_when_real) {
                continue;
            }
            use_remote(false);
        }
        const bool wii_remote = remote.has_value();
#else
        constexpr bool wii_remote = false;
#endif
        const bool missing = input_device_missing(wii_remote);
        galaxy::host::set_input_missing_shown(missing);
        gInputConnected.store(missing ? 0u : 1u, std::memory_order_relaxed);
        gInputSamples.push(sample);
    }
}
#endif

#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/wiimote_speaker.h"
#endif

// The remote speaker's sound, player 1's (6 kHz blocks): the audio output mixes it at the speaker gain (I-06).
void svc_speaker_samples(void*, uint32_t player, const int16_t* samples, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    if (player == 0u && samples != nullptr) {
        galaxy::input::speaker::game_speaker().push(std::span<const std::int16_t>(samples, count), 6000u);
    }
#else
    (void)player, (void)samples, (void)count;
#endif
}

// The game's sound (native_audio.h), on its audio thread. Headless, nothing is mixed.
void svc_audio_setup(void*, NebulaVoice* voices, uint32_t count, const NebulaEffectLine* effects) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->setup(voices, count, effects, !deterministic_mode());
#else
    (void)voices, (void)count, (void)effects;
#endif
}

void svc_audio_mix(void*, float gain) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->mix(gain);
#else
    (void)gain;
#endif
    gSoundSubframes.fetch_add(1, std::memory_order_relaxed);
}

void svc_audio_frame(void*) {
    static const bool trace_frames = ::galaxy::run_options().trace_file_reads != nullptr;
    if (trace_frames) {
        std::fprintf(stderr, "[read] sound frame %llu at frame %llu\n", static_cast<unsigned long long>(gSoundBlocks.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)));
    }
#if defined(NEBULA_HOST_RENDERER)
    gAudio->frame();
#endif
}

// The movie's sound: headless, every frame is taken and dropped.
int svc_movie_audio(void*, const void* record, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    return gAudio->movie_audio(record, size);
#else
    (void)record, (void)size;
    return 1;
#endif
}

double svc_movie_audio_position(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return gAudio->movie_audio_position();
#else
    return 0.0;
#endif
}

void svc_movie_audio_control(void*, uint32_t what, int32_t value, int32_t milliseconds) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->movie_audio_control(what, value, milliseconds);
#else
    (void)what, (void)value, (void)milliseconds;
#endif
}

void svc_audio_control(void*, uint32_t what, uint32_t value) {
    if (::galaxy::run_options().trace_file_reads != nullptr) {
        std::fprintf(stderr, "[read] sound clock %u at frame %llu\n", what, static_cast<unsigned long long>(gDeterministicFrames.load()));
    }
    {
        std::lock_guard<std::mutex> lock(gAiLock);
        if (what == NEBULA_AUDIO_START) {
            gAiRunning = true;
            gAudioFrameSamples = value;
        } else if (what == NEBULA_AUDIO_STOP) {
            gAiRunning = false;
        }
        gAiWake.notify_one();
    }
#if defined(NEBULA_HOST_RENDERER)
    if (what == NEBULA_AUDIO_START) {
        gAudio->start(audio_frame_tick);
    } else if (what == NEBULA_AUDIO_STOP) {
        gAudio->stop();
    }
#endif
}

int svc_hbm(void*, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    switch (what) {
    case NEBULA_HBM_OPEN:
        galaxy::render::home_menu_open();
        return 0;
    case NEBULA_HBM_POLL: {
        // GALAXY_TEST_HOME_ROW=<row>:<polls>: picks a row by the keys a player would press, and answers yes when it
        // asks (reset, leave).
        static const char* test = ::galaxy::run_options().test_home_row;
        static unsigned polls = 0;
        if (test != nullptr && galaxy::render::home_menu_visible()) {
            unsigned row = 0, after = 0;
            if (std::sscanf(test, "%u:%u", &row, &after) == 2 && ++polls == after) {
                for (unsigned i = 0; i < row; ++i) {
                    galaxy::render::native_settings_post_key(VK_DOWN);
                }
                galaxy::render::native_settings_post_key(VK_RETURN);
                if (row != 0) {
                    galaxy::render::native_settings_post_key(VK_RIGHT);
                    galaxy::render::native_settings_post_key(VK_RETURN);
                }
            }
        }
        return galaxy::render::home_menu_poll();
    }
    case NEBULA_HBM_CLOSE:
        galaxy::render::home_menu_close();
        return 0;
    case NEBULA_HBM_VIEW:
        return galaxy::render::home_menu_view(value != 0u);
    case NEBULA_HBM_POINT:
        galaxy::render::home_menu_point(value);
        return 0;
    case NEBULA_HBM_CHOOSE:
        galaxy::render::home_menu_choose(value);
        return 0;
    default:
        return 0;
    }
#else
    (void)value;
    return what == NEBULA_HBM_VIEW ? -1 : 0;  // headless: the menu closes at once
#endif
}

// Test hooks for hidden runs; the game performs them with its own calls each scene frame.
//   GALAXY_TEST_GALAXY_TOUR=<file of "Stage scenario" lines>: each stop in turn, GALAXY_TEST_GALAXY_TOUR_FRAMES scene
//     frames apart (default 1200), through MR::requestChangeStageInGameMoving.
//   GALAXY_TEST_TELEPORT=x,y,z,frame[/x,y,z,frame...]: MR::setPlayerPos at those scene frames, held 30 frames (the
//     player settles there); counted from the latest tour stop when touring.
//   GALAXY_TEST_EVENTS=kind@frame[/kind@frame...]: once at that scene frame (from the latest stop when touring): kill (a
//     miss: MR::forceKillPlayerByAbyss), movieN (MR::startMovie(N), in a stage with a movie sequence), wait (nothing:
//     keeps a tour's last stop running until then), star / grandstar (the stop's star is collected: its result, the get
//     demo, the save and what follows, the ending after KoopaBattleVs3Galaxy).
//   GALAXY_TEST_GRAND_STARS=n: the save holds Grand Stars 1-n from the second scene frame, and the stage starts again
//     with them (the Observatory's domes restored, lit and furnished as the game shows them for that many).
//   GALAXY_TEST_EVENT_FLAGS=name[,name=value...]: the save's stored event flags on and event values set from the second
//     scene frame, before the Grand Stars (with GALAXY_TEST_GRAND_STARS>=3, LibraryOpenNewStarCount=0 opens the Library
//     and its first storybook chapter; PictureBookAlreadyRead=n: chapters 1-n read, which the Library's book lists;
//     SpinAndStarPointer: the spin and pointer the Gateway's Luma grants; Achievement:<id>: that achievement's pop-up; AchievementBoard: the
//     Observatory's achievement board).
//   GALAXY_TEST_EVENTS actors@frame: the live actors with a model, by model name and position (where to teleport),
//     with the animation they play, and whether they are shown, clipped, or drawn while clipped as shadow casters.
//   GALAXY_TEST_EVENTS shake@frame: the screen shakes from that frame until the scene ends.
//   GALAXY_TEST_EVENTS defeat:<model>@frame: the live actors with that model (as actors@ names it) are killed, as their
//     defeat ends: what they leave appears (KuriboChief, the Gateway's big Goomba: its key).
//   GALAXY_TEST_SCRIPT_CLOCK=frames: the scripted input (GALAXY_INPUT_*) counts scene frames instead of frames, so a
//     run's game frames take the same input whatever its renderer's speed (builds compared frame for frame).
std::atomic<bool> gTourDone{false};

int svc_test_scene_frame(void*, NebulaTestAction* action) {
#if defined(NEBULA_RELEASE)
    (void)action;
    return 0;
#else
#include "../../tools/test_driver/host_scene_frame.inc"
#endif
}

// A restart starts this executable again with the same arguments (the game's boot); the
// other power events close the game.
// Every way out of the game: pending saves written (bounded: a dead drive does not hold the game), counters flushed.
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
// Up to kMaxGames games per data folder, so a player can host and join on one computer to test co-op. The first plays
// as always; each further one is game 2, 3, ... with its own folder for the files it writes (galaxy/game_instance.h),
// so two never write the same saves or settings. A start past the last brings the first's window forward and ends.
// Hidden test runs have their own folders and may run side by side; GALAXY_TEST_ONE_GAME=1 tests this.
constexpr unsigned kMaxGames = 4u;
// The game a restart (finish_and_exit) starts again as, so a second game stays the second.
constexpr wchar_t kGameInstanceVariable[] = L"NEBULA_GAME_INSTANCE";
HANDLE gOneGame = nullptr;

// A second game's folder starts with copies of the first's settings, graphics settings and mods list, the window
// windowed (two borderless games on one screen would cover each other). A file it already has stays as it is.
void seed_game_instance(const fs::path& folder, const fs::path& settings, const fs::path& config) {
    std::error_code error;
    fs::create_directories(folder, error);
    const auto copy_once = [&](const fs::path& from, const fs::path& to) {
        std::error_code copy_error;
        if (from.empty() || fs::exists(to, copy_error) || !fs::is_regular_file(from, copy_error)) return false;
        return fs::copy_file(from, to, copy_error) && !copy_error;
    };
    if (!settings.empty()) {
        copy_once(settings / "runtime_settings.ini", folder / "runtime_settings.ini");
        copy_once(settings / "mods.ini", folder / "mods.ini");
    }
    if (copy_once(config, folder / "config.ini")) {
        std::string text;
        if (std::FILE* file = galaxy::open_path(folder / "config.ini", "rb")) {
            char buffer[4096];
            for (std::size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) != 0u;) text.append(buffer, read);
            std::fclose(file);
        }
        std::string out;
        for (std::size_t at = 0; at < text.size();) {
            std::size_t end = text.find('\n', at);
            end = end == std::string::npos ? text.size() : end + 1u;
            const std::string_view line(text.data() + at, end - at);
            out += line.starts_with("display_mode=") ? "display_mode=" + std::to_string(galaxy::render::kDisplayWindowed) + "\n"
                                                     : std::string(line);
            at = end;
        }
        (void)galaxy::io::write_file_atomically(folder / "config.ini", out);
    }
}

// True when this start should end (every game slot taken).
bool claim_game_instance(int argc, char** argv) {
    if (::galaxy::run_option_flag(::galaxy::run_options().hide_window, false) &&
        !::galaxy::run_option_flag(::galaxy::run_options().test_one_game, false)) {
        return false;
    }
    // The mods tool (--import-mod, --list-mods, --remove-mod) plays no game: it works on the first's mods.
    if (argc >= 2 && (std::strcmp(argv[1], "--import-mod") == 0 || std::strcmp(argv[1], "--list-mods") == 0 ||
                      std::strcmp(argv[1], "--remove-mod") == 0)) {
        return false;
    }
    const fs::path settings = galaxy::runtime_settings_folder();
    std::wstring base = L"Local\\NebulaGame-";
    for (const wchar_t c : settings.wstring()) {
        base += c == L'\\' || c == L':' ? L'_' : c;
    }
    // A restart asks for the game it was; otherwise the first free one.
    unsigned wanted = 0u;
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(kGameInstanceVariable, value, 8) != 0u) {
        const unsigned digit = static_cast<unsigned>(value[0]) - static_cast<unsigned>(L'0');
        if (digit >= 1u && digit <= kMaxGames && value[1] == L'\0') wanted = digit;
        SetEnvironmentVariableW(kGameInstanceVariable, nullptr);
    }
    for (unsigned attempt = 0u; attempt <= kMaxGames; ++attempt) {
        const unsigned number = attempt == 0u ? wanted : attempt;
        if (number == 0u || (attempt != 0u && number == wanted)) continue;
        // The first game's name is the one every earlier Nebula used.
        const std::wstring name = number == 1u ? base : base + L"-" + std::to_wstring(number);
        HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (mutex == nullptr) return false;  // nothing to tell games apart with: play, as the first
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            continue;
        }
        gOneGame = mutex;
        if (number > 1u && !settings.empty()) {
            const fs::path folder = settings / "instances" / std::to_wstring(number);
            seed_game_instance(folder, settings, galaxy::render::render_config_path());
            galaxy::set_game_instance(number, folder);
        }
        return false;
    }
    log_line("host:", "every game slot is taken");
    if (HWND window = FindWindowW(galaxy::render::host_window::kWindowClass, nullptr)) {
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(window);
    }
    return true;
}
#endif

// A second game's saves (galaxy/game_instance.h): its own folder, which starts as a copy of the first's.
fs::path game_instance_saves(const fs::path& saves) {
    const fs::path& folder = galaxy::game_instance_folder();
    if (folder.empty()) return saves;
    const fs::path own = folder / "saves";
    std::error_code error;
    if (!fs::exists(own, error)) {
        fs::create_directories(own, error);
        if (fs::is_directory(saves, error)) {
            fs::copy(saves, own, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
            if (error) std::fprintf(stderr, "[host] game %u's saves: copying the first's failed (%s)\n",
                                    galaxy::game_instance(), error.message().c_str());
        }
    }
    return own;
}

[[noreturn]] void finish_and_exit(bool restart) {
    log_line("host:", restart ? "restart" : "the game ended");
    // The room hears this player go (a host's closes for everyone), as the process ends without its destructors.
    gCoopSession.leave();
    for (int waited = 0; gSavesPending.load(std::memory_order_acquire) != 0 && waited < 500; waited++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (gSavesPending.load(std::memory_order_acquire) != 0) {
        log_line("host:", "a save still pending at exit");
    }
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
    std::fflush(stdout);
#if defined(_WIN32)
    if (restart) {
#if defined(NEBULA_HOST_RENDERER)
        if (gOneGame != nullptr) {
            CloseHandle(gOneGame);  // the new process takes it
        }
        if (const unsigned instance = galaxy::game_instance(); instance > 1u) {
            SetEnvironmentVariableW(kGameInstanceVariable, std::to_wstring(instance).c_str());
        }
#endif
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) != 0) {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            std::wstring command = GetCommandLineW();
            if (CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }
    }
#else
    (void)restart;
#endif
    std::_Exit(0);
}

void svc_app_exit(void*) {
    finish_and_exit(false);
}

void svc_app_restart(void*) {
    finish_and_exit(true);
}

#if defined(NEBULA_HOST_RENDERER)
// A movie frame's video is one baseline JPEG; the decoder writes its planes as rows of texels for the renderer.
int32_t svc_movie_video_decode(void*, const void* frame, uint32_t size, uint32_t width, uint32_t height, void* y, void* u, void* v) {
    gMovieFrames.fetch_add(1, std::memory_order_relaxed);
    if (galaxy::render::movie_replacement_showing()) {
        // A replacement video's frames show in place of these planes: their decode would be unseen work.
        gMovieSkipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const auto decode_start = std::chrono::steady_clock::now();
    struct DecodeTime {
        std::chrono::steady_clock::time_point start;
        ~DecodeTime() {
            gMovieDecodeNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - start).count()),
                                     std::memory_order_relaxed);
        }
    } decode_time{decode_start};
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        return -1;
    }
    const size_t luma = size_t(width) * height;
    const size_t chroma = size_t((width + 1u) / 2u) * ((height + 1u) / 2u);
    return galaxy::movie::decode_video({static_cast<const uint8_t*>(frame), size}, static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                                       {static_cast<uint8_t*>(y), luma}, {static_cast<uint8_t*>(u), chroma}, {static_cast<uint8_t*>(v), chroma});
}
#else
int32_t svc_movie_video_decode(void*, const void*, uint32_t, uint32_t, uint32_t, void*, void*, void*) {
    return 0;
}
#endif

}  // namespace


#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/mod_import.h"

namespace {

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20u) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += escaped;
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}


// The mods' disc files, cooked once into a store the game reads over its own (cook/overlay.rs): by the cooker beside
// this program, again when what they change has changed. A failure leaves the game as it is and says why in the log.
void cook_mod_files(const galaxy::mods::DiscPlan& plan, const fs::path& settings_folder, const std::string& game_id);

void prepare_mod_files(const fs::path& settings_folder, const std::string& game_id) {
    const std::string language = galaxy::mods::language_folder(
        game_id, static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language)));
    const galaxy::mods::DiscPlan plan = galaxy::mods::plan_disc_changes(gPak.store, game_id, language);
    gModPatches = plan.native_patches;
    if (!plan.save_folder.empty() && !gSaveDataPath.empty()) {
        // A clone starts from the player's save the first time, the rest starts new; the player's stays as it is.
        const fs::path mine = gSaveRoot / gSaveDataPath.substr(1);
        const fs::path theirs = plan.save_folder / gSaveDataPath.substr(1);
        std::error_code save_error;
        if (!fs::exists(theirs, save_error)) {
            fs::create_directories(theirs, save_error);
            if (plan.save_clone) {
                fs::copy(mine, theirs, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
            }
        }
        gModSaveRoot = plan.save_folder;
        log_line("mods:", "saves go to the mod's save folder");
    }
    for (const std::string& note : plan.notes) {
        log_line("mods:", note.c_str());
    }
    cook_mod_files(plan, settings_folder, game_id);
    // Converted mods over the rest, the top of the list last.
    for (const fs::path& store : plan.stores) {
        const std::string text = "package " + utf8_of(store.parent_path().filename()) + (gPak.store.attach_overlay(store) ? " applied" : " could not be read");
        log_line("mods:", text.c_str());
    }
}

void cook_mod_files(const galaxy::mods::DiscPlan& plan, const fs::path& settings_folder, const std::string& game_id) {
    if (plan.files.empty()) {
        return;
    }
    const fs::path folder = settings_folder / "mods" / ".cooked";
    const fs::path overlay = folder / "mods.pak";
    const fs::path signature_file = folder / "mods.signature";
    const std::string signature = galaxy::mods::plan_signature(plan, game_id);
    std::error_code error;
    fs::create_directories(folder, error);
    std::string kept;
    {
        std::ifstream in(signature_file, std::ios::binary);
        kept.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (kept != signature || !fs::is_regular_file(overlay, error)) {
        const fs::path plan_file = folder / "mods.plan";
        if (!galaxy::io::write_file_atomically(plan_file, galaxy::mods::plan_text(plan, game_id))) {
            log_line("mods:", "the plan could not be written");
            return;
        }
        fs::remove(signature_file, error);
#if defined(_WIN32)
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path cooker = fs::path(exe).parent_path() / "nebula-import.exe";
        std::wstring command = L"\"" + cooker.wstring() + L"\" overlay \"" + plan_file.wstring() + L"\" --output \"" +
                               overlay.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        DWORD exit_code = 1;
        if (CreateProcessW(cooker.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process)) {
            WaitForSingleObject(process.hProcess, INFINITE);
            GetExitCodeProcess(process.hProcess, &exit_code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            log_line("mods:", "the cooker (nebula-import) could not start");
        }
        if (exit_code != 0u) {
            log_line("mods:", "the mods' files could not be cooked: the game runs without them");
            return;
        }
#else
        // The importer's library, as the launcher loads it.
        using CookOverlay = int (*)(const char*, const char*);
        void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
        const auto cook = library != nullptr ? reinterpret_cast<CookOverlay>(dlsym(library, "nebula_cook_overlay")) : nullptr;
        if (cook == nullptr || cook(utf8_of(plan_file).c_str(), utf8_of(overlay).c_str()) != 0) {
            log_line("mods:", "the mods' files could not be cooked: the game runs without them");
            return;
        }
#endif
        if (!galaxy::io::write_file_atomically(signature_file, signature)) {
            log_line("mods:", "the signature could not be written");
        }
    }
    if (gPak.store.attach_overlay(overlay)) {
        char text[96];
        std::snprintf(text, sizeof(text), "%zu files applied", plan.files.size());
        log_line("mods:", text);
    } else {
        log_line("mods:", "the mods' files could not be read: the game runs without them");
    }
}

// The game id from Setup's game.pak, read without mapping it; false (and nothing changed) for a file that is not one:
// the tools then work without it.
bool read_game_id(const fs::path& path, std::string& id) {
    std::ifstream file(path, std::ios::binary);
    char header[64];
    if (!file.read(header, sizeof(header)) || std::memcmp(header, galaxy::disc::kStoreMagic, 8) != 0) return false;
    id.assign(header + 48, 6);
    return true;
}

// The launcher's mod tools: NebulaNative --import-mod <folder or archive>..., --remove-mod <id> or --list-mods
// [--game <id>] [--pak <game.pak>], on the mods folder beside the settings (as the game uses it). One JSON line on
// stdout; exit code 0 when everything succeeded.
int run_mods_tool() {
    std::vector<fs::path> args;
#if defined(_WIN32)
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) args.emplace_back(wide[i]);
        LocalFree(wide);
    }
#endif
    std::string game_id = "RMGE01";
    std::vector<fs::path> sources;
    const bool list = !args.empty() && args[0] == "--list-mods";
    const bool remove = !args.empty() && args[0] == "--remove-mod";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--game" && i + 1u < args.size()) {
            game_id = utf8_of(args[++i]);
        } else if (args[i] == "--pak" && i + 1u < args.size()) {
            std::string id;
            if (read_game_id(args[++i], id)) {
                game_id = id;
            }
        } else {
            sources.push_back(args[i]);
        }
    }
    const fs::path settings_folder = galaxy::runtime_settings_folder();
    if (settings_folder.empty()) {
        std::printf("{\"ok\":false,\"message\":\"no settings folder\"}\n");
        return 2;
    }
    std::error_code error;
    fs::create_directories(settings_folder / "mods", error);
    galaxy::mods::set_mods_folder(settings_folder / "mods", settings_folder / "mods.ini", game_id);
    if (remove) {
        std::string message;
        const bool ok = !sources.empty() &&
                        galaxy::mods::remove_mod(settings_folder / "mods", utf8_of(sources.front()), &message);
        std::printf("{\"ok\":%s,\"message\":%s}\n", ok ? "true" : "false", json_text(message).c_str());
        return ok ? 0 : 1;
    }
    if (list) {
        std::string out = "{\"ok\":true,\"folder\":" + json_text(utf8_of(settings_folder / "mods")) + ",\"mods\":[";
        bool first = true;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods(game_id)) {
            out += first ? "{" : ",{";
            first = false;
            out += "\"id\":" + json_text(mod.id) + ",\"enabled\":" + (mod.enabled ? "true" : "false") +
                   ",\"order\":" + std::to_string(mod.order) + ",\"riivolution\":" + (mod.riivolution ? "true" : "false") +
                   ",\"files\":" + ((mod.files || mod.loose) ? "true" : "false") + ",\"textures\":" +
                   (mod.textures ? "true" : "false") + ",\"movies\":" + (mod.movies ? "true" : "false") +
                   ",\"code_patches\":" + std::to_string(mod.code_patches) + ",\"native_patches\":" +
                   std::to_string(mod.native_patches) + ",\"problem\":" + json_text(mod.problem) + ",\"languages\":[";
            for (std::size_t l = 0; l < mod.languages.size(); ++l) out += (l ? "," : "") + json_text(mod.languages[l]);
            out += "],\"options\":[";
            for (std::size_t o = 0; o < mod.options.size(); ++o) {
                const galaxy::mods::ModOption& option = mod.options[o];
                out += std::string(o ? "," : "") + "{\"key\":" + json_text(option.key) + ",\"section\":" +
                       json_text(option.section) + ",\"name\":" + json_text(option.name) +
                       ",\"selected\":" + std::to_string(option.selected) + ",\"choices\":[";
                for (std::size_t c = 0; c < option.choices.size(); ++c) {
                    const bool other = c < option.other_region.size() && option.other_region[c];
                    out += std::string(c ? "," : "") + "{\"name\":" + json_text(option.choices[c]) +
                           ",\"other_region\":" + (other ? "true" : "false") + "}";
                }
                out += "]}";
            }
            out += "],\"overlaps\":[";
            for (std::size_t o = 0; o < mod.overlaps.size(); ++o) {
                const galaxy::mods::ModOverlap& overlap = mod.overlaps[o];
                out += std::string(o ? "," : "") + "{\"other\":" + json_text(overlap.other) + ",\"above\":" +
                       (overlap.other_above ? "true" : "false") + ",\"files\":" + std::to_string(overlap.files) +
                       ",\"textures\":" + std::to_string(overlap.textures) + "}";
            }
            out += "]}";
        }
        out += "]}";
        std::printf("%s\n", out.c_str());
        return 0;
    }
    int failed = 0;
    std::string out = "{\"results\":[";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const galaxy::mods::ImportResult result = galaxy::mods::import_mod(sources[i], settings_folder / "mods");
        failed += result.ok ? 0 : 1;
        out += std::string(i ? "," : "") + "{\"ok\":" + (result.ok ? "true" : "false") + ",\"id\":" + json_text(result.id) +
               ",\"replaced\":" + (result.replaced ? "true" : "false") + ",\"files\":" + std::to_string(result.files) +
               ",\"bytes\":" + std::to_string(result.bytes) + ",\"kinds\":" + json_text(result.kinds) +
               ",\"message\":" + json_text(result.message) + "}";
    }
    out += "],\"ok\":" + std::string(failed == 0 && !sources.empty() ? "true" : "false") + "}";
    std::printf("%s\n", out.c_str());
    return failed == 0 && !sources.empty() ? 0 : 1;
}

}  // namespace
#endif

namespace {

// The disc when --pak is not given: content/game.pak (as nebula-import and Setup write it) or game.pak, next to the
// program, in the current folder, or in the install root above it (versions/<version>/, the launcher's layout).
// Empty when none of them has one.
fs::path find_default_pak(const fs::path& program) {
    std::vector<fs::path> folders;
    std::error_code error;
    if (!program.empty()) folders.push_back(program);
    if (fs::path current = fs::current_path(error); !error) folders.push_back(std::move(current));
    if (program.parent_path().filename() == "versions") folders.push_back(program.parent_path().parent_path());
    for (const fs::path& folder : folders) {
        for (const fs::path& pak : {folder / "content" / "game.pak", folder / "game.pak"}) {
            if (fs::is_regular_file(pak, error)) return pak;
        }
    }
    return {};
}

// The saves when --nand is not given: a saves folder already next to the program or in the current folder (a test
// build run as docs/TEST_BUILDS.md says), else the launcher's (saves in Nebula's data folder, by the settings).
fs::path default_nand(const fs::path& program) {
    std::error_code error;
    if (!program.empty() && fs::is_directory(program / "saves", error)) return program / "saves";
    if (fs::is_directory("saves", error)) return fs::absolute("saves", error);
    if (const fs::path data = galaxy::nebula_data_directory(); !data.empty()) return data / "saves";
    return program.empty() ? fs::path("saves") : program / "saves";
}

}  // namespace

int main(int argc, char** argv) {
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    if (const char* error = galaxy::host::test_data_paths_error(argc, argv)) {
        std::fprintf(stderr, "[test-paths] %s\n", error);
        return 2;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    if (argc >= 3 && std::strcmp(argv[1], "--crash-report") == 0) {
        return show_crash_report_dialog();
    }
    if (claim_game_instance(argc, argv)) {
        return 0;
    }
#endif
#if defined(NEBULA_HOST_SDL)
    if (!galaxy::sdl::start()) {
        return 1;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // A player's start has nowhere to print (a launcher with no terminal, or none): the log goes to logs/latest.log beside the
    // settings, the last start's to previous.log. A test or a shell capturing the output to a pipe or a file keeps it.
    {
        const DWORD output = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        if (output != FILE_TYPE_PIPE && output != FILE_TYPE_DISK && ::galaxy::run_options().log_to_console == nullptr) {
            const fs::path settings = galaxy::runtime_settings_folder();
            std::error_code error;
            if (!settings.empty() && fs::create_directories(settings / "logs", error) | fs::is_directory(settings / "logs", error)) {
                fs::rename(settings / "logs" / "latest.log", settings / "logs" / "previous.log", error);
                FILE* log = nullptr;
                if (_wfreopen_s(&log, (settings / "logs" / "latest.log").c_str(), L"w", stdout) == 0) {
                    _dup2(_fileno(stdout), _fileno(stderr));
                }
            }
        }
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    for (int i = 1; i + 1 < argc; ++i) {  // (--host may come without its value)
        if (std::strcmp(argv[i], "--diagnostics") == 0) {
            galaxy::g_diagnostics_recording = argv[i + 1][0] == '1';
        }
    }
    // GALAXY_FRAME_TIMELINE=1: the frame pipeline's events (frame_timeline.h), written at exit.
    galaxy::timeline::initialize_from_environment();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // Physical-pixel DPI awareness before any window (sizes and mouse coordinates in pixels), and CRT assertion and
    // error reports to stderr instead of dialogs.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
#endif
#if defined(_WIN32)
    // GALAXY_TEST_CPU_AFFINITY: the logical processors the process may use (hex mask; 0xF = two cores with SMT, a
    // 2C/4T laptop's shape).
    if (const char* mask = ::galaxy::run_options().test_cpu_affinity) {
        SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(std::strtoull(mask, nullptr, 16)));
    }
    install_crash_reporter();
#endif
#if defined(NEBULA_HOST_RENDERER)
    if (argc >= 2 && (std::strcmp(argv[1], "--import-mod") == 0 || std::strcmp(argv[1], "--list-mods") == 0 ||
                      std::strcmp(argv[1], "--remove-mod") == 0)) {
        return run_mods_tool();
    }
#endif
    boot_mark("main");
    fs::path pak, saves, module;
    // A path in the command line: argv is in the system's code page, which cannot hold every folder name.
    const auto arg_path = [&](int index) {
#if defined(_WIN32)
        int count = 0;
        if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
            fs::path path = index < count ? fs::path(wide[index]) : fs::path(argv[index]);
            LocalFree(wide);
            return path;
        }
#endif
        return fs::path(argv[index]);
    };
    double seconds = 0;
    std::error_code error;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        // Each option takes the argument after it, but --host's port may be left out.
        const char* value = i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0 ? argv[i + 1] : nullptr;
        if (value != nullptr) {
            ++i;
        }
#if defined(NEBULA_HOST_RENDERER)
        if (key == "--host" && value == nullptr) {
            gCoopAutoStart = CoopAutoStart::Host;
            gCoopAutoPort = galaxy::kCoopDefaultPort;
            continue;
        }
#endif
        if (value == nullptr) {
            die("%s takes a value", key.c_str());
        }
        if (key == "--pak") {
            pak = arg_path(i);
        } else if (key == "--saves"
#if !defined(NEBULA_RELEASE)
                   || key == "--nand"
#endif
        ) {
            saves = arg_path(i);
        } else if (key == "--module") {
            module = arg_path(i);
        } else if (key == "--diagnostics") {
            // Read above, before the timeline starts.
        } else if (key == "--seconds") {
            seconds = std::atof(value);
#if defined(NEBULA_HOST_RENDERER)
        } else if (key == "--host") {
            // Online co-op (docs/COOP_PLAN.md): as the main menu opens, Host plays the last played save on this port.
            std::string ignored;
            std::uint16_t port = 0u;
            if (!galaxy::parse_coop_endpoint(std::string("host:") + value, ignored, port) ||
                std::string(value).find_first_not_of("0123456789") != std::string::npos) {
                die("--host takes a port, 1 to 65535");
            }
            gCoopAutoStart = CoopAutoStart::Host;
            gCoopAutoPort = port;
        } else if (key == "--join") {
            // As the main menu opens, Join connects to the address.
            std::string address;
            std::uint16_t port = 0u;
            if (!galaxy::parse_coop_endpoint(value, address, port)) {
                die("--join takes an address, address:port or [IPv6]:port");
            }
            gCoopAutoStart = CoopAutoStart::Join;
            gCoopAutoAddress = value;
#endif
        } else {
            die("unknown option %s", key.c_str());
        }
    }
    // Without --pak and --saves, the disc and saves are found as find_default_pak and default_nand say, so the
    // program starts with no arguments from a folder that holds content/game.pak.
    fs::path program = galaxy::program_directory();
    if (program.empty()) {
        program = fs::absolute(arg_path(0), error).parent_path();
    }
    if (pak.empty()) {
        pak = find_default_pak(program);
        if (pak.empty()) {
            die("no game.pak: pack your disc with nebula-import into a content folder next to the program, or name it: "
                "--pak <game.pak> [--saves <saves folder>] [--module <smg_game.dll>] [--seconds N]");
        }
    }
    if (saves.empty()) {
        saves = default_nand(program);
    }
    saves = game_instance_saves(saves);
    if (const unsigned instance = galaxy::game_instance(); instance > 1u) {
        log_line("host:", ("game " + std::to_string(instance) + ", with its own settings and saves in " +
                           utf8_of(galaxy::game_instance_folder())).c_str());
    }
    log_line("paths:", ("disc " + pak.string() + ", saves " + saves.string()).c_str());
    if (module.empty()) {
#if defined(_WIN32)
        module = program / "smg_game.dll";
#elif defined(__APPLE__)
        module = program / "libsmg_game.dylib";
#else
        module = program / "libsmg_game.so";
#endif
    }
    gSaveRoot = saves;
    gCoopSaves.emplace(gSaveRoot / "coop_saves");
    gCoopMenu.set_saves(&*gCoopSaves);
    gCoopMenu.set_mods_source(coop_room_mods);
    open_pak(pak);
    {
        // The room compares games by id, revision and the store's file table (its entries, as the import laid them
        // out): boot.bin's first bytes and no main.dol, which the cooked store does not keep.
        uint8_t boot[8] = {};
        std::memcpy(boot, gPak.id, 6u);
        boot[7] = static_cast<uint8_t>(gPak.store.revision());
        std::vector<uint8_t> table;
        for (int32_t i = 0;; ++i) {
            const std::optional<galaxy::disc::Entry> entry = gPak.store.entry(i);
            if (!entry) break;
            const uint64_t fields[3] = {entry->folder ? 1u : 0u, entry->length, entry->position};
            const auto* bytes = reinterpret_cast<const uint8_t*>(fields);
            table.insert(table.end(), bytes, bytes + sizeof(fields));
            if (entry->name != nullptr) table.insert(table.end(), entry->name, entry->name + std::strlen(entry->name) + 1u);
        }
        gCoopDisc = galaxy::coop::describe_disc(boot, {}, table);
    }
    boot_mark("disc opened");
    load_module(module);
    boot_mark("game module loaded");
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::set_texture_pack_game_id(gPak.id);  // packs' and dumps' game folders: the disc's release
    {
        // Mii import: the files in the import folder (next to the settings: any Mii files, mii_formats.h)
        // join the library before the game reads it; the settings import more while it runs.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const fs::path import_folder = settings_folder.empty() ? fs::path{} : settings_folder / "miis";
        if (!import_folder.empty()) {
            fs::create_directories(import_folder, error);
        }
        galaxy::host::set_mii_library(mii_library_path(), console_mii_database_path(), import_folder,
                                      [](galaxy::host::MiiRecord& mii) {
                                          galaxy::host::fix_wii_mii(mii);
                                          const NebulaMii fields = nebula_mii(galaxy::host::decode_wii_mii(mii));
                                          return gModule.mii_valid(&fields) != 0u;
                                      });
        std::string message;
        (void)galaxy::host::import_mii_folder(&message);
        std::fprintf(stderr, "[mii] import folder %s: %s (%u in the library)\n", utf8_of(import_folder.filename()).c_str(),
                     message.c_str(), galaxy::host::mii_library_count());
        (void)galaxy::host::take_mii_installs();  // in the file the game reads at boot
    }
#if defined(NEBULA_HOST_RENDERER)
    // The player's settings (GALAXY_RUNTIME_SETTINGS_PATH in tests) before the mods: the language they choose
    // picks the mods' text (and is the game's language from then on).
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
#endif
    {
        // Mods: the mods folder next to the settings (galaxy::mods), applied before the game reads its disc.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const std::string game_id(gPak.store.game_id());
        char title[9];
        std::snprintf(title, sizeof(title), "%02x%02x%02x%02x", (unsigned char)gPak.id[0], (unsigned char)gPak.id[1], (unsigned char)gPak.id[2], (unsigned char)gPak.id[3]);
        gSaveDataPath = std::string("/title/00010000/") + title + "/data";
        fs::create_directories(gSaveRoot / gSaveDataPath.substr(1), error);  // the release's own save folder
        if (!settings_folder.empty()) {
            // A second game (galaxy/game_instance.h) plays the first's installed mods, with its own list of which are on.
            const fs::path& instance = galaxy::game_instance_folder();
            const fs::path mods_home = instance.empty() ? settings_folder : instance.parent_path().parent_path();
            fs::create_directories(mods_home / "mods", error);
            galaxy::mods::set_mods_folder(mods_home / "mods", settings_folder / "mods.ini", game_id);
            // A join Nebula started again for: this start plays the room's mods (prepare_mod_files), then joins.
            if (std::optional<galaxy::coop::NextJoin> join = galaxy::coop::take_next_join(coop_next_join_file())) {
                std::vector<galaxy::mods::RoomModChoice> room;
                for (const galaxy::coop::SharedMod& mod : join->mods) {
                    galaxy::mods::RoomModChoice choice{mod.id, mod.folder, mod.own_text, {}};
                    for (const auto& [key, selected] : mod.options) choice.options[key] = selected;
                    room.push_back(std::move(choice));
                }
                galaxy::mods::set_room_mods(std::move(room));
                gCoopAutoStart = CoopAutoStart::Join;
                gCoopAutoAddress = join->address;
                std::fprintf(stderr, "[coop] starting with the room's mods (%s) to join %s\n",
                             galaxy::coop::mods_text(join->mods).c_str(), join->address.c_str());
                gCoopStartedRoomMods = std::move(join->mods);
            }
            galaxy::mods::set_mods_store_file(gPak.store_file);
            {
                // The mods mods.ini has on: their packs and movies from the first frame.
                const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
                galaxy::render::set_texture_pack_mod_folders(galaxy::mods::texture_folders(mods),
                                                             galaxy::mods::texture_folder_categories_off(mods));
                galaxy::render::set_movie_mod_folders(galaxy::mods::movie_folders(mods));
            }
            prepare_mod_files(settings_folder, game_id);
        }
        {
            // The session's mods, for crash reports: one line each, and whether one changes the game itself.
            std::string lines;
            bool change_game = false;
            for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
                if (!mod.enabled) continue;
                std::string kinds;
                for (const auto& [on, name] : {std::pair{mod.riivolution, "Riivolution"},
                                               std::pair{mod.files || mod.loose, "game files"},
                                               std::pair{mod.textures, "textures"}, std::pair{mod.movies, "movies"}}) {
                    if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
                }
                lines += "  " + mod.id + (kinds.empty() ? "" : " (" + kinds + ")") + "\n";
                change_game = change_game || mod.riivolution || mod.files || mod.loose;
            }
            galaxy::crash::set_session_mods(lines, change_game);
        }
        {
            // The game's version and its languages, which the language setting offers.
            const std::vector<std::string> languages = gPak.store.language_folders();
            std::string list;
            for (const std::string& language : languages) list += (list.empty() ? "" : ", ") + language;
            char text[256];
            std::snprintf(text, sizeof(text), "%s revision %u: %s", game_id.c_str(), gPak.store.revision(),
                          list.empty() ? "no language folders" : list.c_str());
            log_line("disc:", text);
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_disc_languages(languages);
#endif
        }
    }
#endif

    boot_mark("settings and mods");
    static NebulaHostServices services{};
    services.version = NEBULA_GAME_API_VERSION;
    services.size = sizeof(services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.heap_alloc = [](void*, size_t size) -> void* { return std::malloc(size); };
    services.heap_realloc = [](void*, void* data, size_t size) -> void* { return std::realloc(data, size); };
    services.heap_free = [](void*, void* data) { std::free(data); };
    services.thread_slots = [](void*) -> void** {
        static thread_local void* slots[NEBULA_THREAD_SLOTS];
        return slots;
    };
    services.scene_marker = svc_scene_marker;
    services.movie_frame = svc_movie_frame;
    services.presence = svc_presence;
    services.achievement = svc_achievement;
    services.achievement_date = svc_achievement_date;
    services.stat_add = svc_stat_add;
    services.stat_get = svc_stat_get;
    services.settings_view = svc_settings_view;
    services.settings_input = svc_settings_input;
    services.sharp_2d = svc_sharp_2d;
    services.button_icons = svc_button_icons;
    services.art_export = svc_art_export;
    services.layer_2d_lines = svc_layer_2d_lines;
    services.pointer_wheel = svc_pointer_wheel;
    services.galaxy_select_stage = svc_galaxy_select_stage;
    services.galaxy_select_names_wanted = svc_galaxy_select_names_wanted;
    services.galaxy_select_name = svc_galaxy_select_name;
    services.curved_surface = svc_curved_surface;
    services.shape_array = svc_shape_array;
    services.shape_geometry = svc_shape_geometry;
    services.shape_forget = svc_shape_forget;
    services.shape_draw = svc_shape_draw;
    services.native_material = svc_native_material;
    services.test_frame_step = deterministic_mode() ? svc_test_frame_step : nullptr;
    services.view_projection = svc_view_projection;
    services.view_viewport = svc_view_viewport;
    services.view_scissor = svc_view_scissor;
    services.light = svc_light;
    services.frame_alpha = svc_frame_alpha;
    services.texture_coordinate = svc_texture_coordinate;
    services.immediate = svc_immediate;
    services.target_begin = svc_target_begin;
    services.target_begin_over_frame = svc_target_begin_over_frame;
    services.target_end = svc_target_end;
    services.target_clear = svc_target_clear;
    services.frame_pass = svc_frame_pass;
    services.frame_take = svc_frame_take;
    services.texture_image = svc_texture_image;
    services.texture_hd = svc_texture_hd;
    services.mii_record = svc_mii_record;
    services.screen_probe = svc_screen_probe;
    services.screen_probe_results = svc_screen_probe_results;
    services.frame_clear = svc_frame_clear;
    services.frame_show = svc_frame_show;
    services.frame_end = svc_frame_end;
    services.marker = svc_marker;
    services.clock_ns = svc_clock_ns;
    services.calendar_ns = svc_calendar_ns;
    services.stripe_particle = svc_stripe_particle;
    services.code_patch = svc_code_patch;
    services.asset_find = svc_asset_find;
    services.asset_entry = svc_asset_entry;
    services.asset_child = svc_asset_child;
    services.asset_read = svc_asset_read;
    services.memory_reserve = svc_memory_reserve;
    services.memory_commit = svc_memory_commit;
    services.memory_decommit = svc_memory_decommit;
    services.memory_release = svc_memory_release;
    services.thread_create = svc_thread_create;
    services.thread_adopt = svc_thread_adopt;
    services.thread_pass = svc_thread_pass;
    services.thread_wait = svc_thread_wait;
    services.thread_exit = svc_thread_exit;
    services.thread_cancel = svc_thread_cancel;
    services.lock = svc_lock;
    services.unlock = svc_unlock;
    services.threads_dump = svc_threads_dump;
    services.asset_map = svc_asset_map;
    services.asset_map_private = svc_asset_map_private;
    services.asset_unmap = svc_asset_unmap;
    services.game_id = svc_game_id;
    services.frame_wait = svc_frame_wait;
    services.frame_begun = svc_frame_begun;
    services.save_request = svc_save_request;
    services.system_setting = svc_system_setting;
    services.input_read = svc_input_read;
    services.input_control = svc_input_control;
    services.speaker_samples = svc_speaker_samples;
    services.voices_setup = svc_audio_setup;
    services.voices_mix = svc_audio_mix;
    services.voices_frame = svc_audio_frame;
    services.movie_audio = svc_movie_audio;
    services.movie_audio_position = svc_movie_audio_position;
    services.movie_audio_control = svc_movie_audio_control;
    services.sound_clock = svc_audio_control;
    services.hbm = svc_hbm;
    services.movie_video_decode = svc_movie_video_decode;
    services.memory_rewritten = svc_memory_rewritten;
    services.game_context = svc_game_context;
    services.app_exit = svc_app_exit;
    services.app_restart = svc_app_restart;
    services.test_scene_frame = svc_test_scene_frame;
    services.display_extent = svc_display_extent;
    services.option = svc_option;
    services.game_light = svc_game_light;
    services.game_stage = svc_game_stage;
    services.coop_player = svc_coop_player;
    services.coop_menu = svc_coop_menu;
    services.coop_chat = svc_coop_chat;
    gCoopChat.attach(gCoopSession);
    galaxy::coop::add_room_commands(gCoopChat, gCoopSession);
    add_coop_star_counter_command();
    services.coop_list = svc_coop_list;
    services.coop_remote = svc_coop_remote;
    services.coop_world_report = svc_coop_world_report;
    services.coop_world_take = svc_coop_world_take;
    services.coop_star_count = svc_coop_star_count;
    services.coop_rules = svc_coop_rules;
    services.coop_achievement_take = svc_coop_achievement_take;
    services.coop_popup_take = svc_coop_popup_take;
    services.coop_objects_report = svc_coop_objects_report;
    services.coop_objects_take = svc_coop_objects_take;
#if defined(NEBULA_HOST_RENDERER)
    // The display configuration (GALAXY_RENDER_CONFIG_PATH in tests), loaded before the window; the settings overlay
    // reads its frame art from the game files.
    try {
        galaxy::render::load_render_config_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
    galaxy::render::set_native_settings_content_root(utf8_of(galaxy::host::locate_game(pak).content_root));
    // Discord: never from a hidden test run, which must leave the player's status alone.
    if (const char* hidden = ::galaxy::run_options().hide_window; hidden == nullptr || hidden[0] == '\0' || hidden[0] == '0') {
        galaxy::discord::set_enabled_provider([] { return galaxy::get_runtime_settings().discord_status; });
        galaxy::discord::start(kDiscordClientId);
    }
    // The environment is final from here: the input report's reads are served from a table (each read otherwise scans
    // the whole environment block, 100 reports a second).
    galaxy::host::set_env_flag_cache_enabled(true);
    const galaxy::render::RenderConfig render_config = galaxy::render::get_render_config();
    gNative.log = native_log;
    if (!galaxy::render::initialize(static_cast<int>(render_config.window_width),
                                    static_cast<int>(render_config.window_height))) {
        const std::string reason = galaxy::render::startup_failure();
        if (retry_start_for_gpu_memory(reason)) {
            finish_and_exit(true);
        }
        die("the renderer did not start%s%s", reason.empty() ? "" : ": ", reason.c_str());
    }
    boot_mark("renderer started");
    // While the start runs ahead, the frames' presents alone reach the screen (svc_frame_started).
    galaxy::render::set_frame_end_presents(!gBootFast.load(std::memory_order_relaxed));
    gAudio = new galaxy::host::NativeGameAudio();  // lives as long as the process (the audio clock is detached)
    // The live controls' links to the window: the mouse as the pointer, keyboard capture and controller navigation
    // while the settings overlay or the HOME Menu is open.
    galaxy::host::install_host_pointer_provider([]() noexcept {
        galaxy::host::HostPointerState state{};
        const galaxy::render::HostPointerState pointer = galaxy::render::poll_host_pointer_state();
        state.window_focused = pointer.window_focused;
        state.inside_client = pointer.inside_client;
        state.absolute_valid = pointer.absolute_valid && pointer.client_width > 1 && pointer.client_height > 1;
        state.left_button = pointer.left_button;
        state.right_button = pointer.right_button;
        state.client_x = pointer.client_x;
        state.client_y = pointer.client_y;
        state.client_width = pointer.client_width;
        state.client_height = pointer.client_height;
        state.absolute_sequence = pointer.absolute_sequence;
        state.debug_window = pointer.debug_window;
        return state;
    });
    galaxy::host::install_host_pointer_transition_provider(
        [](std::uint64_t after) noexcept { return galaxy::render::poll_host_pointer_transition(after); });
    galaxy::host::install_native_keyboard_capture_provider([]() noexcept { return galaxy::render::native_window_input_captured(); });
    galaxy::host::install_native_settings_pad_hooks(
        []() noexcept { return galaxy::render::native_settings_overlay_visible() || galaxy::render::home_menu_visible(); },
        [](std::uintptr_t key) noexcept { galaxy::render::native_settings_post_key(key); });
#endif
    if (gModule.api_fingerprint == nullptr || gModule.api_fingerprint() != NEBULA_GAME_API_FINGERPRINT) {
        die("the game module was built from another nebula_game_api.h than the host (%08x): build both again",
            static_cast<unsigned>(NEBULA_GAME_API_FINGERPRINT));
    }
    if (gModule.attach(&services) != NEBULA_GAME_API_VERSION) {
        die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
    }

    name_thread(L"Nebula Host Main");
    gFs.start();
#if !defined(NEBULA_HOST_RENDERER)
    std::thread(ai_thread).detach();
#endif
#if defined(NEBULA_HOST_RENDERER)
    std::thread(input_thread).detach();
#endif
#if defined(NEBULA_HOST_SDL)
    galaxy::sdl::start_thread_sampler();
#endif
    std::thread game([] {
        name_thread(L"Nebula Game");
        galaxy::host::perf_hint_add_current_thread();
        gModule.run();
    });
#if defined(_WIN32)
    if (const char* profile = ::galaxy::run_options().profile_game; profile != nullptr && std::atof(profile) > 0.0) {
        const HANDLE sampled = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                          GetThreadId(static_cast<HANDLE>(game.native_handle())));
        if (sampled != nullptr) {
            std::thread([sampled, seconds = std::atof(profile)] {
                game_profile::run(sampled, seconds);
                CloseHandle(sampled);
            }).detach();
        }
    }
    // The simulation thread's CPU time, reported each second (the game and everything it calls on that thread).
    HANDLE game_thread = nullptr;  // its own handle: detach() closes the thread object's
    DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(game.native_handle()), GetCurrentProcess(), &game_thread,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    uint64_t game_cycles_last = 0;
    const char* trace_env = ::galaxy::run_options().trace_thread_cpu;
    const bool trace_thread_cpu = trace_env != nullptr && trace_env[0] == '1';
    // Thread cycles count at the time-stamp counter's rate, measured here against the performance counter.
    const double tsc_hz = !trace_thread_cpu ? 0.0 : [] {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        const uint64_t t0 = __rdtsc();
        do {
            QueryPerformanceCounter(&b);
        } while (b.QuadPart - a.QuadPart < f.QuadPart / 20);
        return (double)(__rdtsc() - t0) * (double)f.QuadPart / (double)(b.QuadPart - a.QuadPart);
    }();
#endif
    game.detach();
    boot_mark("game started");

    const auto end = Clock::now() + std::chrono::duration<double>(seconds > 0 ? seconds : 1e9);
    bool first_frame = false;
    uint32_t reported = 0;
    auto next_report = Clock::now() + std::chrono::seconds(1);
    while (Clock::now() < end && !gTourDone.load(std::memory_order_relaxed)) {
#if defined(NEBULA_HOST_RENDERER)
        // The window's messages, on the thread that created it.
#if defined(NEBULA_HOST_SDL)
        galaxy::sdl::pump_events(4);
        if (const bool away = galaxy::sdl::in_background(); away != gSuspended) {
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = away;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(away);
            log_line("host:", away ? "suspended (out of sight)" : "resumed");
        }
#else
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 4, QS_ALLINPUT);
        // Minimised, the game and its sound stop, as on the platforms that suspend.
        if (const bool away = galaxy::render::host_window::is_minimized(); away != gSuspended) {
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = away;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(away);
            log_line("host:", away ? "suspended (minimised)" : "resumed");
        }
#endif
        galaxy::render::pump_messages();
        if (galaxy::render::quit_requested()) {
            break;
        }
        if (!first_frame && gSubmissions.load(std::memory_order_relaxed) != 0u) {
            first_frame = true;
            boot_mark("first game frame");
        }
        if (Clock::now() < next_report) {
            continue;
        }
#else
        std::this_thread::sleep_until(next_report);
#endif
        next_report += std::chrono::seconds(1);
        const uint32_t frames = gGameFrames.load(std::memory_order_relaxed);
        if (frames != reported) {
            char text[384];
            std::snprintf(text, sizeof(text), "frame %u, file reads %u, save ops %u, sound blocks %u, sound subframes %u",
                          frames,
                          gFileReads.load(std::memory_order_relaxed), gSaveOps.load(std::memory_order_relaxed),
                          gSoundBlocks.load(std::memory_order_relaxed), gSoundSubframes.load(std::memory_order_relaxed));
#if defined(NEBULA_HOST_RENDERER)
            if (frames / 60 % 30 == 0) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
            const size_t length = std::strlen(text);
            static uint64_t last_decode_ns = 0;
            const uint64_t decode_ns = gMovieDecodeNs.load(std::memory_order_relaxed);
            std::snprintf(text + length, sizeof(text) - length,
                          ", renderer frames %llu, movie frames %u (decoded in %.1f ms/s, %u replaced)",
                          (unsigned long long)gSubmissions.load(std::memory_order_relaxed),
                          gMovieFrames.load(std::memory_order_relaxed),
                          static_cast<double>(decode_ns - last_decode_ns) / 1.0e6,
                          gMovieSkipped.load(std::memory_order_relaxed));
            last_decode_ns = decode_ns;
#endif
#if defined(_WIN32)
            if (trace_thread_cpu) {
                report_thread_cpu(tsc_hz);
            }
            ULONG64 cycles = 0;
            if (QueryThreadCycleTime(game_thread, &cycles)) {
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, ", game cpu %.1f ms/s",
                              (double)(cycles - game_cycles_last) * 1000.0 / tsc_hz);
                game_cycles_last = cycles;
            }
#endif
            log_line("host:", text);
            reported = frames;
        }
    }
    log_line("host:", Clock::now() < end ? "the window was closed" : "time limit reached");
    log_line("host:", ("3D scenes " + std::to_string(gSceneBegins.load(std::memory_order_relaxed))).c_str());
#if defined(NEBULA_HOST_RENDERER)
    galaxy::loss::report();
#endif
    if (gModule.debug_dump != nullptr) {
        gModule.debug_dump();
    }
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::timeline::dump();
#endif
    finish_and_exit(false);
}
