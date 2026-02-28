// overlay.cpp â€” Direct2D pill overlay (DC render target + UpdateLayeredWindow)
//
// Key improvement over the old HwndRenderTarget approach:
//   â€¢ ID2D1DCRenderTarget renders into a 32-bit DIB with premultiplied alpha.
//   â€¢ UpdateLayeredWindow composites the DIB using per-pixel alpha â€” the area
//     outside the rounded pill is fully transparent with zero bleed/outline.
//   â€¢ SetLayeredWindowAttributes(LWA_ALPHA) is NOT used, which was the cause
//     of the "weird outline" in the previous version.
//
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <algorithm>
#include "overlay.h"

static constexpr int FPS_MS = 16;   // ~62.5 fps

// Smooth lerp helper
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
// Ease-out cubic
static inline float easeOut(float t) { float u = 1.f - t; return 1.f - u * u * u; }
static constexpr float PI = 3.14159265f;

// ==================================================================
// init
// ==================================================================
bool Overlay::init(HINSTANCE hInst)
{
    // D2D factory
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory)))
        return false;

    // DirectWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));

    if (m_dwFactory) {
        m_dwFactory->CreateTextFormat(
            L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", &m_textFormat);
        if (m_textFormat) {
            m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Window class â€” no background brush; we paint everything ourselves
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FLOWON_OVERLAY";
    wc.hbrBackground = nullptr;           // â† critical: no GDI background paint
    RegisterClassExW(&wc);

    // Layered, click-through, always-on-top popup
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FLOWON_OVERLAY", L"", WS_POPUP,
        0, 0, PILL_W, PILL_H,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!createGDIResources())    return false;
    if (!createDeviceResources()) return false;

    SetTimer(m_hwnd, TIMER_ID, FPS_MS, nullptr);
    return true;
}

// ==================================================================
// GDI resources â€” 32-bit DIB + memory DC for UpdateLayeredWindow
// ==================================================================
bool Overlay::createGDIResources()
{
    releaseGDIResources();

    BITMAPINFO bmi          = {};
    bmi.bmiHeader.biSize    = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth   = PILL_W;
    bmi.bmiHeader.biHeight  = -PILL_H;  // top-down
    bmi.bmiHeader.biPlanes  = 1;
    bmi.bmiHeader.biBitCount   = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    void* pBits  = nullptr;
    m_hBitmap    = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);
    if (!m_hBitmap) return false;

    m_memDC = CreateCompatibleDC(nullptr);
    SelectObject(m_memDC, m_hBitmap);
    return true;
}

void Overlay::releaseGDIResources()
{
    if (m_memDC)   { DeleteDC(m_memDC);         m_memDC   = nullptr; }
    if (m_hBitmap) { DeleteObject(m_hBitmap);   m_hBitmap = nullptr; }
}

// ==================================================================
// D2D DC Render Target
// ==================================================================
bool Overlay::createDeviceResources()
{
    if (m_dcRT) { m_dcRT->Release(); m_dcRT = nullptr; }

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    return SUCCEEDED(m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRT));
}

