// dashboard.cpp — WinUI 3 Dashboard (Phase 8)
//
// This file compiles in two modes:
//
//   1. With ENABLE_WINUI3_DASHBOARD defined (and the Microsoft.WindowsAppSDK
//      NuGet package installed in the VS project):
//      → Full WinUI 3 window with Mica backdrop, TabView, history list.
//
//   2. Without ENABLE_WINUI3_DASHBOARD (default until NuGet is installed):
//      → Lightweight Win32 fallback dialog showing history in a ListBox.
//
// To enable WinUI 3:
//   • In Visual Studio: Project → Manage NuGet Packages → install Microsoft.WindowsAppSDK (≥1.5)
//   • Add ENABLE_WINUI3_DASHBOARD to Project → Properties → C/C++ → Preprocessor Definitions.

#include <windows.h>
#include <commctrl.h>
#include "dashboard.h"
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

// ------------------------------------------------------------------
// Shared state visible to both implementations
// ------------------------------------------------------------------
static std::atomic<bool>  g_dashVisible{false};
static std::atomic<HWND>  g_dashHwnd{nullptr};

// ------------------------------------------------------------------
#ifdef ENABLE_WINUI3_DASHBOARD
// ------------------------------------------------------------------
// Full WinUI 3 implementation
// ------------------------------------------------------------------
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <MddBootstrap.h>

static winrt::Microsoft::UI::Dispatching::DispatcherQueue g_dispatcherQueue{nullptr};

bool Dashboard::init(HINSTANCE, HWND ownerHwnd)
{
    m_ownerHwnd   = ownerHwnd;
    m_initialized = true;
    return true;
}

void Dashboard::addEntry(const TranscriptionEntry& e)
{
    {
        std::lock_guard<std::mutex> lock(m_histMutex);
        m_history.push_back(e);
        if (m_history.size() > 200)
            m_history.erase(m_history.begin());
    }
    if (g_dashVisible && g_dispatcherQueue) {
        auto copy = e;
        g_dispatcherQueue.TryEnqueue([copy]() {
            // UI-thread update: wire to the captured ListView control reference
            // if you move the window object to module scope.
            (void)copy;
        });
    }
}

void Dashboard::show()
{
    if (g_dashVisible) {
        if (g_dispatcherQueue) {
            g_dispatcherQueue.TryEnqueue([]() {
                // Bring-to-front if window reference is captured
            });
        }
        return;
    }
    std::thread([this]{ launchOnThread(); }).detach();
}

void Dashboard::launchOnThread()
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    g_dashVisible = true;

    auto controller = winrt::Microsoft::UI::Dispatching::
        DispatcherQueueController::CreateOnCurrentThread();
    g_dispatcherQueue = controller.DispatcherQueue();

    // Capture history snapshot for initial population
    std::vector<TranscriptionEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_histMutex);
        snapshot = m_history;
    }

    auto window = winrt::Microsoft::UI::Xaml::Window();

    // Mica frosted-glass backdrop (Windows 11 only; silently no-ops on Win10)
    try {
        winrt::Microsoft::UI::Xaml::Media::MicaBackdrop mica;
        window.SystemBackdrop(mica);
    } catch (...) {}

    auto appWindow = window.AppWindow();
    appWindow.Resize({ 760, 580 });
    appWindow.Title(L"FLOW-ON! Dashboard");

    // ---- Build UI ----
    auto grid = winrt::Microsoft::UI::Xaml::Controls::Grid();

    auto tabs = winrt::Microsoft::UI::Xaml::Controls::TabView();
    tabs.IsAddTabButtonVisible(false);
    tabs.TabWidthMode(
        winrt::Microsoft::UI::Xaml::Controls::TabViewWidthMode::Equal);

    // History tab
    {
        auto item    = winrt::Microsoft::UI::Xaml::Controls::TabViewItem();
        item.Header(winrt::box_value(L"History"));
        auto list    = winrt::Microsoft::UI::Xaml::Controls::ListView();
        for (auto& e : snapshot) {
            auto tb = winrt::Microsoft::UI::Xaml::Controls::TextBlock();
            std::wstring line(e.text.begin(), e.text.end());
            tb.Text(line);
            list.Items().Append(tb);
        }
        item.Content(list);
        tabs.TabItems().Append(item);
    }

    // Settings tab
    {
        auto item = winrt::Microsoft::UI::Xaml::Controls::TabViewItem();
        item.Header(winrt::box_value(L"Settings"));
        auto tb   = winrt::Microsoft::UI::Xaml::Controls::TextBlock();
        tb.Text(L"Settings — configure via %APPDATA%\\FLOW-ON\\settings.json");
        item.Content(tb);
        tabs.TabItems().Append(item);
    }

    // About tab
    {
        auto item = winrt::Microsoft::UI::Xaml::Controls::TabViewItem();
        item.Header(winrt::box_value(L"About"));
        auto tb   = winrt::Microsoft::UI::Xaml::Controls::TextBlock();
        tb.Text(L"FLOW-ON! v1.0 — Local voice-to-text for Windows developers.\n"
                L"C++20 · whisper.cpp · miniaudio · Direct2D · WinUI 3 · NSIS\n\n"
                L"Zero cloud. Zero subscription. Zero data leaves your machine.");
        tb.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        item.Content(tb);
        tabs.TabItems().Append(item);
    }

    grid.Children().Append(tabs);
    window.Content(grid);

    window.Closed([](auto&, auto&) {
        g_dashVisible     = false;
        g_dispatcherQueue = nullptr;
    });

    window.Activate();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_dashVisible = false;
}

void Dashboard::shutdown() {}

