// main.cpp — FLOW-ON! entry point: integrates all phases.
//
// Build with CMake (see CMakeLists.txt).  The easiest path is:
//   cmake -B build -G "Visual Studio 17 2022" -A x64
//   cmake --build build --config Release
//
// For WinUI 3 dashboard: install Microsoft.WindowsAppSDK via NuGet in the
// generated VS solution, then rebuild with ENABLE_WINUI3_DASHBOARD defined.

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined globally in CMakeLists.txt
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <atomic>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "audio_manager.h"
#include "transcriber.h"
#include "formatter.h"
#include "injector.h"
#include "overlay.h"
#include "dashboard.h"
#include "snippet_engine.h"
#include "config_manager.h"
#include "../Resource.h"   // IDI_IDLE_ICON, IDI_RECORDING_ICON

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// ------------------------------------------------------------------
// Application-defined window messages
// ------------------------------------------------------------------
#define WM_TRAYICON            (WM_APP + 1)
#define WM_SHOW_DASHBOARD      (WM_APP + 2)
#define WM_START_TRANSCRIPTION (WM_APP + 3)
#define WM_TRANSCRIPTION_DONE  (WM_APP + 4)

// Hotkey
#define HOTKEY_ID_RECORD       1

// WM_TIMER IDs
#define TIMER_ID_KEYCHECK      2   // 30 ms poll for Alt key release + VAD during recording
#define TIMER_ID_IDLECHECK     3   // 30 s idle check for model unload

// ------------------------------------------------------------------
// Globals
// ------------------------------------------------------------------
static NOTIFYICONDATAW  g_nid            = {};
static UINT             g_taskbarCreated = 0;
static HINSTANCE        g_hInst          = nullptr;
static HWND             g_hwnd           = nullptr;

// Subsystem managers
static AudioManager  g_audio;
static Transcriber   g_transcriber;
static Overlay       g_overlay;
static Dashboard     g_dashboard;
static SnippetEngine g_snippets;
static ConfigManager g_config;

// The audio callback writes RMS here; overlay.cpp reads it.
// Defined here, extern-declared in audio_manager.cpp.
Overlay* g_overlayPtr = nullptr;

// State machine
enum class AppState { IDLE, RECORDING, TRANSCRIBING, INJECTING };
static std::atomic<AppState> g_state{AppState::IDLE};
static std::atomic<bool>     g_recordingActive{false};
static bool                  g_hotkeyDown   = false;
static bool                  g_altHotkeyFallback = false; // true = using Alt+Shift+V
static std::atomic<uint64_t> g_idleUnloadMs{120000};  // 120 s default — keep model warm
static float                 g_vadNoiseFloor = 0.004f;
static int                   g_vadSilentFrames = 0;
static int                   g_vadSpeechFrames = 0;

struct RecentTranscript {
    std::string normalized;
    uint64_t tick = 0;
};
static RecentTranscript g_recentTranscript;

// Timing: used to measure transcription latency for the history entry
static std::chrono::steady_clock::time_point g_recordStart;

static bool FileExistsWPath(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};

    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

static uint64_t ClampIdleUnloadMs(int sec)
{
    if (sec < 15) sec = 15;
    if (sec > 600) sec = 600;
    return static_cast<uint64_t>(sec) * 1000ULL;
}

static std::string NormalizeForDedup(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    bool prevSpace = true;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            prevSpace = false;
        } else if (!prevSpace) {
            out.push_back(' ');
            prevSpace = true;
        }
    }

    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static bool IsLikelyDuplicateText(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;

    const size_t minLen = std::min(a.size(), b.size());
    const size_t maxLen = std::max(a.size(), b.size());
    if (minLen < 8) return false;

    if (maxLen - minLen <= 2) {
        size_t diff = 0;
        for (size_t i = 0; i < minLen; ++i) {
            if (a[i] != b[i]) {
                ++diff;
                if (diff > 2) break;
            }
        }
        if (diff <= 2) return true;
    }

    const std::string& shorter = (a.size() < b.size()) ? a : b;
    const std::string& longer  = (a.size() < b.size()) ? b : a;
    if (longer.find(shorter) != std::string::npos) {
        return static_cast<double>(shorter.size()) / static_cast<double>(longer.size()) >= 0.85;
    }

    return false;
}