// ==================================================================
// positionWindow â€” centres pill near bottom of primary monitor
// ==================================================================
void Overlay::positionWindow()
{
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    m_winX = (screenW - PILL_W) / 2;
    m_winY = screenH - PILL_H - 76;
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 m_winX, m_winY, PILL_W, PILL_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ==================================================================
// setState
// ==================================================================
void Overlay::setState(OverlayState s)
{
    const OverlayState prev = m_state.load(std::memory_order_acquire);
    m_state.store(s, std::memory_order_release);

    if (s == OverlayState::Hidden) {
        // Trigger dismiss animation; window hides when it completes
        m_dismissing  = true;
    } else {
        m_dismissing  = false;
        m_stateAnim   = 0.0f;   // reset circle scale-in
        if (prev == OverlayState::Hidden) {
            m_appearAnim = 0.0f;   // start appear animation
        }
        if (s == OverlayState::Done || s == OverlayState::Error)
            m_flashFrames = 50;    // ~800 ms then auto-hide
        positionWindow();
    }
}

void Overlay::pushRMS(float rms)
{
    m_latestRMS.store(rms, std::memory_order_relaxed);
}

// ==================================================================
// Draw helpers
// ==================================================================
void Overlay::fillRect(const D2D1_ROUNDED_RECT& r, D2D1_COLOR_F c)
{
    ID2D1SolidColorBrush* b = nullptr;
    m_dcRT->CreateSolidColorBrush(c, &b);
    if (b) { m_dcRT->FillRoundedRectangle(r, b); b->Release(); }
}
void Overlay::strokeRect(const D2D1_ROUNDED_RECT& r, D2D1_COLOR_F c, float w)
{
    ID2D1SolidColorBrush* b = nullptr;
    m_dcRT->CreateSolidColorBrush(c, &b);
    if (b) { m_dcRT->DrawRoundedRectangle(r, b, w); b->Release(); }
}

// ==================================================================
// draw â€” one frame
// ==================================================================
void Overlay::draw()
{
    const OverlayState state = m_state.load(std::memory_order_acquire);

    // Even during dismiss we still need to draw the fading frame
    const bool visible = (state != OverlayState::Hidden) || m_dismissing;
    if (!visible || !m_dcRT || !m_memDC) return;

    // ---- Bind DC to render target each frame ----
    RECT rc = {0, 0, PILL_W, PILL_H};
    if (FAILED(m_dcRT->BindDC(m_memDC, &rc))) return;

    m_dcRT->BeginDraw();
    m_dcRT->Clear(D2D1::ColorF(0, 0, 0, 0));   // fully transparent canvas

    // ---- Compute scale for appear/dismiss ----
    float scale   = easeOut(clamp01(m_appearAnim));
    float opacity = clamp01(m_appearAnim);
    if (m_dismissing) {
        scale   = easeOut(clamp01(m_appearAnim));
        opacity = clamp01(m_appearAnim);
    }
    const float scaledW = PILL_W * scale;
    const float scaledH = PILL_H * scale;
    const float ox      = (PILL_W - scaledW) * 0.5f;
    const float oy      = (PILL_H - scaledH) * 0.5f;

    // ---- Pill background ----
    const D2D1_ROUNDED_RECT pill = {
        D2D1::RectF(ox + 1.5f, oy + 1.5f, ox + scaledW - 1.5f, oy + scaledH - 1.5f),
        PILL_R * scale, PILL_R * scale
    };
    fillRect  (pill, D2D1::ColorF(0.053f, 0.055f, 0.082f, 0.96f * opacity));
    strokeRect(pill, D2D1::ColorF(1.f, 1.f, 1.f, 0.09f * opacity), 1.0f);

    // ---- Subtle inner highlight at top ----
    {
        const D2D1_ROUNDED_RECT hi = {
            D2D1::RectF(ox + 1.5f, oy + 1.5f, ox + scaledW - 1.5f, oy + scaledH * 0.38f),
            PILL_R * scale, PILL_R * scale
        };
        ID2D1LinearGradientBrush* lgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.045f * opacity)},
            {1.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(ox, oy + 1.5f),
                    D2D1::Point2F(ox, oy + scaledH * 0.38f)),
                stops, &lgb);
            if (lgb) { m_dcRT->FillRoundedRectangle(hi, lgb); lgb->Release(); }
            stops->Release();
        }
    }

    const float cx = PILL_W / 2.0f;
    const float cy = PILL_H / 2.0f;

    // ---- State content ----
    if (!m_dismissing) {
        if      (state == OverlayState::Recording)  drawRecording (cx, cy);
        else if (state == OverlayState::Processing) drawProcessing(cx, cy);
        else if (state == OverlayState::Done)        drawDone      (cx, cy);
        else if (state == OverlayState::Error)       drawError     (cx, cy);
    }

    m_dcRT->EndDraw();
    present();
}

// ==================================================================
// present â€” blit DIB to screen via UpdateLayeredWindow
// ==================================================================
void Overlay::present()
{
    POINT ptSrc  = {0, 0};
    POINT ptDst  = {m_winX, m_winY};
    SIZE  szWnd  = {PILL_W, PILL_H};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, nullptr, &ptDst, &szWnd,
                        m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
}

