// dashboard.cpp — Modern Tabbed Dashboard with Glassmorphism Design
// Features: History, Statistics, Settings tabs with smooth animations

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include "dashboard.h"
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

static constexpr int FPS_MS = 16;
static constexpr int TIMER_ID = 99;
static constexpr int WIN_W = 900;
static constexpr int WIN_H = 640;

// Colors - Modern Dark Theme
namespace Colors {
    const D2D1_COLOR_F Background = {0.055f, 0.055f, 0.078f, 1.0f};      // Deep navy
    const D2D1_COLOR_F Surface    = {0.086f, 0.086f, 0.114f, 1.0f};      // Card surface
    const D2D1_COLOR_F SurfaceAlt = {0.110f, 0.110f, 0.145f, 1.0f};      // Lighter surface
    const D2D1_COLOR_F Accent     = {0.345f, 0.416f, 0.969f, 1.0f};      // Indigo
    const D2D1_COLOR_F AccentGlow = {0.345f, 0.416f, 0.969f, 0.3f};      // Glow effect
    const D2D1_COLOR_F TextPrimary   = {0.95f, 0.95f, 0.97f, 1.0f};      // White-ish
    const D2D1_COLOR_F TextSecondary = {0.65f, 0.65f, 0.70f, 1.0f};      // Gray
    const D2D1_COLOR_F TextMuted     = {0.45f, 0.45f, 0.50f, 1.0f};      // Dark gray
    const D2D1_COLOR_F Success    = {0.20f, 0.82f, 0.50f, 1.0f};         // Green
    const D2D1_COLOR_F Warning    = {0.98f, 0.72f, 0.20f, 1.0f};         // Amber
    const D2D1_COLOR_F Border     = {0.20f, 0.20f, 0.28f, 1.0f};         // Subtle border
    const D2D1_COLOR_F GlassTint  = {0.10f, 0.12f, 0.18f, 0.85f};        // Glass effect
}

// Animation helpers
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static inline float easeOutCubic(float t) { float u = 1.f - t; return 1.f - u * u * u; }
static inline float easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
}

// Tab enumeration
enum class Tab { History, Statistics, Settings, Count };

// Global state
static std::atomic<bool>  g_dashVisible{false};
static std::atomic<HWND>  g_dashHwnd{nullptr};
static Dashboard*         g_dashInst = nullptr;

// ==================================================================
// Modern Dashboard UI Class
// ==================================================================

class ModernDashboard {
public:
    ModernDashboard() = default;
    ~ModernDashboard() { cleanup(); }

    bool init(HINSTANCE hInst, Dashboard* dashboard);
    void show();
    void hide();
    bool isVisible() const { return g_dashVisible.load(); }
    void addEntry(const TranscriptionEntry& e);
    void updateStats();
    void cleanup();
    void onTimer();
    void onPaint();
    void onResize(int w, int h);
    void onMouseMove(int x, int y);
    void onMouseLeave();
    void onLButtonDown(int x, int y);
    void onLButtonUp(int x, int y);

private:
    // Window handles
    HWND m_hwnd = nullptr;
    HDC m_memDC = nullptr;
    HBITMAP m_hBitmap = nullptr;
    Dashboard* m_dashboard = nullptr;

    // Direct2D resources
    ID2D1Factory* m_d2dFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRT = nullptr;
    IDWriteFactory* m_dwFactory = nullptr;
    IDWriteTextFormat* m_fontTitle = nullptr;
    IDWriteTextFormat* m_fontHeader = nullptr;
    IDWriteTextFormat* m_fontBody = nullptr;
    IDWriteTextFormat* m_fontSmall = nullptr;
    IDWriteTextFormat* m_fontMono = nullptr;

    // Layout constants
    static constexpr int HEADER_H = 70;
    static constexpr int TAB_H = 48;
    static constexpr int PADDING = 24;
    static constexpr int SIDEBAR_W = 220;
    static constexpr int ITEM_H = 80;
    static constexpr int CARD_RADIUS = 12;

    // State
    Tab m_currentTab = Tab::History;
    float m_tabAnim = 0.0f;  // 0 = history, 1 = stats, 2 = settings
    int m_winW = WIN_W;
    int m_winH = WIN_H;
    int m_contentW = WIN_W - SIDEBAR_W;
    int m_contentH = WIN_H - HEADER_H - TAB_H;

    // Mouse state
    int m_mouseX = -1, m_mouseY = -1;
    int m_hoverTab = -1;
    int m_hoverItem = -1;
    bool m_mouseDown = false;

    // History items with animation
    struct HistItem {
        TranscriptionEntry entry;
        float fadeIn = 0.0f;
        float slideX = -30.0f;
        float hover = 0.0f;
    };
    std::vector<HistItem> m_items;
    float m_scrollY = 0.0f;
    int m_visibleItems = 0;

    // Stats animation
    struct StatAnim {
        float value = 0.0f;
        float target = 0.0f;
    };
    StatAnim m_statAnims[6];

    // Settings state
    struct SettingItem {
        std::string label;
        std::string description;
        bool isToggle = false;
        bool* boolValue = nullptr;
        int* intValue = nullptr;
        int minVal = 0, maxVal = 100;
        RECT rect;
        float hover = 0.0f;
    };
    std::vector<SettingItem> m_settings;
    int m_hoverSetting = -1;

