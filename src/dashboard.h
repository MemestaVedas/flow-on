#pragma once
// -----------------------------------------------------------------
// dashboard.h — Modern Dashboard with Tabbed Interface
//
// Features:
//   - Tab-based navigation (History, Statistics, Settings)
//   - Modern glassmorphism dark theme
//   - Real-time metrics and usage statistics
//   - Integrated settings panel
// -----------------------------------------------------------------

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <windows.h>

struct TranscriptionEntry {
    std::string text;
    std::string timestamp;   // "HH:MM"
    int         latencyMs  = 0;
    bool        wasCoded   = false;
    int         wordCount  = 0;
};

struct DashboardSettings {
    bool useGPU           = true;
    bool startWithWindows = true;
    int  modelChoice      = 0;     // 0 = tiny.en, 1 = base.en
    bool enableOverlay    = true;
    int  audioThreshold   = 8;     // 0-100
};

struct UsageStats {
    int totalTranscriptions = 0;
    int totalWords          = 0;
    int totalCharacters     = 0;
    int avgLatencyMs        = 0;
    int todayCount          = 0;
    int sessionCount        = 0;
    std::string lastUsed;
};

class Dashboard {
public:
    // Call once from WinMain.  ownerHwnd receives WM_SHOW_DASHBOARD.
    bool init(HINSTANCE hInst, HWND ownerHwnd);
    void shutdown();

    // Thread-safe: add a history entry; updates UI if dashboard is open.
    void addEntry(const TranscriptionEntry& e);

    // Open (or bring to front) the dashboard window.
    void show();
    void hide();
    bool isVisible() const;

    // Returns a snapshot of the full history (caller holds no locks).
    std::vector<TranscriptionEntry> snapshotHistory() const;

    // Clears the in-memory history list.
    void clearHistory();

    // Get current usage statistics.
    UsageStats getStats() const;

    // Update settings from UI.
    void updateSettings(const DashboardSettings& settings);

    // Fired on the main thread when user saves settings.
    std::function<void(const DashboardSettings&)> onSettingsChanged;

    // Fired when user requests history clear.
    std::function<void()> onClearHistoryRequested;

    // Mutex and history are public so the Win32 DashWndProc friend can
    // access them directly.  Treat as private in all other contexts.
    mutable std::mutex              m_histMutex;
    std::vector<TranscriptionEntry> m_history;
    DashboardSettings               m_settings;

private:
    HWND  m_ownerHwnd    = nullptr;
    bool  m_initialized  = false;

    void launchOnThread();
};