// ==================================================================
// drawRecording â€” animated waveform + pulsing record dot
// ==================================================================
void Overlay::drawRecording(float cx, float cy)
{
    // ---- Drain latest RMS into ring buffer ----
    const float newRMS = m_latestRMS.load(std::memory_order_relaxed);
    m_wave[m_waveHead] = newRMS;
    m_waveHead = (m_waveHead + 1) % WAVE_SAMPLES;

    // ---- Per-bar exponential smoothing + idle sine at silence ----
    m_idlePhase += 0.08f;
    for (int i = 0; i < WAVE_SAMPLES; ++i) {
        const int   idx    = (m_waveHead + i) % WAVE_SAMPLES;
        const float target = m_wave[idx];
        m_smoothed[i] = lerp(m_smoothed[i], target, 0.28f);

        // Idle animation: subtle standing sine wave proportional to silence
        const float silence = 1.0f - clamp01(target * 8.0f);
        m_smoothed[i] += silence * 0.018f *
            std::sin(m_idlePhase + static_cast<float>(i) * 0.42f);
        m_smoothed[i] = std::max(m_smoothed[i], 0.0f);
    }

    // ---- Pulsing record dot (left side) ----
    m_dotPulse += 0.07f;
    const float pulseScale = 1.0f + 0.30f * std::sin(m_dotPulse);
    const float dotR = 4.5f * pulseScale;
    const float dotX = cx - (PILL_W * 0.5f) + 24.0f;

    // Outer glow
    {
        ID2D1RadialGradientBrush* rgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(1.f, 0.24f, 0.30f, 0.35f)},
            {1.0f, D2D1::ColorF(1.f, 0.24f, 0.30f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(dotX, cy), D2D1::Point2F(0.f, 0.f),
                    dotR * 2.2f, dotR * 2.2f),
                stops, &rgb);
            if (rgb) {
                m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, cy),
                                                   dotR * 2.2f, dotR * 2.2f), rgb);
                rgb->Release();
            }
            stops->Release();
        }
    }
    // Solid core
    {
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.27f, 0.33f, 1.0f), &b);
        if (b) {
            m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, cy), dotR, dotR), b);
            b->Release();
        }
    }

    // ---- Waveform bars ----
    constexpr float barW    = 3.0f;
    constexpr float barGap  = 2.2f;
    const     float totalW  = WAVE_SAMPLES * (barW + barGap);
    const     float startX  = cx - totalW / 2.0f + 18.0f;
    const     float maxBarH = PILL_H - 22.0f;

    for (int i = 0; i < WAVE_SAMPLES; ++i) {
        const float rmsVal = m_smoothed[i];

        // Edge fade envelope
        float fade = 1.0f;
        if      (i < 5)                    fade = static_cast<float>(i) / 5.0f;
        else if (i > WAVE_SAMPLES - 6)     fade = static_cast<float>(WAVE_SAMPLES - 1 - i) / 5.0f;
        fade = easeOut(fade);

        float barH = rmsVal * maxBarH * 3.2f + 2.5f;
        barH = std::min(barH * fade, maxBarH);
        barH = std::max(barH, 2.0f);

        const float x = startX + static_cast<float>(i) * (barW + barGap);

        // Colour: violet (#8B6FEF) in the centre fading to soft blue (#5BA8FF) at edges
        const float t    = static_cast<float>(i) / static_cast<float>(WAVE_SAMPLES - 1);
        // Mix violet â†’ blue by position; darken further at low amplitude
        const float r    = lerp(0.545f, 0.357f, t);
        const float g    = lerp(0.435f, 0.659f, t);
        const float b    = lerp(0.937f, 1.000f, t);
        const float a    = (0.55f + fade * 0.45f);

        const D2D1_ROUNDED_RECT bar = {
            D2D1::RectF(x, cy - barH / 2.0f, x + barW, cy + barH / 2.0f),
            1.5f, 1.5f
        };
        ID2D1SolidColorBrush* wb = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a), &wb);
        if (wb) { m_dcRT->FillRoundedRectangle(bar, wb); wb->Release(); }
    }
}