    // Methods
    bool createResources();
    void releaseResources();
    void draw();
    void drawSidebar();
    void drawHeader();
    void drawTabs();
    void drawHistoryTab();
    void drawStatisticsTab();
    void drawSettingsTab();
    void drawCard(float x, float y, float w, float h, D2D1_COLOR_F bg, D2D1_COLOR_F border, float radius = 12);
    void drawButton(float x, float y, float w, float h, const wchar_t* text, bool primary, float hover);
    void drawToggle(float x, float y, bool enabled, float hover);
    void drawSlider(float x, float y, float w, int value, int min, int max, float hover);
    void drawStatCard(float x, float y, float w, float h, const wchar_t* label, const wchar_t* value, 
                      const wchar_t* subtext, D2D1_COLOR_F accent);
    void drawEmptyState(const wchar_t* title, const wchar_t* subtitle);
    int hitTestTab(int x, int y);
    int hitTestSetting(int x, int y);
    void rebuildSettings();
    
    // Make rebuildSettings accessible to Dashboard class
    friend class Dashboard;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

static ModernDashboard* g_dashboardUI = nullptr;

// ==================================================================
// Resource Creation
// ==================================================================

bool ModernDashboard::createResources()
{
    // Create GDI resources
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_winW;
    bmi.bmiHeader.biHeight = -m_winH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    void* pBits = nullptr;
    m_hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);

    m_memDC = CreateCompatibleDC(nullptr);
    SelectObject(m_memDC, m_hBitmap);

    // Create D2D factory
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory)))
        return false;

    // Create DirectWrite factory
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));

    // Create fonts
    if (m_dwFactory) {
        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-US", &m_fontTitle);

        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-US", &m_fontHeader);

        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &m_fontBody);

        m_dwFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &m_fontSmall);

        m_dwFactory->CreateTextFormat(L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-US", &m_fontMono);
    }

    // Create render target — use IGNORE alpha for GDI DC render target (prevents transparency)
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f);

    return SUCCEEDED(m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRT));
}

void ModernDashboard::releaseResources()
{
    if (m_fontMono) { m_fontMono->Release(); m_fontMono = nullptr; }
    if (m_fontSmall) { m_fontSmall->Release(); m_fontSmall = nullptr; }
    if (m_fontBody) { m_fontBody->Release(); m_fontBody = nullptr; }
    if (m_fontHeader) { m_fontHeader->Release(); m_fontHeader = nullptr; }
    if (m_fontTitle) { m_fontTitle->Release(); m_fontTitle = nullptr; }
    if (m_dcRT) { m_dcRT->Release(); m_dcRT = nullptr; }
    if (m_dwFactory) { m_dwFactory->Release(); m_dwFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
    if (m_memDC) { DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_hBitmap) { DeleteObject(m_hBitmap); m_hBitmap = nullptr; }
}

// ==================================================================
// Initialization
// ==================================================================

bool ModernDashboard::init(HINSTANCE hInst, Dashboard* dashboard)
{
    m_dashboard = dashboard;
    if (!createResources()) return false;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"FLOWON_DASHBOARD_V2";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // Center window on screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - m_winW) / 2;
    int y = (screenH - m_winH) / 2;

    // Create window with dark title bar
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"FLOWON_DASHBOARD_V2", L"FLOW-ON! Dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
        x, y, m_winW, m_winH,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) return false;

    // Enable dark title bar (Windows 10/11)
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Extend frame for glass effect
    MARGINS margins = {0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    SetTimer(m_hwnd, TIMER_ID, FPS_MS, nullptr);
    rebuildSettings();
    return true;
}

void ModernDashboard::rebuildSettings()
{
    m_settings.clear();
    if (!m_dashboard) return;

    auto& s = m_dashboard->m_settings;

    SettingItem gpu;
    gpu.label = "Use GPU Acceleration";
    gpu.description = "Faster transcription with NVIDIA/AMD GPUs";
    gpu.isToggle = true;
    gpu.boolValue = &s.useGPU;
    m_settings.push_back(gpu);

    SettingItem startup;
    startup.label = "Start with Windows";
    startup.description = "Automatically launch FLOW-ON! on login";
    startup.isToggle = true;
    startup.boolValue = &s.startWithWindows;
    m_settings.push_back(startup);

    SettingItem overlay;
    overlay.label = "Show Recording Overlay";
    overlay.description = "Display visual feedback during recording";
    overlay.isToggle = true;
    overlay.boolValue = &s.enableOverlay;
    m_settings.push_back(overlay);
}

// ==================================================================
// Window Management
// ==================================================================

void ModernDashboard::show()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        g_dashVisible = true;
        g_dashHwnd = m_hwnd;
        updateStats();
    }
}

void ModernDashboard::hide()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        g_dashVisible = false;
        g_dashHwnd = nullptr;
    }
}

void ModernDashboard::cleanup()
{
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    releaseResources();
    g_dashVisible = false;
    g_dashHwnd = nullptr;
}

// ==================================================================
// Data Management
// ==================================================================

void ModernDashboard::addEntry(const TranscriptionEntry& e)
{
    HistItem item;
    item.entry = e;
    item.fadeIn = 0.0f;
    item.slideX = -30.0f;
    m_items.insert(m_items.begin(), item);
    if (m_items.size() > 100) m_items.pop_back();
}

void ModernDashboard::updateStats()
{
    if (!m_dashboard) return;
    auto stats = m_dashboard->getStats();

    m_statAnims[0].target = static_cast<float>(stats.totalTranscriptions);
    m_statAnims[1].target = static_cast<float>(stats.totalWords);
    m_statAnims[2].target = static_cast<float>(stats.totalCharacters);
    m_statAnims[3].target = static_cast<float>(stats.avgLatencyMs);
    m_statAnims[4].target = static_cast<float>(stats.todayCount);
    m_statAnims[5].target = static_cast<float>(stats.sessionCount);
}

