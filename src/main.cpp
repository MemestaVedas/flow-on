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
#define TIMER_ID_KEYCHECK      2   // 50 ms poll for Alt key release during recording

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

// Timing: used to measure transcription latency for the history entry
static std::chrono::steady_clock::time_point g_recordStart;

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
static std::string GetModelPath()
{
    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    // Strip filename
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    std::wstring model = std::wstring(exeDir) + L"models\\ggml-tiny.en.bin";
    // Convert to narrow string (ASCII-safe path)
    return std::string(model.begin(), model.end());
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

            // Poll every 50 ms for Alt key release (more reliable than WM_KEYUP
            // on a hidden window)
            SetTimer(hwnd, TIMER_ID_KEYCHECK, 50, nullptr);
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

        // Gate on a meaningful recording length (>0.25 s) and low drop rate
        const bool tooShort  = pcm.size() < 16000 / 4;
        const bool tooDroppy = dropped > 160;   // >10 ms gap = noticeable corruption

        if (tooShort || tooDroppy) {
            wchar_t tip[128];
            if (tooShort)
                wcscpy_s(tip, L"FLOW-ON! \u2014 Too short, try again");
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
    // ----------------------------------------------------------
    case WM_TRANSCRIPTION_DONE: {
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

        OutputDebugStringA(("FLOW-ON FMT: " + formatted + "\n").c_str());

        // Measure latency
        const auto now = std::chrono::steady_clock::now();
        const int latMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_recordStart).count());

        if (!formatted.empty()) {
            std::wstring wide(formatted.begin(), formatted.end());
            g_state.store(AppState::INJECTING, std::memory_order_release);
            InjectText(wide);
        }

        g_overlay.setState(OverlayState::Done);
        g_state.store(AppState::IDLE, std::memory_order_release);
        SetTrayIcon(IDI_IDLE_ICON, L"FLOW-ON! \u2014 Idle (Alt+V to record)");

        // Record in dashboard history
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
        g_dashboard.addEntry(entry);

        OutputDebugStringA(("FLOW-ON LATENCY: " + std::to_string(latMs) + " ms\n").c_str());
        break;
    }

    // ----------------------------------------------------------
    // Cleanup on exit
    // ----------------------------------------------------------
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID_KEYCHECK);
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
    const std::string modelPath = GetModelPath();
    if (!g_transcriber.init(modelPath.c_str())) {
        MessageBoxW(nullptr,
            L"Failed to load Whisper model.\n\n"
            L"Expected location:\n"
            L"  <exe-dir>\\models\\ggml-tiny.en.bin\n\n"
            L"Download it with:\n"
            L"  external\\whisper.cpp\\models\\download-ggml-model.cmd tiny.en",
            L"FLOW-ON! \u2014 Model Not Found", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_audio.shutdown();
        g_overlay.shutdown();
        return 1;
    }

    // ----------------------------------------------------------
    // Dashboard (Phase 8)
    // ----------------------------------------------------------
    g_dashboard.init(hInst, g_hwnd);
    g_dashboard.onSettingsChanged = [](const DashboardSettings& ds) {
        // Apply settings changes from the dashboard UI
        g_config.settings().useGPU           = ds.useGPU;
        g_config.settings().startWithWindows = ds.startWithWindows;
        g_config.save();
        if (ds.startWithWindows) {
            wchar_t exeFull[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exeFull, MAX_PATH);
            g_config.applyAutostart(exeFull);
        } else {
            g_config.removeAutostart();
        }
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