// ==================================================================
// drawProcessing â€” gradient-fade sweeping spinner + label
// ==================================================================
void Overlay::drawProcessing(float cx, float cy)
{
    m_spinAngle += 5.5f;
    if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;

    constexpr float r       = 13.5f;
    constexpr int   SEGS    = 32;    // arc divided into segments for gradient fade
    constexpr float SWEEP   = 285.0f;

    for (int s = 0; s < SEGS; ++s) {
        const float t0 = static_cast<float>(s)     / SEGS;
        const float t1 = static_cast<float>(s + 1) / SEGS;

        const float a0 = (m_spinAngle - 90.0f + t0 * SWEEP) * (PI / 180.0f);
        const float a1 = (m_spinAngle - 90.0f + t1 * SWEEP) * (PI / 180.0f);

        // Fade: tail is transparent, leading edge is opaque
        const float opacity = t0 * t0;   // quadratic â€” fade from tail

        ID2D1PathGeometry* path = nullptr;
        m_d2dFactory->CreatePathGeometry(&path);
        if (!path) continue;

        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(path->Open(&sink))) {
            const D2D1_POINT_2F p0 = {cx + r * std::cos(a0), cy + r * std::sin(a0)};
            const D2D1_POINT_2F p1 = {cx + r * std::cos(a1), cy + r * std::sin(a1)};
            const bool largeArc    = (SWEEP / SEGS) > 180.0f;

            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                p1, D2D1::SizeF(r, r), 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                largeArc ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();

            ID2D1SolidColorBrush* b = nullptr;
            m_dcRT->CreateSolidColorBrush(
                D2D1::ColorF(0.545f, 0.361f, 0.965f, opacity), &b);
            if (b) { m_dcRT->DrawGeometry(path, b, 2.4f); b->Release(); }
        }
        path->Release();
    }

    // Bright tip dot at the leading edge
    {
        const float tipA = (m_spinAngle - 90.0f + SWEEP) * (PI / 180.0f);
        const float tx = cx + r * std::cos(tipA);
        const float ty = cy + r * std::sin(tipA);

        ID2D1RadialGradientBrush* rgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.78f, 0.62f, 1.f, 1.0f)},
            {1.0f, D2D1::ColorF(0.78f, 0.62f, 1.f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(tx, ty), D2D1::Point2F(0.f, 0.f), 5.f, 5.f),
                stops, &rgb);
            if (rgb) {
                m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 5.f, 5.f), rgb);
                rgb->Release();
            }
            stops->Release();
        }
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.82f, 1.f, 1.0f), &b);
        if (b) {
            m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 2.2f, 2.2f), b);
            b->Release();
        }
    }

    // "transcribingâ€¦" label to the right of the spinner
    if (m_textFormat) {
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.68f, 0.80f, 0.90f), &b);
        if (b) {
            const D2D1_RECT_F tr = D2D1::RectF(cx + 8.0f, cy - 10.0f,
                                                 PILL_W - 14.0f, cy + 10.0f);
            m_dcRT->DrawTextW(L"transcribing\u2026", 13, m_textFormat, tr, b);
            b->Release();
        }
    }
}

// ==================================================================
// drawDone â€” scale-in green circle + animated checkmark
// ==================================================================
void Overlay::drawDone(float cx, float cy)
{
    m_stateAnim = clamp01(m_stateAnim + STATE_SPD);
    const float s = easeOut(m_stateAnim);
    const float r = 14.0f * s;

    // Outer glow
    {
        ID2D1RadialGradientBrush* rgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.063f, 0.725f, 0.506f, 0.30f * s)},
            {1.0f, D2D1::ColorF(0.063f, 0.725f, 0.506f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(cx, cy), D2D1::Point2F(0.f, 0.f),
                    r * 2.0f, r * 2.0f),
                stops, &rgb);
            if (rgb) {
                m_dcRT->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 2.0f, r * 2.0f), rgb);
                rgb->Release();
            }
            stops->Release();
        }
    }
    // Filled circle
    {
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &b);
        if (b) {
            m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
            b->Release();
        }
    }
    // Checkmark (two strokes, drawn progressively with stateAnim)
    if (s > 0.3f) {
        const float prog = clamp01((s - 0.3f) / 0.7f);
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.0f), &b);
        if (b && prog > 0.0f) {
            // Short left leg: (-5,-0) â†’ (-1.5, 4)
            // Long right leg: (-1.5, 4) â†’ (6, -5)
            const D2D1_POINT_2F p0 = {cx - 5.5f, cy + 0.5f};
            const D2D1_POINT_2F p1 = {cx - 1.5f, cy + 4.5f};
            const D2D1_POINT_2F p2 = {cx + 6.0f, cy - 5.0f};

            // First half of checkmark
            if (prog <= 0.5f) {
                const float t   = prog * 2.0f;
                const D2D1_POINT_2F mid = {
                    lerp(p0.x, p1.x, t), lerp(p0.y, p1.y, t)};
                m_dcRT->DrawLine(p0, mid, b, 2.0f);
            } else {
                m_dcRT->DrawLine(p0, p1, b, 2.0f);
                const float t   = (prog - 0.5f) * 2.0f;
                const D2D1_POINT_2F mid = {
                    lerp(p1.x, p2.x, t), lerp(p1.y, p2.y, t)};
                m_dcRT->DrawLine(p1, mid, b, 2.0f);
            }
            b->Release();
        }
    }
}