// ==================================================================
// Animation & Rendering
// ==================================================================

void ModernDashboard::onTimer()
{
    // Animate history items
    for (auto& item : m_items) {
        if (item.fadeIn < 1.0f) item.fadeIn = lerp(item.fadeIn, 1.0f, 0.15f);
        if (item.slideX < 0.0f) item.slideX = lerp(item.slideX, 0.0f, 0.12f);
        if (item.hover > 0.0f) item.hover = lerp(item.hover, 0.0f, 0.2f);
    }

    // Animate stats
    for (auto& anim : m_statAnims) {
        anim.value = lerp(anim.value, anim.target, 0.08f);
    }

    // Animate tab transition
    float targetTab = static_cast<float>(m_currentTab);
    m_tabAnim = lerp(m_tabAnim, targetTab, 0.15f);

    // Animate settings hover
    for (auto& setting : m_settings) {
        float target = (m_hoverSetting == &setting - &m_settings[0]) ? 1.0f : 0.0f;
        setting.hover = lerp(setting.hover, target, 0.2f);
    }

    draw();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ModernDashboard::draw()
{
    if (!m_dcRT || !m_memDC) return;

    RECT rc = {0, 0, m_winW, m_winH};
    if (FAILED(m_dcRT->BindDC(m_memDC, &rc))) return;

    m_dcRT->BeginDraw();

    // Clear background
    m_dcRT->Clear(Colors::Background);

    // Draw sidebar
    drawSidebar();

    // Draw header
    drawHeader();

    // Draw tabs
    drawTabs();

    // Draw content based on current tab
    switch (m_currentTab) {
    case Tab::History: drawHistoryTab(); break;
    case Tab::Statistics: drawStatisticsTab(); break;
    case Tab::Settings: drawSettingsTab(); break;
    default: break;
    }

    m_dcRT->EndDraw();

    // Present to window
    HDC windowDC = GetDC(m_hwnd);
    if (windowDC) {
        BitBlt(windowDC, 0, 0, m_winW, m_winH, m_memDC, 0, 0, SRCCOPY);
        ReleaseDC(m_hwnd, windowDC);
    }
}

// ==================================================================
// Drawing Helpers
// ==================================================================

void ModernDashboard::drawCard(float x, float y, float w, float h, 
                               D2D1_COLOR_F bg, D2D1_COLOR_F border, float radius)
{
    ID2D1SolidColorBrush* brush = nullptr;

    // Fill
    m_dcRT->CreateSolidColorBrush(bg, &brush);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(x, y, x + w, y + h), radius, radius};
        m_dcRT->FillRoundedRectangle(rr, brush);
        brush->Release();
    }

    // Border
    m_dcRT->CreateSolidColorBrush(border, &brush);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(x, y, x + w, y + h), radius, radius};
        m_dcRT->DrawRoundedRectangle(rr, brush, 1.0f);
        brush->Release();
    }
}

void ModernDashboard::drawButton(float x, float y, float w, float h, 
                                 const wchar_t* text, bool primary, float hover)
{
    D2D1_COLOR_F bg = primary ? Colors::Accent : Colors::Surface;
    D2D1_COLOR_F border = primary ? Colors::Accent : Colors::Border;

    // Apply hover effect
    if (hover > 0.0f) {
        bg.r = lerp(bg.r, 1.0f, hover * 0.1f);
        bg.g = lerp(bg.g, 1.0f, hover * 0.1f);
        bg.b = lerp(bg.b, 1.0f, hover * 0.1f);
    }

    drawCard(x, y, w, h, bg, border, 8);

    // Text
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(primary ? Colors::TextPrimary : Colors::TextSecondary, &brush);
    if (brush && m_fontBody) {
        D2D1_RECT_F rc = {x, y + (h - 20) / 2, x + w, y + h};
        m_dcRT->DrawTextW(text, static_cast<UINT32>(wcslen(text)), m_fontBody, rc, brush,
            D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        brush->Release();
    }
}

void ModernDashboard::drawToggle(float x, float y, bool enabled, float hover)
{
    const float w = 44, h = 24, radius = 12;
    const float thumbRadius = 10;
    const float thumbX = enabled ? x + w - radius : x + radius;

    // Track
    D2D1_COLOR_F trackColor = enabled ? Colors::Accent : Colors::SurfaceAlt;
    if (hover > 0.0f && !enabled) {
        trackColor.r = lerp(trackColor.r, Colors::Accent.r, hover * 0.3f);
        trackColor.g = lerp(trackColor.g, Colors::Accent.g, hover * 0.3f);
        trackColor.b = lerp(trackColor.b, Colors::Accent.b, hover * 0.3f);
    }

    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(trackColor, &brush);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(x, y, x + w, y + h), radius, radius};
        m_dcRT->FillRoundedRectangle(rr, brush);
        brush->Release();
    }

    // Thumb
    m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
    if (brush) {
        D2D1_ELLIPSE thumb = {D2D1::Point2F(thumbX, y + h / 2), thumbRadius, thumbRadius};
        m_dcRT->FillEllipse(thumb, brush);
        brush->Release();
    }
}

