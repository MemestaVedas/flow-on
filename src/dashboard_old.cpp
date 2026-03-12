// dashboard.cpp — Modern Direct2D dashboard with glassmorphism
//
// Replaces the old Win32 ListBox with a polished, animated Direct2D UI.
// Features: smooth list animations, dark theme, professional layout, rich history display.

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "dashboard.h"
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

static constexpr int FPS_MS = 16;
static constexpr int TIMER_ID = 99;

static std::atomic<bool>  g_dashVisible{false};
static std::atomic<HWND>  g_dashHwnd{nullptr};
static Dashboard*         g_dashInst = nullptr;

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static inline float easeOut(float t) { float u = 1.f - t; return 1.f - u * u * u; }

// ==================================================================
// Modern Direct2D Dashboard Window
// ==================================================================

class ModernDashboard {
public:
    ModernDashboard() = default;
    ~ModernDashboard() { cleanup(); }

    bool init(HINSTANCE hInst);
    void show();
    void addEntry(const TranscriptionEntry& e);
    void cleanup();
    void onTimer();
    void onPaint();

private:
    static constexpr int ITEM_H      = 72;
    static constexpr int HEADER_H    = 60;
    static constexpr int PADDING     = 16;
    static constexpr int HIST_START  = HEADER_H + PADDING;

    HWND m_hwnd = nullptr;
    HDC m_memDC = nullptr;
    HBITMAP m_hBitmap = nullptr;

    ID2D1Factory* m_d2dFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRT = nullptr;
    IDWriteFactory* m_dwFactory = nullptr;
    IDWriteTextFormat* m_titleFmt = nullptr;
    IDWriteTextFormat* m_textFmt = nullptr;
    IDWriteTextFormat* m_smallFmt = nullptr;

    // Window dimensions
    int m_winW = 840;
    int m_winH = 600;

    // List animation state
    struct HistItem {
        TranscriptionEntry entry;
        float fadeIn = 0.0f;      // [0,1]
        float slideIn = -20.0f;   // offset
    };
    std::vector<HistItem> m_items;
    float m_scrollY = 0.0f;
    int m_visibleItems = 0;

    bool createGDIResources();
    void releaseGDIResources();
    bool createD2DResources();
    void releaseD2DResources();
    void draw();
    void present();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

static ModernDashboard* g_dashboardUI = nullptr;

bool ModernDashboard::init(HINSTANCE hInst)
{
    if (!createGDIResources() || !createD2DResources())
        return false;

    WNDCLASSEXW wc = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.lpszClassName = L"FLOWON_MODERN_DASHBOARD";
    wc.hCursor      = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, L"FLOWON_MODERN_DASHBOARD", L"FLOW-ON! Dashboard",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, m_winW, m_winH,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetTimer(m_hwnd, TIMER_ID, FPS_MS, nullptr);
    return true;
}

bool ModernDashboard::createGDIResources()
{
    releaseGDIResources();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_winW;
    bmi.bmiHeader.biHeight = -m_winH;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    void* pBits = nullptr;
    m_hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);

    m_memDC = CreateCompatibleDC(nullptr);
    SelectObject(m_memDC, m_hBitmap);
    return m_hBitmap != nullptr && m_memDC != nullptr;
}

void ModernDashboard::releaseGDIResources()
{
    if (m_memDC) { DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_hBitmap) { DeleteObject(m_hBitmap); m_hBitmap = nullptr; }
}

bool ModernDashboard::createD2DResources()
{
    if (m_dcRT) { m_dcRT->Release(); m_dcRT = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
    if (m_dwFactory) { m_dwFactory->Release(); m_dwFactory = nullptr; }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory)))
        return false;

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));

    if (m_dwFactory) {
        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-US", &m_titleFmt);

        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-US", &m_textFmt);

        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &m_smallFmt);
        if (m_smallFmt) {
            m_smallFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    return SUCCEEDED(m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRT));
}