// ==================================================================
// drawError â€” scale-in red circle + X symbol
// ==================================================================
void Overlay::drawError(float cx, float cy)
{
    m_stateAnim = clamp01(m_stateAnim + STATE_SPD);
    const float s = easeOut(m_stateAnim);
    const float r = 14.0f * s;

    // Outer glow
    {
        ID2D1RadialGradientBrush* rgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(0.937f, 0.267f, 0.267f, 0.30f * s)},
            {1.0f, D2D1::ColorF(0.937f, 0.267f, 0.267f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(cx, cy), D2D1::Point2F(0.f, 0.f),
                    r * 2.0f, r * 2.0f),
                stops, &rgb);
            if (rgb) {
                m_dcRT->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 2.0f, r * 2.0f), rgb);
                rgb->Release();
            }
            stops->Release();
        }
    }
    // Filled circle
    {
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(0.937f, 0.267f, 0.267f, 1.0f), &b);
        if (b) {
            m_dcRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
            b->Release();
        }
    }
    // X symbol
    if (s > 0.4f) {
        ID2D1SolidColorBrush* b = nullptr;
        m_dcRT->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.0f), &b);
        if (b) {
            const float o = 5.0f * s;
            m_dcRT->DrawLine({cx - o, cy - o}, {cx + o, cy + o}, b, 2.0f);
            m_dcRT->DrawLine({cx + o, cy - o}, {cx - o, cy + o}, b, 2.0f);
            b->Release();
        }
    }
}

// ==================================================================
// onTimer â€” update animations then draw
// ==================================================================
void Overlay::onTimer()
{
    if (m_dismissing) {
        m_appearAnim -= DISMISS_SPD;
        if (m_appearAnim <= 0.0f) {
            m_appearAnim  = 0.0f;
            m_dismissing  = false;
            ShowWindow(m_hwnd, SW_HIDE);
            return;
        }
    } else {
        m_appearAnim = std::min(m_appearAnim + APPEAR_SPD, 1.0f);
    }

    // Auto-hide after Done/Error hold frames
    const OverlayState state = m_state.load(std::memory_order_acquire);
    if ((state == OverlayState::Done || state == OverlayState::Error)
        && m_flashFrames > 0) {
        --m_flashFrames;
        if (m_flashFrames == 0)
            setState(OverlayState::Hidden);
    }

    draw();
}

// ==================================================================
// WndProc
// ==================================================================
LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) {
        auto* self = reinterpret_cast<Overlay*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && wp == static_cast<WPARAM>(TIMER_ID)) {
            self->onTimer();
            return 0;
        }
    }
    // WM_PAINT: do nothing â€” UpdateLayeredWindow handles compositing
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ==================================================================
// shutdown
// ==================================================================
void Overlay::shutdown()
{
    if (m_hwnd) { KillTimer(m_hwnd, TIMER_ID); DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    if (m_textFormat)  { m_textFormat->Release();  m_textFormat  = nullptr; }
    if (m_dwFactory)   { m_dwFactory->Release();   m_dwFactory   = nullptr; }
    if (m_dcRT)        { m_dcRT->Release();        m_dcRT        = nullptr; }
    if (m_d2dFactory)  { m_d2dFactory->Release();  m_d2dFactory  = nullptr; }
    releaseGDIResources();
}