void ModernDashboard::drawSlider(float x, float y, float w, int value, int min, int max, float hover)
{
    const float h = 6, thumbRadius = 8;
    float pct = static_cast<float>(value - min) / (max - min);
    float thumbX = x + pct * w;

    // Track background
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(Colors::SurfaceAlt, &brush);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(x, y, x + w, y + h), h / 2, h / 2};
        m_dcRT->FillRoundedRectangle(rr, brush);
        brush->Release();
    }

    // Track fill
    m_dcRT->CreateSolidColorBrush(Colors::Accent, &brush);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(x, y, thumbX, y + h), h / 2, h / 2};
        m_dcRT->FillRoundedRectangle(rr, brush);
        brush->Release();
    }

    // Thumb
    D2D1_COLOR_F thumbColor = Colors::TextPrimary;
    if (hover > 0.0f) {
        thumbColor.r = lerp(thumbColor.r, Colors::Accent.r, hover * 0.5f);
        thumbColor.g = lerp(thumbColor.g, Colors::Accent.g, hover * 0.5f);
        thumbColor.b = lerp(thumbColor.b, Colors::Accent.b, hover * 0.5f);
    }
    m_dcRT->CreateSolidColorBrush(thumbColor, &brush);
    if (brush) {
        D2D1_ELLIPSE thumb = {D2D1::Point2F(thumbX, y + h / 2), thumbRadius, thumbRadius};
        m_dcRT->FillEllipse(thumb, brush);
        brush->Release();
    }
}

void ModernDashboard::drawStatCard(float x, float y, float w, float h, 
                                   const wchar_t* label, const wchar_t* value,
                                   const wchar_t* subtext, D2D1_COLOR_F accent)
{
    // Card background with subtle gradient effect
    D2D1_COLOR_F bg = Colors::Surface;
    bg.r = lerp(bg.r, accent.r, 0.05f);
    bg.g = lerp(bg.g, accent.g, 0.05f);
    bg.b = lerp(bg.b, accent.b, 0.05f);

    drawCard(x, y, w, h, bg, Colors::Border, CARD_RADIUS);

    // Accent line on left
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(accent, &brush);
    if (brush) {
        D2D1_RECT_F line = {x, y + 16, x + 3, y + h - 16};
        m_dcRT->FillRectangle(line, brush);
        brush->Release();
    }

    // Label
    if (m_fontSmall) {
        m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + 16, y + 16, x + w - 16, y + 36};
            m_dcRT->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fontSmall, rc, brush);
            brush->Release();
        }
    }

    // Value
    if (m_fontTitle) {
        m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + 16, y + 38, x + w - 16, y + 75};
            m_dcRT->DrawTextW(value, static_cast<UINT32>(wcslen(value)), m_fontTitle, rc, brush);
            brush->Release();
        }
    }

    // Subtext
    if (m_fontSmall && subtext) {
        m_dcRT->CreateSolidColorBrush(Colors::TextSecondary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + 16, y + 78, x + w - 16, y + 96};
            m_dcRT->DrawTextW(subtext, static_cast<UINT32>(wcslen(subtext)), m_fontSmall, rc, brush);
            brush->Release();
        }
    }
}

void ModernDashboard::drawEmptyState(const wchar_t* title, const wchar_t* subtitle)
{
    float cx = SIDEBAR_W + m_contentW / 2;
    float cy = HEADER_H + TAB_H + m_contentH / 2;

    ID2D1SolidColorBrush* brush = nullptr;

    // Title
    if (m_fontHeader) {
        m_dcRT->CreateSolidColorBrush(Colors::TextSecondary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {SIDEBAR_W, cy - 30, m_winW, cy - 5};
            m_dcRT->DrawTextW(title, static_cast<UINT32>(wcslen(title)), m_fontHeader, rc, brush,
                D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            brush->Release();
        }
    }

    // Subtitle
    if (m_fontBody) {
        m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
        if (brush) {
            D2D1_RECT_F rc = {SIDEBAR_W, cy + 5, m_winW, cy + 30};
            m_dcRT->DrawTextW(subtitle, static_cast<UINT32>(wcslen(subtitle)), m_fontBody, rc, brush,
                D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            brush->Release();
        }
    }
}

// ==================================================================
// Sidebar
// ==================================================================

void ModernDashboard::drawSidebar()
{
    // Sidebar background
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(Colors::Surface, &brush);
    if (brush) {
        D2D1_RECT_F rc = {0, 0, SIDEBAR_W, static_cast<float>(m_winH)};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }

    // Right border
    m_dcRT->CreateSolidColorBrush(Colors::Border, &brush);
    if (brush) {
        D2D1_RECT_F rc = {SIDEBAR_W - 1, 0, SIDEBAR_W, static_cast<float>(m_winH)};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }

    // App logo/title
    if (m_fontTitle) {
        m_dcRT->CreateSolidColorBrush(Colors::Accent, &brush);
        if (brush) {
            D2D1_RECT_F rc = {24, 20, SIDEBAR_W - 24, 60};
            m_dcRT->DrawTextW(L"FLOW-ON!", 8, m_fontTitle, rc, brush);
            brush->Release();
        }
    }

    // Version
    if (m_fontSmall) {
        m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
        if (brush) {
            D2D1_RECT_F rc = {24, 55, SIDEBAR_W - 24, 75};
            m_dcRT->DrawTextW(L"v1.0.0", 6, m_fontSmall, rc, brush);
            brush->Release();
        }
    }

    // Status indicator
    float statusY = m_winH - 60;
    D2D1_COLOR_F statusColor = Colors::Success;

    // Status dot
    m_dcRT->CreateSolidColorBrush(statusColor, &brush);
    if (brush) {
        D2D1_ELLIPSE dot = {D2D1::Point2F(32, statusY + 6), 5, 5};
        m_dcRT->FillEllipse(dot, brush);
        brush->Release();
    }

    // Status text
    if (m_fontSmall) {
        m_dcRT->CreateSolidColorBrush(Colors::TextSecondary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {45, static_cast<float>(statusY), SIDEBAR_W - 24, static_cast<float>(statusY + 20)};
            m_dcRT->DrawTextW(L"Ready", 5, m_fontSmall, rc, brush);
            brush->Release();
        }
    }

    // Shortcut hint
    if (m_fontSmall) {
        m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
        if (brush) {
            D2D1_RECT_F rc = {24, static_cast<float>(statusY + 18), SIDEBAR_W - 24, static_cast<float>(statusY + 40)};
            m_dcRT->DrawTextW(L"Press Alt+V to record", 21, m_fontSmall, rc, brush);
            brush->Release();
        }
    }
}

// ==================================================================
// Header
// ==================================================================

void ModernDashboard::drawHeader()
{
    float x = SIDEBAR_W;
    float w = m_contentW;

    // Header background
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(Colors::Background, &brush);
    if (brush) {
        D2D1_RECT_F rc = {x, 0, x + w, HEADER_H};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }

    // Bottom border
    m_dcRT->CreateSolidColorBrush(Colors::Border, &brush);
    if (brush) {
        D2D1_RECT_F rc = {x, HEADER_H - 1, x + w, HEADER_H};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }

    // Page title based on current tab
    const wchar_t* title = L"History";
    switch (m_currentTab) {
    case Tab::History: title = L"Transcription History"; break;
    case Tab::Statistics: title = L"Usage Statistics"; break;
    case Tab::Settings: title = L"Settings"; break;
    default: break;
    }

    if (m_fontHeader) {
        m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + PADDING, 20, x + w - PADDING, HEADER_H - 10};
            m_dcRT->DrawTextW(title, static_cast<UINT32>(wcslen(title)), m_fontHeader, rc, brush);
            brush->Release();
        }
    }

    // Clear button (History tab only)
    if (m_currentTab == Tab::History && !m_items.empty()) {
        float btnX = x + w - 120;
        float btnY = 18;
        drawButton(btnX, btnY, 90, 34, L"Clear All", false, 0.0f);
    }
}