// ------------------------------------------------------------------
// Helper: swap tray icon and tooltip
// ------------------------------------------------------------------
static void SetTrayIcon(UINT iconId, const wchar_t* tip)
{
    HICON hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(iconId));
    if (!hIcon) hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // safe fallback
    g_nid.hIcon = hIcon;
    wcscpy_s(g_nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ------------------------------------------------------------------
// Helper: right-click tray menu
// ------------------------------------------------------------------
static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING,    1001, L"Dashboard");
    AppendMenuW(menu, MF_SEPARATOR, 0,    nullptr);
    AppendMenuW(menu, MF_STRING,    1002, L"Exit");

    POINT pt; GetCursorPos(&pt);
    // Required by Shell docs so the menu dismisses on click-away
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 1001) PostMessageW(hwnd, WM_SHOW_DASHBOARD, 0, 0);
    if (cmd == 1002) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
}

// ------------------------------------------------------------------
// StopRecordingOnce — atomic CAS ensures only ONE path (hotkey release
// OR VAD silence) wins and triggers the transcription.
// ------------------------------------------------------------------
static void StopRecordingOnce(HWND hwnd)
{
    bool expected = true;
    if (g_recordingActive.compare_exchange_strong(expected, false,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        g_audio.stopCapture();
        g_state.store(AppState::TRANSCRIBING, std::memory_order_release);
        g_overlay.setState(OverlayState::Processing);
        SetTrayIcon(IDI_IDLE_ICON, L"FLOW-ON! \u2014 Processing\u2026");
        PostMessageW(hwnd, WM_START_TRANSCRIPTION, 0, 0);
        KillTimer(hwnd, TIMER_ID_KEYCHECK);
    }
}

// ------------------------------------------------------------------
// Build the model path relative to the executable directory
// ------------------------------------------------------------------
static std::string ResolveModelPath(const std::string& configuredModel)
{
    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    // Strip filename
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    // Try configured model first, then common English fallbacks.
    std::vector<std::wstring> candidateFiles;
    if (configuredModel == "tiny.en") {
        candidateFiles.push_back(L"ggml-tiny.en.bin");
    } else if (configuredModel == "base.en") {
        candidateFiles.push_back(L"ggml-base.en.bin");
    } else if (configuredModel == "tiny") {
        candidateFiles.push_back(L"ggml-tiny.bin");
    } else if (configuredModel == "base") {
        candidateFiles.push_back(L"ggml-base.bin");
    }

    candidateFiles.push_back(L"ggml-tiny.en.bin");
    candidateFiles.push_back(L"ggml-base.en.bin");

    for (const auto& file : candidateFiles) {
        const std::wstring full = std::wstring(exeDir) + L"models\\" + file;
        if (FileExistsWPath(full)) {
            return WideToUtf8(full);
        }
    }

    return "";
}

// ------------------------------------------------------------------
// WindowProc
// ------------------------------------------------------------------
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Re-add tray icon after Explorer crash/restart
    if (msg == g_taskbarCreated) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }

    switch (msg) {

    // ----------------------------------------------------------
    // Initialise hotkey on window creation
    // ----------------------------------------------------------
    case WM_CREATE: {
        if (!RegisterHotKey(hwnd, HOTKEY_ID_RECORD, MOD_ALT | MOD_NOREPEAT, 'V')) {
            // Alt+V taken — try Alt+Shift+V
            if (!RegisterHotKey(hwnd, HOTKEY_ID_RECORD,
                                MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'V')) {
                MessageBoxW(hwnd,
                    L"Could not register Alt+V or Alt+Shift+V.\n"
                    L"Another application has claimed both hotkeys.\n\n"
                    L"Close that application and restart FLOW-ON!.",
                    L"FLOW-ON! \u2014 Hotkey Conflict", MB_ICONWARNING | MB_OK);
            } else {
                g_altHotkeyFallback = true;
                wcscpy_s(g_nid.szTip,
                    L"FLOW-ON! \u2014 Using Alt+Shift+V (Alt+V was taken)");
                Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            }
        }
        return 0;
    }

    // ----------------------------------------------------------
    // Hotkey press → start recording
    // ----------------------------------------------------------
    case WM_HOTKEY:
        if (wp == HOTKEY_ID_RECORD
            && !g_hotkeyDown
            && g_state.load(std::memory_order_acquire) == AppState::IDLE)
        {
            g_hotkeyDown = true;
            g_recordingActive.store(true, std::memory_order_release);
            g_state.store(AppState::RECORDING, std::memory_order_release);
            g_recordStart = std::chrono::steady_clock::now();

            g_overlay.setState(OverlayState::Recording);
            SetTrayIcon(IDI_RECORDING_ICON, L"FLOW-ON! \u2014 Recording\u2026");
            g_audio.startCapture();

            g_vadSilentFrames = 0;
            g_vadSpeechFrames = 0;

            SetTimer(hwnd, TIMER_ID_KEYCHECK, 30, nullptr);  // 30 ms poll
        }
        return 0;

    // ----------------------------------------------------------
    // Polling timer — detect Alt release while recording
    // ----------------------------------------------------------
    case WM_TIMER:
        if (wp == TIMER_ID_KEYCHECK && g_hotkeyDown) {
            // GetAsyncKeyState: high bit set = key is currently down
            bool altHeld = (GetAsyncKeyState(VK_MENU)   & 0x8000) != 0;
            bool vHeld   = (GetAsyncKeyState('V')        & 0x8000) != 0;
            bool shiftHeld = g_altHotkeyFallback
                           ? (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0
                           : true;

            if (!altHeld || !vHeld || !shiftHeld) {
                g_hotkeyDown = false;
                StopRecordingOnce(hwnd);
            }
            // Adaptive VAD: estimate local noise floor and only auto-stop
            // once real speech has occurred and then tailed off.
            else {
                const float rms = g_audio.getRMS();

                g_vadNoiseFloor = std::max(0.0015f, g_vadNoiseFloor * 0.98f + rms * 0.02f);

                const float speechThreshold  = std::max(0.0065f, g_vadNoiseFloor * 2.4f);
                const float silenceThreshold = std::max(0.0045f, g_vadNoiseFloor * 1.5f);

                if (rms >= speechThreshold) {
                    g_vadSpeechFrames++;
                    g_vadSilentFrames = 0;
                } else {
                    if (g_vadSpeechFrames >= 4 && rms <= silenceThreshold) {
                        g_vadSilentFrames++;
                    } else if (g_vadSpeechFrames == 0) {
                        // Before first speech frame, keep adapting quickly to ambient noise.
                        g_vadNoiseFloor = std::max(0.0015f, g_vadNoiseFloor * 0.9f + rms * 0.1f);
                    }
                }

                // 1500ms / 30ms per poll = ~50 consecutive silent polls.
                if (g_vadSpeechFrames >= 4 && g_vadSilentFrames > 50) {
                    g_vadSilentFrames = 0;
                    g_vadSpeechFrames = 0;
                    g_hotkeyDown = false;
                    StopRecordingOnce(hwnd);
                }
            }
        }
        if (wp == TIMER_ID_IDLECHECK) {
            if (g_state.load(std::memory_order_acquire) == AppState::IDLE
                && !g_transcriber.isBusy()) {
                g_transcriber.unloadIfIdle(
                    GetTickCount64(),
                    g_idleUnloadMs.load(std::memory_order_acquire));
            }
        }
        return 0;

    // ----------------------------------------------------------
    // Tray icon interaction
    // ----------------------------------------------------------
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP)
            ShowTrayMenu(hwnd);
        else if (lp == WM_LBUTTONDBLCLK)
            PostMessageW(hwnd, WM_SHOW_DASHBOARD, 0, 0);
        return 0;

    // ----------------------------------------------------------
    // Open dashboard
    // ----------------------------------------------------------
    case WM_SHOW_DASHBOARD:
        g_dashboard.show();
        return 0;

    // ----------------------------------------------------------
    // Drain audio and hand off to Whisper
    // ----------------------------------------------------------
    case WM_START_TRANSCRIPTION: {
        std::vector<float> pcm = g_audio.drainBuffer();

        const int dropped = g_audio.getDroppedSamples();
        g_audio.resetDropCounter();

        // Gate on a meaningful recording length (>0.15 s) and low drop rate
        const bool tooShort  = pcm.size() < 2400;   // 0.15 s at 16 kHz
        const bool tooDroppy = dropped > 160;   // >10 ms gap = noticeable corruption

        size_t voicedSamples = 0;
        const float voicedThreshold = std::max(0.0065f, g_vadNoiseFloor * 2.0f);
        for (float s : pcm) {
            if (std::fabs(s) >= voicedThreshold) {
                ++voicedSamples;
            }
        }
        const bool mostlySilence = pcm.empty() ||
            (static_cast<double>(voicedSamples) / static_cast<double>(pcm.size()) < 0.015);

        if (tooShort || tooDroppy || mostlySilence) {
            wchar_t tip[128];
            if (tooShort)
                wcscpy_s(tip, L"FLOW-ON! \u2014 Too short, try again");
            else if (mostlySilence)
                wcscpy_s(tip, L"FLOW-ON! \u2014 No clear speech detected");
            else
                swprintf_s(tip, L"FLOW-ON! \u2014 Audio capture error (%d drops)", dropped);
            g_overlay.setState(OverlayState::Error);
            g_state.store(AppState::IDLE, std::memory_order_release);
            SetTrayIcon(IDI_IDLE_ICON, tip);
            break;
        }

        // Single-flight guard in transcribeAsync prevents re-entry
        if (!g_transcriber.transcribeAsync(hwnd, std::move(pcm), WM_TRANSCRIPTION_DONE)) {
            // Whisper was still busy — silently drop and reset
            g_overlay.setState(OverlayState::Error);
            g_state.store(AppState::IDLE, std::memory_order_release);
            SetTrayIcon(IDI_IDLE_ICON, L"FLOW-ON! \u2014 Busy, try again");
        }
        break;
    }

    // ----------------------------------------------------------
    // Transcription complete — format, expand snippets, inject
    // Use a static to prevent duplicate processing of the same message
    // ----------------------------------------------------------
    case WM_TRANSCRIPTION_DONE: {
        const uint64_t tickNow = GetTickCount64();
        auto* rawPtr = reinterpret_cast<std::string*>(lp);
        std::string raw = rawPtr ? *rawPtr : "";
        delete rawPtr;

        OutputDebugStringA(("FLOW-ON RAW: " + raw + "\n").c_str());

        // Detect active window mode (code editor vs prose)
        const AppMode mode = g_config.settings().modeStr == "code"  ? AppMode::CODING
                           : g_config.settings().modeStr == "prose" ? AppMode::PROSE
                           : DetectModeFromActiveWindow();

        std::string formatted = FormatTranscription(raw, mode);
        formatted = g_snippets.apply(formatted);

        const std::string normalized = NormalizeForDedup(formatted);
        const bool duplicateRecent =
            !normalized.empty() &&
            (tickNow - g_recentTranscript.tick) < 12000 &&
            IsLikelyDuplicateText(normalized, g_recentTranscript.normalized);

        if (duplicateRecent) {
            OutputDebugStringA("FLOW-ON: Suppressed duplicate transcription chunk\n");
            g_overlay.setState(OverlayState::Done);
            g_state.store(AppState::IDLE, std::memory_order_release);
            SetTrayIcon(IDI_IDLE_ICON, L"FLOW-ON! \u2014 Idle (Alt+V to record)");
            break;
        }

        OutputDebugStringA(("FLOW-ON FMT: " + formatted + "\n").c_str());

        // Measure latency
        const auto now = std::chrono::steady_clock::now();
        const int latMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_recordStart).count());

        if (!formatted.empty()) {
            // Proper UTF-8 → Wide conversion (handles multi-byte correctly)
            int wlen = MultiByteToWideChar(CP_UTF8, 0, formatted.c_str(), -1, nullptr, 0);
            std::wstring wide;
            if (wlen > 1) {
                wide.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, formatted.c_str(), -1, wide.data(), wlen);
            }
            g_state.store(AppState::INJECTING, std::memory_order_release);
            InjectText(wide);

            g_recentTranscript.normalized = normalized;
            g_recentTranscript.tick = tickNow;
        }

        g_overlay.setState(OverlayState::Done);
        g_state.store(AppState::IDLE, std::memory_order_release);
        SetTrayIcon(IDI_IDLE_ICON, L"FLOW-ON! \u2014 Idle (Alt+V to record)");

        // Record in dashboard history
        if (!formatted.empty()) {
            TranscriptionEntry entry;
            entry.text      = formatted;
            entry.latencyMs = latMs;
            entry.wasCoded  = (mode == AppMode::CODING);
            {
                // Build timestamp "HH:MM"
                SYSTEMTIME st; GetLocalTime(&st);
                wchar_t ts[8];
                swprintf_s(ts, L"%02d:%02d", st.wHour, st.wMinute);
                entry.timestamp = std::string(ts, ts + 5);
            }
            // Count words
            entry.wordCount = 0;
            bool inWord = false;
            for (char c : formatted) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (inWord) entry.wordCount++;
                    inWord = false;
                } else {
                    inWord = true;
                }
            }
            if (inWord) entry.wordCount++;
            g_dashboard.addEntry(entry);
        }

        OutputDebugStringA(("FLOW-ON LATENCY: " + std::to_string(latMs) + " ms\n").c_str());
        break;
    }

    // ----------------------------------------------------------
    // Cleanup on exit
    // ----------------------------------------------------------
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID_KEYCHECK);
        KillTimer(hwnd, TIMER_ID_IDLECHECK);
        UnregisterHotKey(hwnd, HOTKEY_ID_RECORD);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;

    } // switch

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------
// WinMain
// ------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Set CWD to the directory containing the executable so "models/…"
    // relative paths resolve correctly on all launch methods.
    {
        wchar_t exeDir[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        SetCurrentDirectoryW(exeDir);
    }

    // Enable modern Common Controls (required for Dashboard Win32 fallback)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    // Critical: register BEFORE creating the tray icon so the icon
    // auto-reappears if Explorer crashes and restarts.
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // ----------------------------------------------------------
    // Hidden message-only window (owns tray, hotkey, messages)
    // ----------------------------------------------------------
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = WindowProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"FLOWON_HIDDEN";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        0, L"FLOWON_HIDDEN", L"",
        WS_POPUP,              // No caption, no border — truly invisible
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        MessageBoxW(nullptr, L"Failed to create message window.",
                    L"FLOW-ON!", MB_ICONERROR);
        return 1;
    }

    // ----------------------------------------------------------
    // Load config
    // ----------------------------------------------------------
    g_config.load();
    g_snippets.setSnippets(g_config.settings().snippets);
    g_idleUnloadMs.store(ClampIdleUnloadMs(g_config.settings().idleUnloadSec),
                         std::memory_order_release);
    if (g_config.settings().startWithWindows) {
        wchar_t exeFull[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exeFull, MAX_PATH);
        g_config.applyAutostart(exeFull);
    }

    // ----------------------------------------------------------
    // System tray icon
    // ----------------------------------------------------------
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    {
        HICON hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_IDLE_ICON));
        if (!hIcon) hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        g_nid.hIcon = hIcon;
    }
    wcscpy_s(g_nid.szTip, L"FLOW-ON! \u2014 Idle (Alt+V to record)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // ----------------------------------------------------------
    // Audio manager (Phase 2)
    // ----------------------------------------------------------
    if (!g_audio.init(nullptr)) {
        MessageBoxW(nullptr,
            L"Failed to open microphone.\n\n"
            L"Make sure a microphone is connected and privacy settings\n"
            L"allow app access to the microphone.",
            L"FLOW-ON! \u2014 Audio Error", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 1;
    }

    // ----------------------------------------------------------
    // Direct2D overlay (Phase 7)
    // ----------------------------------------------------------
    if (!g_overlay.init(hInst)) {
        MessageBoxW(nullptr,
            L"Failed to initialise Direct2D overlay.\n"
            L"Ensure your display driver supports Direct2D.",
            L"FLOW-ON! \u2014 Overlay Error", MB_ICONWARNING);
        // Non-fatal: continue without overlay
    } else {
        g_overlayPtr = &g_overlay;
    }

    // ----------------------------------------------------------
    // Whisper transcriber (Phase 5)
    // ----------------------------------------------------------
    const std::string modelPath = ResolveModelPath(g_config.settings().model);
    if (modelPath.empty()) {
        MessageBoxW(nullptr,
            L"No Whisper model found in:\n"
            L"  <exe-dir>\\models\\\n\n"
            L"Expected one of:\n"
            L"  ggml-tiny.en.bin\n"
            L"  ggml-base.en.bin\n\n"
            L"Download one with:\n"
            L"  external\\whisper.cpp\\models\\download-ggml-model.cmd tiny.en",
            L"FLOW-ON! — Model Not Found", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_audio.shutdown();
        g_overlay.shutdown();
        return 1;
    }

    // Keep baseline RAM low by loading the model only when transcription starts.
    g_transcriber.setModelPath(modelPath);
    g_transcriber.setUseGPU(g_config.settings().useGPU);

    SetTimer(g_hwnd, TIMER_ID_IDLECHECK, 30000, nullptr);

    // ----------------------------------------------------------
    // Dashboard (Phase 8)
    // ----------------------------------------------------------
    g_dashboard.init(hInst, g_hwnd);
    
    // Initialize dashboard settings from config
    g_dashboard.m_settings.useGPU = g_config.settings().useGPU;
    g_dashboard.m_settings.startWithWindows = g_config.settings().startWithWindows;
    g_dashboard.m_settings.idleUnloadSec = g_config.settings().idleUnloadSec;
    g_dashboard.m_settings.enableOverlay = true;
    
    g_dashboard.onSettingsChanged = [](const DashboardSettings& ds) {
        // Apply settings changes from the dashboard UI
        g_config.settings().useGPU           = ds.useGPU;
        g_config.settings().startWithWindows = ds.startWithWindows;
        g_config.settings().idleUnloadSec    = ds.idleUnloadSec;
        g_idleUnloadMs.store(ClampIdleUnloadMs(ds.idleUnloadSec), std::memory_order_release);
        g_transcriber.setUseGPU(ds.useGPU);
        g_config.save();
        if (ds.startWithWindows) {
            wchar_t exeFull[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exeFull, MAX_PATH);
            g_config.applyAutostart(exeFull);
        } else {
            g_config.removeAutostart();
        }
    };
    
    g_dashboard.onClearHistoryRequested = []() {
        // Clear history in config if needed
        OutputDebugStringA("FLOW-ON: History cleared from dashboard\n");
    };

    // ----------------------------------------------------------
    // Message loop
    // ----------------------------------------------------------
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ----------------------------------------------------------
    // Graceful shutdown
    // ----------------------------------------------------------
    // Zero the PCM buffer before freeing — prevents secrets lingering in RAM
    g_audio.stopCapture();
    {
        auto buf = g_audio.drainBuffer();
        SecureZeroMemory(buf.data(), buf.size() * sizeof(float));
    }
    g_audio.shutdown();
    g_transcriber.shutdown();
    g_overlay.shutdown();
    g_dashboard.shutdown();
    g_config.save();

    return static_cast<int>(msg.wParam);
}
