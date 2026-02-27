#pragma once
// -----------------------------------------------------------------
// dashboard.h — WinUI 3 / Windows App SDK dashboard bridge
//
// COMPILATION REQUIREMENT:
//   This module requires the Microsoft.WindowsAppSDK NuGet package
//   (v1.5+). In Visual Studio:
//     Project → Manage NuGet Packages → Browse → Microsoft.WindowsAppSDK
//   The NuGet package provides the winrt/Microsoft.UI.* headers and the
//   bootstrap lib automatically via the .props/.targets injection.
//
//   When building via CMake, add the NuGet-installed include/lib paths
//   manually, or use the VS-generated .sln from CMake and install the
//   package there.
// -----------------------------------------------------------------

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <windows.h>

struct TranscriptionEntry {
    std::string text;
    std::string timestamp;   // "HH:MM"
    int         latencyMs  = 0;
    bool        wasCoded   = false;
};

struct DashboardSettings {
    bool useGPU           = true;
    bool startWithWindows = true;
    int  modelChoice      = 0;     // 0 = tiny.en, 1 = base.en
};

class Dashboard {
public:
    // Call once from WinMain.  ownerHwnd receives WM_SHOW_DASHBOARD.
    bool init(HINSTANCE hInst, HWND ownerHwnd);
    void shutdown();

    // Thread-safe: add a history entry; updates UI if dashboard is open.
    void addEntry(const TranscriptionEntry& e);

    // Open (or bring to front) the WinUI 3 window (or Win32 fallback).
    // Safe to call from any thread — dispatches internally.
    void show();

    // Returns a snapshot of the full history (caller holds no locks).
    std::vector<TranscriptionEntry> snapshotHistory() const;

    // Clears the in-memory history list.
    void clearHistory();

    // Fired on the main thread when user saves settings.
    std::function<void(const DashboardSettings&)> onSettingsChanged;

    // Mutex and history are public so the Win32 DashWndProc friend can
    // access them directly.  Treat as private in all other contexts.
    mutable std::mutex              m_histMutex;
    std::vector<TranscriptionEntry> m_history;

private:
    HWND  m_ownerHwnd    = nullptr;
    bool  m_initialized  = false;

    void launchOnThread();
};