std::vector<TranscriptionEntry> Dashboard::snapshotHistory() const
{
    std::lock_guard<std::mutex> lock(m_histMutex);
    return m_history;
}

void Dashboard::clearHistory()
{
    std::lock_guard<std::mutex> lock(m_histMutex);
    m_history.clear();
}

// ------------------------------------------------------------------
#else  // ENABLE_WINUI3_DASHBOARD not defined — Win32 fallback
// ------------------------------------------------------------------

static constexpr int IDC_LISTBOX  = 1001;
static constexpr int IDC_CLEAR    = 1002;
static constexpr int IDC_COPYLAST = 1003;

static Dashboard*   g_dashInst  = nullptr;
static HWND         g_listHwnd  = nullptr;

static LRESULT CALLBACK DashWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        g_listHwnd = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            8, 8, rc.right - 16, rc.bottom - 50,
            hwnd, (HMENU)(INT_PTR)IDC_LISTBOX, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Clear",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            8, rc.bottom - 38, 90, 28,
            hwnd, (HMENU)(INT_PTR)IDC_CLEAR, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Copy Last",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            106, rc.bottom - 38, 100, 28,
            hwnd, (HMENU)(INT_PTR)IDC_COPYLAST, nullptr, nullptr);

        // Populate list from existing history
        if (g_dashInst) {
            std::lock_guard<std::mutex> lock(g_dashInst->m_histMutex);
            for (auto& e : g_dashInst->m_history) {
                std::wstring item(e.text.begin(), e.text.end());
                SendMessageW(g_listHwnd, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(item.c_str()));
            }
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CLEAR && g_listHwnd) {
            SendMessageW(g_listHwnd, LB_RESETCONTENT, 0, 0);
            if (g_dashInst) {
                std::lock_guard<std::mutex> lock(g_dashInst->m_histMutex);
                g_dashInst->m_history.clear();
            }
        } else if (LOWORD(wp) == IDC_COPYLAST && g_listHwnd) {
            int cnt = static_cast<int>(SendMessageW(g_listHwnd, LB_GETCOUNT, 0, 0));
            if (cnt > 0) {
                int len = static_cast<int>(
                    SendMessageW(g_listHwnd, LB_GETTEXTLEN, cnt - 1, 0));
                if (len > 0) {
                    std::wstring buf(len + 1, L'\0');
                    SendMessageW(g_listHwnd, LB_GETTEXT, cnt - 1,
                                 reinterpret_cast<LPARAM>(buf.data()));
                    buf.resize(len);
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE,
                            (buf.size() + 1) * sizeof(wchar_t));
                        if (h) {
                            memcpy(GlobalLock(h), buf.c_str(),
                                   (buf.size() + 1) * sizeof(wchar_t));
                            GlobalUnlock(h);
                            SetClipboardData(CF_UNICODETEXT, h);
                        }
                        CloseClipboard();
                    }
                }
            }
        }
        return 0;
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        if (g_listHwnd)
            SetWindowPos(g_listHwnd, nullptr, 8, 8,
                         rc.right - 16, rc.bottom - 50, SWP_NOZORDER);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        g_dashVisible = false;
        g_dashHwnd    = nullptr;
        return 0;
    case WM_DESTROY:
        g_dashVisible = false;
        g_dashHwnd    = nullptr;
        g_listHwnd    = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Dashboard::init(HINSTANCE hInst, HWND ownerHwnd)
{
    g_dashInst    = this;
    m_ownerHwnd   = ownerHwnd;
    m_initialized = true;

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = DashWndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground= reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName= L"FLOWON_DASHBOARD";
    wc.hIcon        = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    return true;
}

void Dashboard::show()
{
    if (g_dashVisible && g_dashHwnd) {
        SetForegroundWindow(static_cast<HWND>(g_dashHwnd));
        return;
    }

    HWND hwnd = CreateWindowExW(
        0, L"FLOWON_DASHBOARD", L"FLOW-ON! Dashboard",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 540,
        nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);

    if (!hwnd) return;

    g_dashHwnd    = hwnd;
    g_dashVisible = true;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

std::vector<TranscriptionEntry> Dashboard::snapshotHistory() const
{
    std::lock_guard<std::mutex> lock(m_histMutex);
    return m_history;
}

void Dashboard::clearHistory()
{
    std::lock_guard<std::mutex> lock(m_histMutex);
    m_history.clear();
}

void Dashboard::addEntry(const TranscriptionEntry& e)
{
    {
        std::lock_guard<std::mutex> lock(m_histMutex);
        m_history.push_back(e);
        if (m_history.size() > 200)
            m_history.erase(m_history.begin());
    }
    // addEntry is called from the main thread (WM_TRANSCRIPTION_DONE),
    // so SendMessage (synchronous) is safe here — no lifetime issues.
    if (g_dashVisible && g_listHwnd) {
        std::wstring item(e.text.begin(), e.text.end());
        SendMessageW(g_listHwnd, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
        // Auto-scroll to the newest entry
        int cnt = static_cast<int>(SendMessageW(g_listHwnd, LB_GETCOUNT, 0, 0));
        if (cnt > 0)
            SendMessageW(g_listHwnd, LB_SETTOPINDEX, cnt - 1, 0);
    }
}

void Dashboard::shutdown()
{
    if (g_dashHwnd) {
        DestroyWindow(static_cast<HWND>(g_dashHwnd));
        g_dashHwnd = nullptr;
    }
    g_dashVisible = false;
}

// launchOnThread() is only used by the WinUI 3 path.
// Provide a no-op stub so the linker is satisfied in Win32 fallback mode.
void Dashboard::launchOnThread() {}

#endif  // ENABLE_WINUI3_DASHBOARD