void ModernDashboard::releaseD2DResources()
{
    if (m_smallFmt) { m_smallFmt->Release(); m_smallFmt = nullptr; }
    if (m_textFmt) { m_textFmt->Release(); m_textFmt = nullptr; }
    if (m_titleFmt) { m_titleFmt->Release(); m_titleFmt = nullptr; }
    if (m_dcRT) { m_dcRT->Release(); m_dcRT = nullptr; }
    if (m_dwFactory) { m_dwFactory->Release(); m_dwFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

void ModernDashboard::cleanup()
{
    if (m_hwnd) { KillTimer(m_hwnd, TIMER_ID); DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    releaseD2DResources();
    releaseGDIResources();
}

void ModernDashboard::addEntry(const TranscriptionEntry& e)
{
    HistItem item;
    item.entry = e;
    item.fadeIn = 0.0f;
    item.slideIn = -20.0f;
    m_items.insert(m_items.begin(), item);  // newest at top
    if (m_items.size() > 100)
        m_items.pop_back();
}

void ModernDashboard::onTimer()
{
    // Animate list items
    for (auto& item : m_items) {
        if (item.fadeIn < 1.0f) item.fadeIn += 0.15f;
        if (item.slideIn < 0.0f) item.slideIn += 1.5f;
    }
    m_visibleItems = std::min(static_cast<int>(m_items.size()), (m_winH - HIST_START - PADDING) / ITEM_H);
    
    // Render to offscreen buffer
    draw();
    
    // Trigger WM_PAINT to blit to screen
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ModernDashboard::draw()
{
    if (!m_dcRT || !m_memDC) return;

    RECT rc = {0, 0, m_winW, m_winH};
    if (FAILED(m_dcRT->BindDC(m_memDC, &rc))) return;

    m_dcRT->BeginDraw();
    
    // ---- Background ----
    m_dcRT->Clear(D2D1::ColorF(0.053f, 0.055f, 0.082f, 1.0f));

    // ---- Header background ----
    const D2D1_RECT_F headerRect = {0, 0, static_cast<float>(m_winW), static_cast<float>(HEADER_H)};
    ID2D1SolidColorBrush* hdBrush = nullptr;
    m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.04f, 0.06f, 1.0f), &hdBrush);
    if (hdBrush) { m_dcRT->FillRectangle(headerRect, hdBrush); hdBrush->Release(); }

    // Header title
    if (m_titleFmt) {
        ID2D1SolidColorBrush* txtBrush = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &txtBrush);
        if (txtBrush) {
            const D2D1_RECT_F trect = {PADDING, 12.0f, static_cast<float>(m_winW - PADDING), static_cast<float>(HEADER_H)};
            m_dcRT->DrawTextW(L"Recent Transcriptions", 20, m_titleFmt, trect, txtBrush);
            txtBrush->Release();
        }
    }

    // ---- List items ----
    const float listTop = static_cast<float>(HIST_START);
    const float listBottom = static_cast<float>(m_winH - PADDING);
    const float itemW = static_cast<float>(m_winW - 2 * PADDING);

    for (int i = 0; i < m_visibleItems && i < static_cast<int>(m_items.size()); ++i) {
        auto& item = m_items[i];
        const float y = listTop + i * ITEM_H + item.slideIn * easeOut(item.fadeIn);
        const float itemOpacity = easeOut(clamp01(item.fadeIn));

        // Skip if off-screen
        if (y + ITEM_H < listTop || y > listBottom) continue;

        // Item background
        const D2D1_ROUNDED_RECT itemBg = {
            D2D1::RectF(PADDING, y, PADDING + itemW, y + ITEM_H - 4), 8.0f, 8.0f
        };
        ID2D1SolidColorBrush* itemBrush = nullptr;
        m_dcRT->CreateSolidColorBrush(
            D2D1::ColorF(0.10f, 0.11f, 0.16f, 0.7f * itemOpacity), &itemBrush);
        if (itemBrush) { m_dcRT->FillRoundedRectangle(itemBg, itemBrush); itemBrush->Release(); }

        // Item border
        ID2D1SolidColorBrush* bordBrush = nullptr;
        m_dcRT->CreateSolidColorBrush(
            D2D1::ColorF(0.5f, 0.5f, 0.6f, 0.15f * itemOpacity), &bordBrush);
        if (bordBrush) { m_dcRT->DrawRoundedRectangle(itemBg, bordBrush, 1.0f); bordBrush->Release(); }

        // Text content
        if (m_textFmt) {
            ID2D1SolidColorBrush* txtBrush = nullptr;
            m_dcRT->CreateSolidColorBrush(
                D2D1::ColorF(0.92f, 0.92f, 0.95f, itemOpacity), &txtBrush);
            if (txtBrush) {
                std::wstring text(item.entry.text.begin(), item.entry.text.end());
                if (text.length() > 100) text = text.substr(0, 97) + L"…";

                const D2D1_RECT_F textRect = {PADDING + 12.0f, y + 8.0f, PADDING + itemW - 80.0f, y + ITEM_H - 4};
                m_dcRT->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_textFmt, textRect, txtBrush);
                txtBrush->Release();
            }
        }

        // Latency badge (right side)
        if (item.entry.latencyMs > 0 && m_smallFmt) {
            wchar_t latStr[32];
            swprintf_s(latStr, L"%d ms", item.entry.latencyMs);
            ID2D1SolidColorBrush* latBrush = nullptr;
            m_dcRT->CreateSolidColorBrush(
                D2D1::ColorF(0.545f, 0.361f, 0.965f, 0.8f * itemOpacity), &latBrush);
            if (latBrush) {
                const D2D1_RECT_F latRect = {PADDING + itemW - 70.0f, y + 14.0f, PADDING + itemW - 8.0f, y + ITEM_H - 20};
                m_dcRT->DrawTextW(latStr, static_cast<UINT32>(wcslen(latStr)), m_smallFmt, latRect, latBrush);
                latBrush->Release();
            }
        }
    }

    // ---- Empty state ----
    if (m_items.empty() && m_textFmt) {
        ID2D1SolidColorBrush* emptyBrush = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.6f, 0.65f, 0.5f), &emptyBrush);
        if (emptyBrush) {
            const D2D1_RECT_F emptyRect = {PADDING, HIST_START + 100.0f,
                                            static_cast<float>(m_winW - PADDING), static_cast<float>(m_winH - PADDING)};
            m_dcRT->DrawTextW(L"Start transcribing to see your history here", 43, m_textFmt, emptyRect, emptyBrush);
            emptyBrush->Release();
        }
    }

    m_dcRT->EndDraw();
    present();
}