// ==================================================================
// Tabs
// ==================================================================

void ModernDashboard::drawTabs()
{
    float x = SIDEBAR_W;
    float y = HEADER_H;
    float w = m_contentW;

    // Tab bar background
    ID2D1SolidColorBrush* brush = nullptr;
    m_dcRT->CreateSolidColorBrush(Colors::Background, &brush);
    if (brush) {
        D2D1_RECT_F rc = {x, y, x + w, y + TAB_H};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }

    // Tab items
    const wchar_t* tabNames[] = {L"History", L"Statistics", L"Settings"};
    float tabX = x + PADDING;
    float tabW = 90;
    float tabH = TAB_H - 8;

    for (int i = 0; i < 3; i++) {
        bool isActive = (m_currentTab == static_cast<Tab>(i));
        bool isHover = (m_hoverTab == i);

        // Tab background
        D2D1_COLOR_F bg = isActive ? Colors::Surface : Colors::Background;
        if (isHover && !isActive) {
            bg = Colors::SurfaceAlt;
        }

        drawCard(tabX, y + 4, tabW, tabH, bg, isActive ? Colors::Accent : Colors::Background, 8);

        // Active indicator line
        if (isActive) {
            m_dcRT->CreateSolidColorBrush(Colors::Accent, &brush);
            if (brush) {
                D2D1_RECT_F rc = {tabX + 20, y + tabH + 2, tabX + tabW - 20, y + tabH + 4};
                m_dcRT->FillRectangle(rc, brush);
                brush->Release();
            }
        }

        // Tab text
        if (m_fontBody) {
            D2D1_COLOR_F textColor = isActive ? Colors::TextPrimary : Colors::TextSecondary;
            m_dcRT->CreateSolidColorBrush(textColor, &brush);
            if (brush) {
                D2D1_RECT_F rc = {tabX, y + 14, tabX + tabW, y + tabH};
                m_dcRT->DrawTextW(tabNames[i], static_cast<UINT32>(wcslen(tabNames[i])), 
                    m_fontBody, rc, brush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                brush->Release();
            }
        }

        tabX += tabW + 8;
    }

    // Bottom border
    m_dcRT->CreateSolidColorBrush(Colors::Border, &brush);
    if (brush) {
        D2D1_RECT_F rc = {x, y + TAB_H - 1, x + w, y + TAB_H};
        m_dcRT->FillRectangle(rc, brush);
        brush->Release();
    }
}

int ModernDashboard::hitTestTab(int mx, int my)
{
    float x = SIDEBAR_W + PADDING;
    float y = HEADER_H + 4;
    float w = 90;
    float h = TAB_H - 8;

    for (int i = 0; i < 3; i++) {
        if (mx >= x && mx <= x + w && my >= y && my <= y + h) {
            return i;
        }
        x += w + 8;
    }
    return -1;
}

// ==================================================================
// History Tab
// ==================================================================

void ModernDashboard::drawHistoryTab()
{
    float x = SIDEBAR_W + PADDING;
    float y = HEADER_H + TAB_H + PADDING;
    float w = m_contentW - 2 * PADDING;
    float h = m_contentH - 2 * PADDING;

    if (m_items.empty()) {
        drawEmptyState(L"No transcriptions yet", L"Press Alt+V to start recording");
        return;
    }

    // Calculate visible items
    int maxItems = static_cast<int>(h / ITEM_H);
    int startIdx = static_cast<int>(m_scrollY / ITEM_H);
    startIdx = std::max(0, std::min(startIdx, static_cast<int>(m_items.size()) - 1));

    ID2D1SolidColorBrush* brush = nullptr;

    for (int i = startIdx; i < std::min(startIdx + maxItems + 1, static_cast<int>(m_items.size())); i++) {
        auto& item = m_items[i];
        float itemY = y + (i - startIdx) * ITEM_H - fmod(m_scrollY, ITEM_H);

        if (itemY + ITEM_H < y || itemY > y + h) continue;

        float anim = easeOutCubic(clamp01(item.fadeIn));
        float slide = item.slideX * (1.0f - anim);
        float itemX = x + slide;

        // Item card
        D2D1_COLOR_F bg = Colors::Surface;
        if (item.hover > 0.0f) {
            bg.r = lerp(bg.r, Colors::SurfaceAlt.r, item.hover * 0.5f);
            bg.g = lerp(bg.g, Colors::SurfaceAlt.g, item.hover * 0.5f);
            bg.b = lerp(bg.b, Colors::SurfaceAlt.b, item.hover * 0.5f);
        }

        drawCard(itemX, itemY, w, ITEM_H - 8, bg, Colors::Border, 10);

        // Timestamp
        if (m_fontSmall) {
            std::wstring time(item.entry.timestamp.begin(), item.entry.timestamp.end());
            m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
            if (brush) {
                D2D1_RECT_F rc = {itemX + 16, itemY + 12, itemX + 80, itemY + 30};
                m_dcRT->DrawTextW(time.c_str(), static_cast<UINT32>(time.size()), m_fontSmall, rc, brush);
                brush->Release();
            }
        }

        // Transcription text
        if (m_fontBody) {
            std::wstring text(item.entry.text.begin(), item.entry.text.end());
            if (text.length() > 120) text = text.substr(0, 117) + L"...";

            m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
            if (brush) {
                D2D1_RECT_F rc = {itemX + 16, itemY + 32, itemX + w - 100, itemY + ITEM_H - 16};
                m_dcRT->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_fontBody, rc, brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
                brush->Release();
            }
        }

        // Latency badge
        if (item.entry.latencyMs > 0) {
            wchar_t latStr[32];
            swprintf_s(latStr, L"%d ms", item.entry.latencyMs);

            // Badge background
            D2D1_COLOR_F badgeColor = item.entry.latencyMs < 1000 ? Colors::Success : 
                                      item.entry.latencyMs < 3000 ? Colors::Warning : 
                                      D2D1_COLOR_F{0.9f, 0.3f, 0.3f, 1.0f};

            m_dcRT->CreateSolidColorBrush(badgeColor, &brush);
            if (brush) {
                D2D1_ROUNDED_RECT rr = {D2D1::RectF(itemX + w - 80, itemY + 28, itemX + w - 16, itemY + 50), 10, 10};
                m_dcRT->FillRoundedRectangle(rr, brush);
                brush->Release();
            }

            // Badge text
            if (m_fontSmall) {
                m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
                if (brush) {
                    D2D1_RECT_F rc = {itemX + w - 80, itemY + 32, itemX + w - 16, itemY + 50};
                    m_dcRT->DrawTextW(latStr, static_cast<UINT32>(wcslen(latStr)), m_fontSmall, rc, brush,
                        D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                    brush->Release();
                }
            }
        }
    }
}

// ==================================================================
// Statistics Tab
// ==================================================================

void ModernDashboard::drawStatisticsTab()
{
    float x = SIDEBAR_W + PADDING;
    float y = HEADER_H + TAB_H + PADDING;
    float w = m_contentW - 2 * PADDING;

    // Stat cards grid (2x3)
    float cardW = (w - PADDING) / 2;
    float cardH = 110;

    const wchar_t* labels[] = {
        L"Total Transcriptions",
        L"Total Words",
        L"Total Characters",
        L"Average Latency",
        L"Today",
        L"This Session"
    };

    const wchar_t* subtexts[] = {
        L"All time",
        L"Words transcribed",
        L"Characters typed",
        L"Processing time",
        L"Transcriptions today",
        L"Since app started"
    };

    D2D1_COLOR_F accents[] = {
        Colors::Accent,
        Colors::Success,
        D2D1_COLOR_F{0.2f, 0.6f, 0.9f, 1.0f},  // Blue
        Colors::Warning,
        D2D1_COLOR_F{0.9f, 0.4f, 0.7f, 1.0f},  // Pink
        D2D1_COLOR_F{0.5f, 0.8f, 0.3f, 1.0f}   // Green
    };

    ID2D1SolidColorBrush* brush = nullptr;

    for (int i = 0; i < 6; i++) {
        float cx = x + (i % 2) * (cardW + PADDING);
        float cy = y + (i / 2) * (cardH + PADDING);

        // Format value
        int val = static_cast<int>(m_statAnims[i].value);
        wchar_t valueStr[32];

        if (i == 3) {  // Latency
            swprintf_s(valueStr, L"%d ms", val);
        } else if (val >= 1000000) {
            swprintf_s(valueStr, L"%.1fM", val / 1000000.0f);
        } else if (val >= 1000) {
            swprintf_s(valueStr, L"%d,%03d", val / 1000, val % 1000);
        } else {
            swprintf_s(valueStr, L"%d", val);
        }

        drawStatCard(cx, cy, cardW, cardH, labels[i], valueStr, subtexts[i], accents[i]);
    }

    // Recent activity section
    float activityY = y + 3 * (cardH + PADDING) + PADDING;
    float activityH = m_contentH - (3 * (cardH + PADDING) + 2 * PADDING);

    if (activityH > 100) {
        // Section title
        if (m_fontHeader) {
            m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
            if (brush) {
                D2D1_RECT_F rc = {x, activityY, x + w, activityY + 30};
                m_dcRT->DrawTextW(L"Recent Activity", 15, m_fontHeader, rc, brush);
                brush->Release();
            }
        }

        // Activity card
        drawCard(x, activityY + 35, w, activityH - 35, Colors::Surface, Colors::Border, CARD_RADIUS);

        // Activity content
        if (m_items.empty()) {
            if (m_fontBody) {
                m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
                if (brush) {
                    D2D1_RECT_F rc = {x + PADDING, activityY + 80, x + w - PADDING, activityY + 110};
                    m_dcRT->DrawTextW(L"No recent activity", 18, m_fontBody, rc, brush);
                    brush->Release();
                }
            }
        } else {
            // Show last 3 entries
            float entryY = activityY + 50;
            for (int i = 0; i < std::min(3, static_cast<int>(m_items.size())); i++) {
                auto& item = m_items[i];

                // Time
                if (m_fontSmall) {
                    std::wstring time(item.entry.timestamp.begin(), item.entry.timestamp.end());
                    m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
                    if (brush) {
                        D2D1_RECT_F rc = {x + PADDING, entryY, x + 80, entryY + 20};
                        m_dcRT->DrawTextW(time.c_str(), static_cast<UINT32>(time.size()), m_fontSmall, rc, brush);
                        brush->Release();
                    }
                }

                // Text preview
                if (m_fontBody) {
                    std::wstring text(item.entry.text.begin(), item.entry.text.end());
                    if (text.length() > 80) text = text.substr(0, 77) + L"...";

                    m_dcRT->CreateSolidColorBrush(Colors::TextSecondary, &brush);
                    if (brush) {
                        D2D1_RECT_F rc = {x + 90, entryY, x + w - PADDING, entryY + 24};
                        m_dcRT->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_fontBody, rc, brush);
                        brush->Release();
                    }
                }

                entryY += 35;
            }
        }
    }
}

// ==================================================================
// Settings Tab
// ==================================================================

void ModernDashboard::drawSettingsTab()
{
    float x = SIDEBAR_W + PADDING;
    float y = HEADER_H + TAB_H + PADDING;
    float w = m_contentW - 2 * PADDING;

    ID2D1SolidColorBrush* brush = nullptr;

    // Settings title
    if (m_fontHeader) {
        m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x, y, x + w, y + 30};
            m_dcRT->DrawTextW(L"General Settings", 16, m_fontHeader, rc, brush);
            brush->Release();
        }
    }

    y += 45;

    // Draw settings items
    for (size_t i = 0; i < m_settings.size(); i++) {
        auto& setting = m_settings[i];
        float itemH = 70;

        // Store rect for hit testing
        setting.rect = {
            static_cast<LONG>(x),
            static_cast<LONG>(y),
            static_cast<LONG>(x + w),
            static_cast<LONG>(y + itemH)
        };

        // Card background
        D2D1_COLOR_F bg = Colors::Surface;
        if (setting.hover > 0.0f) {
            bg.r = lerp(bg.r, Colors::SurfaceAlt.r, setting.hover * 0.5f);
            bg.g = lerp(bg.g, Colors::SurfaceAlt.g, setting.hover * 0.5f);
            bg.b = lerp(bg.b, Colors::SurfaceAlt.b, setting.hover * 0.5f);
        }
        drawCard(x, y, w, itemH, bg, Colors::Border, 10);

        // Label
        if (m_fontBody) {
            std::wstring label(setting.label.begin(), setting.label.end());
            m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
            if (brush) {
                D2D1_RECT_F rc = {x + 20, y + 12, x + w - 100, y + 32};
                m_dcRT->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fontBody, rc, brush);
                brush->Release();
            }
        }

        // Description
        if (m_fontSmall) {
            std::wstring desc(setting.description.begin(), setting.description.end());
            m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
            if (brush) {
                D2D1_RECT_F rc = {x + 20, y + 36, x + w - 100, y + 56};
                m_dcRT->DrawTextW(desc.c_str(), static_cast<UINT32>(desc.size()), m_fontSmall, rc, brush);
                brush->Release();
            }
        }

        // Toggle control
        if (setting.isToggle && setting.boolValue) {
            drawToggle(x + w - 70, y + 23, *setting.boolValue, setting.hover);
        }

        y += itemH + 12;
    }

    // About section
    y += 20;
    if (m_fontHeader) {
        m_dcRT->CreateSolidColorBrush(Colors::TextPrimary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x, y, x + w, y + 30};
            m_dcRT->DrawTextW(L"About", 5, m_fontHeader, rc, brush);
            brush->Release();
        }
    }

    y += 40;
    drawCard(x, y, w, 100, Colors::Surface, Colors::Border, 10);

    if (m_fontBody) {
        m_dcRT->CreateSolidColorBrush(Colors::TextSecondary, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + 20, y + 20, x + w - 20, y + 50};
            m_dcRT->DrawTextW(L"FLOW-ON! Voice Dictation", 24, m_fontBody, rc, brush);
            brush->Release();
        }
    }

    if (m_fontSmall) {
        m_dcRT->CreateSolidColorBrush(Colors::TextMuted, &brush);
        if (brush) {
            D2D1_RECT_F rc = {x + 20, y + 50, x + w - 20, y + 80};
            m_dcRT->DrawTextW(L"Version 1.0.0 | Built with Whisper.cpp", 38, m_fontSmall, rc, brush);
            brush->Release();
        }
    }
}