void ModernDashboard::present()
{
    // Blit the offscreen buffer to the window DC
    HDC windowDC = GetDC(m_hwnd);
    if (windowDC) {
        BitBlt(windowDC, 0, 0, m_winW, m_winH, m_memDC, 0, 0, SRCCOPY);
        ReleaseDC(m_hwnd, windowDC);
    }
}

void ModernDashboard::show()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
    }
}

LRESULT CALLBACK ModernDashboard::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ModernDashboard* pThis = nullptr;
    if (msg == WM_CREATE) {
        pThis = reinterpret_cast<ModernDashboard*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ModernDashboard*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!pThis) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_ID) {
            pThis->onTimer();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        // BitBlt the rendered offscreen buffer to the window
        if (pThis->m_memDC && ps.hdc) {
            BitBlt(ps.hdc, 0, 0, pThis->m_winW, pThis->m_winH,
                   pThis->m_memDC, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        g_dashVisible = false;
        g_dashHwnd = nullptr;
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        g_dashVisible = false;
        g_dashHwnd = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ==================================================================
// Dashboard API implementation
// ==================================================================

bool Dashboard::init(HINSTANCE hInst, HWND ownerHwnd)
{
    m_ownerHwnd = ownerHwnd;
    m_initialized = true;

    g_dashboardUI = new ModernDashboard();
    if (!g_dashboardUI->init(hInst)) {
        delete g_dashboardUI;
        g_dashboardUI = nullptr;
        return false;
    }
    g_dashInst = this;
    return true;
}

void Dashboard::show()
{
    if (!g_dashboardUI) return;
    g_dashVisible = true;
    g_dashHwnd = nullptr;
    g_dashboardUI->show();
}

void Dashboard::shutdown()
{
    if (g_dashboardUI) {
        g_dashboardUI->cleanup();
        delete g_dashboardUI;
        g_dashboardUI = nullptr;
    }
    g_dashVisible = false;
    g_dashHwnd = nullptr;
}

void Dashboard::addEntry(const TranscriptionEntry& e)
{
    {
        std::lock_guard<std::mutex> lock(m_histMutex);
        m_history.push_back(e);
        if (m_history.size() > 200)
            m_history.erase(m_history.begin());
    }
    if (g_dashboardUI) {
        g_dashboardUI->addEntry(e);
    }
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

void Dashboard::launchOnThread() {}