int ModernDashboard::hitTestSetting(int mx, int my)
{
    for (size_t i = 0; i < m_settings.size(); i++) {
        if (mx >= m_settings[i].rect.left && mx <= m_settings[i].rect.right &&
            my >= m_settings[i].rect.top && my <= m_settings[i].rect.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ==================================================================
// Input Handling
// ==================================================================

void ModernDashboard::onMouseMove(int x, int y)
{
    m_mouseX = x;
    m_mouseY = y;

    int oldHoverTab = m_hoverTab;
    m_hoverTab = hitTestTab(x, y);

    int oldHoverSetting = m_hoverSetting;
    m_hoverSetting = hitTestSetting(x, y);

    // Update item hover states
    if (m_currentTab == Tab::History) {
        float contentY = HEADER_H + TAB_H + PADDING;
        if (y >= contentY && x >= SIDEBAR_W + PADDING) {
            int itemIdx = static_cast<int>((y - contentY + m_scrollY) / ITEM_H);
            if (itemIdx >= 0 && itemIdx < static_cast<int>(m_items.size())) {
                for (size_t i = 0; i < m_items.size(); i++) {
                    m_items[i].hover = (i == itemIdx) ? 1.0f : 0.0f;
                }
            }
        }
    }
}

void ModernDashboard::onMouseLeave()
{
    m_mouseX = m_mouseY = -1;
    m_hoverTab = -1;
    m_hoverSetting = -1;
    for (auto& item : m_items) item.hover = 0.0f;
}

void ModernDashboard::onLButtonDown(int x, int y)
{
    m_mouseDown = true;

    // Check tab click
    int tab = hitTestTab(x, y);
    if (tab >= 0) {
        m_currentTab = static_cast<Tab>(tab);
        if (m_currentTab == Tab::Statistics) {
            updateStats();
        }
        return;
    }

    // Check settings toggle
    if (m_currentTab == Tab::Settings) {
        int setting = hitTestSetting(x, y);
        if (setting >= 0 && m_settings[setting].isToggle && m_settings[setting].boolValue) {
            *m_settings[setting].boolValue = !*m_settings[setting].boolValue;
            if (m_dashboard && m_dashboard->onSettingsChanged) {
                m_dashboard->onSettingsChanged(m_dashboard->m_settings);
            }
        }
    }

    // Check clear button
    if (m_currentTab == Tab::History && !m_items.empty()) {
        float btnX = SIDEBAR_W + m_contentW - 120;
        float btnY = 18;
        if (x >= btnX && x <= btnX + 90 && y >= btnY && y <= btnY + 34) {
            m_items.clear();
            if (m_dashboard && m_dashboard->onClearHistoryRequested) {
                m_dashboard->onClearHistoryRequested();
            }
        }
    }
}

void ModernDashboard::onLButtonUp(int x, int y)
{
    m_mouseDown = false;
}

void ModernDashboard::onResize(int w, int h)
{
    m_winW = w;
    m_winH = h;
    m_contentW = w - SIDEBAR_W;
    m_contentH = h - HEADER_H - TAB_H;

    // Recreate GDI resources for new size
    releaseResources();
    createResources();
}

// ==================================================================
// Window Procedure
// ==================================================================

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
        if (pThis->m_memDC && ps.hdc) {
            BitBlt(ps.hdc, 0, 0, pThis->m_winW, pThis->m_winH,
                   pThis->m_memDC, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        pThis->onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSELEAVE:
        pThis->onMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        pThis->onLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        pThis->onLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_SIZE:
        pThis->onResize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_CLOSE:
        pThis->hide();
        return 0;

    case WM_DESTROY:
        g_dashVisible = false;
        g_dashHwnd = nullptr;
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ==================================================================
// Dashboard API Implementation
// ==================================================================

bool Dashboard::init(HINSTANCE hInst, HWND ownerHwnd)
{
    m_ownerHwnd = ownerHwnd;
    m_initialized = true;

    g_dashboardUI = new ModernDashboard();
    if (!g_dashboardUI->init(hInst, this)) {
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
    g_dashboardUI->show();
}

void Dashboard::hide()
{
    if (!g_dashboardUI) return;
    g_dashboardUI->hide();
}

bool Dashboard::isVisible() const
{
    return g_dashboardUI && g_dashboardUI->isVisible();
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

UsageStats Dashboard::getStats() const
{
    std::lock_guard<std::mutex> lock(m_histMutex);
    UsageStats stats;
    stats.totalTranscriptions = static_cast<int>(m_history.size());

    int totalLatency = 0;
    for (const auto& e : m_history) {
        stats.totalWords += e.wordCount;
        stats.totalCharacters += static_cast<int>(e.text.length());
        totalLatency += e.latencyMs;
    }

    if (!m_history.empty()) {
        stats.avgLatencyMs = totalLatency / static_cast<int>(m_history.size());
        stats.lastUsed = m_history.back().timestamp;
    }

    // TODO: Implement today count and session count
    stats.todayCount = static_cast<int>(m_history.size());
    stats.sessionCount = static_cast<int>(m_history.size());

    return stats;
}

void Dashboard::updateSettings(const DashboardSettings& settings)
{
    m_settings = settings;
    if (g_dashboardUI) {
        g_dashboardUI->rebuildSettings();
    }
}
