// overlay.cpp — Direct2D pill overlay (DC render target + UpdateLayeredWindow)
//
// Key improvement over the old HwndRenderTarget approach:
//   • ID2D1DCRenderTarget renders into a 32-bit DIB with premultiplied alpha.
//   • UpdateLayeredWindow composites the DIB using per-pixel alpha — the area
//     outside the rounded pill is fully transparent with zero bleed/outline.
//   • SetLayeredWindowAttributes(LWA_ALPHA) is NOT used, which was the cause
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

    // Window class — no background brush; we paint everything ourselves
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FLOWON_OVERLAY";
    wc.hbrBackground = nullptr;           // ← critical: no GDI background paint
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
    edgeGlow.init(hInst);
    return true;
}

// ==================================================================
// GDI resources — 32-bit DIB + memory DC for UpdateLayeredWindow
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
// positionWindow — centres pill near bottom of primary monitor
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
    edgeGlow.setState(s);
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
// draw — one frame
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

    // ---- Pill background (organic pastel, no hard container) ----
    const D2D1_ROUNDED_RECT pill = {
        D2D1::RectF(ox + 1.5f, oy + 1.5f, ox + scaledW - 1.5f, oy + scaledH - 1.5f),
        PILL_R * scale, PILL_R * scale
    };

    // Base glass tint
    fillRect(pill, D2D1::ColorF(0.06f, 0.06f, 0.08f, 0.22f * opacity));

    // Soft, shifting pastel blobs clipped to the rounded pill
    ID2D1RoundedRectangleGeometry* pillGeo = nullptr;
    m_d2dFactory->CreateRoundedRectangleGeometry(
        D2D1::RoundedRect(pill.rect, pill.radiusX, pill.radiusY),
        &pillGeo);

    if (pillGeo) {
        ID2D1Layer* layer = nullptr;
        m_dcRT->CreateLayer(&layer);
        if (layer) {
            D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters();
            lp.contentBounds = D2D1::RectF(ox, oy, ox + scaledW, oy + scaledH);
            lp.geometricMask = pillGeo;
            lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
            lp.opacity = opacity;

            m_dcRT->PushLayer(lp, layer);

            const float pcx = ox + scaledW * 0.5f;
            const float pcy = oy + scaledH * 0.5f;
            const float p   = m_bgPhase;

            auto drawBlob = [&](float x, float y, float rx, float ry, D2D1_COLOR_F c0) {
                ID2D1RadialGradientBrush* rgb = nullptr;
                ID2D1GradientStopCollection* stops = nullptr;
                D2D1_GRADIENT_STOP gs[2] = {
                    {0.0f, c0},
                    {1.0f, D2D1::ColorF(c0.r, c0.g, c0.b, 0.0f)}
                };
                m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
                if (stops) {
                    m_dcRT->CreateRadialGradientBrush(
                        D2D1::RadialGradientBrushProperties(
                            D2D1::Point2F(x, y), D2D1::Point2F(0.f, 0.f), rx, ry),
                        stops, &rgb);
                    if (rgb) {
                        m_dcRT->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), rgb);
                        rgb->Release();
                    }
                    stops->Release();
                }
            };

            // Pastel palette (mint, peach, lavender)
            drawBlob(pcx + 72.0f * std::sin(p * 0.65f), pcy - 14.0f + 10.0f * std::cos(p * 0.82f), 92.0f, 58.0f,
                     D2D1::ColorF(0.64f, 0.93f, 0.86f, 0.22f));
            drawBlob(pcx - 54.0f * std::cos(p * 0.58f), pcy + 12.0f + 12.0f * std::sin(p * 0.74f), 78.0f, 52.0f,
                     D2D1::ColorF(0.98f, 0.78f, 0.72f, 0.20f));
            drawBlob(pcx + 18.0f * std::sin(p * 0.91f), pcy + 22.0f * std::cos(p * 0.63f), 70.0f, 46.0f,
                     D2D1::ColorF(0.80f, 0.74f, 0.98f, 0.20f));

            m_dcRT->PopLayer();
            layer->Release();
        }
        pillGeo->Release();
    }

    // Light top sheen
    {
        const D2D1_ROUNDED_RECT hi = {
            D2D1::RectF(ox + 1.5f, oy + 1.5f, ox + scaledW - 1.5f, oy + scaledH * 0.36f),
            PILL_R * scale, PILL_R * scale
        };
        ID2D1LinearGradientBrush* lgb = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.050f * opacity)},
            {1.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.0f)}
        };
        m_dcRT->CreateGradientStopCollection(gs, 2, &stops);
        if (stops) {
            m_dcRT->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(ox, oy + 1.5f),
                    D2D1::Point2F(ox, oy + scaledH * 0.36f)),
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
// present — blit DIB to screen via UpdateLayeredWindow
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
// drawRecording — animated waveform + pulsing record dot
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
    constexpr float barW    = 3.8f;
    constexpr float barGap  = 1.9f;
    const     float totalW  = WAVE_SAMPLES * (barW + barGap);
    const     float startX  = cx - totalW / 2.0f + 18.0f;
    const     float maxBarH = PILL_H - 14.0f;

    for (int i = 0; i < WAVE_SAMPLES; ++i) {
        const float rmsVal = m_smoothed[i];

        // Edge fade envelope
        float fade = 1.0f;
        if      (i < 5)                    fade = static_cast<float>(i) / 5.0f;
        else if (i > WAVE_SAMPLES - 6)     fade = static_cast<float>(WAVE_SAMPLES - 1 - i) / 5.0f;
        fade = easeOut(fade);

        float barH = rmsVal * maxBarH * 4.1f + 3.0f;
        barH = std::min(barH * fade, maxBarH);
        barH = std::max(barH, 2.0f);

        const float x = startX + static_cast<float>(i) * (barW + barGap);
        // Pastel gradient (mint -> peach -> lavender), slowly shifting over time
        const float t    = static_cast<float>(i) / static_cast<float>(WAVE_SAMPLES - 1);
        const float wob  = 0.5f + 0.5f * std::sin(m_bgPhase * 0.8f + t * 6.2831853f);
        const float amp  = clamp01(rmsVal * 7.0f);

        // Palette anchors
        const float mR = 0.64f, mG = 0.93f, mB = 0.86f;  // mint
        const float pR = 0.98f, pG = 0.78f, pB = 0.72f;  // peach
        const float lR = 0.80f, lG = 0.74f, lB = 0.98f;  // lavender

        float r, g, b;
        if (wob < 0.5f) {
            const float u = wob * 2.0f;
            r = lerp(mR, pR, u);
            g = lerp(mG, pG, u);
            b = lerp(mB, pB, u);
        } else {
            const float u = (wob - 0.5f) * 2.0f;
            r = lerp(pR, lR, u);
            g = lerp(pG, lG, u);
            b = lerp(pB, lB, u);
        }

        // Boost saturation/brightness slightly with amplitude
        r = clamp01(r + amp * 0.08f);
        g = clamp01(g + amp * 0.06f);
        b = clamp01(b + amp * 0.10f);

        const float a = (0.48f + fade * 0.52f);

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
// drawProcessing — gradient-fade sweeping spinner + label
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
        const float opacity = t0 * t0;   // quadratic — fade from tail

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

    // "transcribing…" label to the right of the spinner
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
// drawDone — scale-in green circle + animated checkmark
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
            // Short left leg: (-5,-0) → (-1.5, 4)
            // Long right leg: (-1.5, 4) → (6, -5)
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
// drawError — scale-in red circle + X symbol
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
// onTimer — update animations then draw
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

    m_bgPhase += 0.018f;
    if (m_bgPhase > 100000.0f) m_bgPhase = 0.0f;

    draw();
    edgeGlow.onTimer();
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
    // WM_PAINT: do nothing — UpdateLayeredWindow handles compositing
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
    edgeGlow.shutdown();
}

// ==================================================================
// ScreenEdgeGlow � full-screen gradient edge effect
// ==================================================================

bool ScreenEdgeGlow::createResources()
{
    m_screenW = GetSystemMetrics(SM_CXSCREEN);
    m_screenH = GetSystemMetrics(SM_CYSCREEN);

    // D2D factory
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory)))
        return false;

    // DC render target with premultiplied alpha for UpdateLayeredWindow
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRT)))
        return false;

    // Full-screen 32-bit DIB
    BITMAPINFO bmi            = {};
    bmi.bmiHeader.biSize      = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth     = m_screenW;
    bmi.bmiHeader.biHeight    = -m_screenH;  // top-down
    bmi.bmiHeader.biPlanes    = 1;
    bmi.bmiHeader.biBitCount  = 32;
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

bool ScreenEdgeGlow::init(HINSTANCE hInst)
{
    if (!createResources()) return false;

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"FLOWON_EDGE_GLOW";
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // Full-screen, click-through, always-on-top, layered
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FLOWON_EDGE_GLOW", L"", WS_POPUP,
        0, 0, m_screenW, m_screenH,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    // Start hidden
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void ScreenEdgeGlow::setState(OverlayState s)
{
    m_state = s;
    if (s == OverlayState::Hidden || s == OverlayState::Done || s == OverlayState::Error) {
        m_dismissing = true;
    } else {
        m_dismissing = false;
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_screenW, m_screenH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void ScreenEdgeGlow::drawEdgeBand(float x0, float y0, float x1, float y1,
                                   bool horizontal, bool invertDir,
                                   D2D1_COLOR_F color, float alpha)
{
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[3] = {
        {0.0f, D2D1::ColorF(color.r, color.g, color.b, alpha * 0.55f)},
        {0.35f, D2D1::ColorF(color.r, color.g, color.b, alpha * 0.22f)},
        {1.0f, D2D1::ColorF(color.r, color.g, color.b, 0.0f)}
    };
    if (invertDir) {
        gs[0].color.a = 0.0f;
        gs[2].color.a = alpha * 0.55f;
        gs[1].color.a = alpha * 0.22f;
        // swap stop positions
        gs[0].position = 0.0f;
        gs[1].position = 0.65f;
        gs[2].position = 1.0f;
    }

    if (FAILED(m_dcRT->CreateGradientStopCollection(gs, 3, &stops))) return;

    ID2D1LinearGradientBrush* lgb = nullptr;
    D2D1_POINT_2F pt0, pt1;
    if (horizontal) {
        pt0 = D2D1::Point2F(x0, invertDir ? y1 : y0);
        pt1 = D2D1::Point2F(x0, invertDir ? y0 : y1);
    } else {
        pt0 = D2D1::Point2F(invertDir ? x1 : x0, y0);
        pt1 = D2D1::Point2F(invertDir ? x0 : x1, y0);
    }

    m_dcRT->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(pt0, pt1), stops, &lgb);

    if (lgb) {
        m_dcRT->FillRectangle(D2D1::RectF(x0, y0, x1, y1), lgb);
        lgb->Release();
    }
    stops->Release();
}

void ScreenEdgeGlow::draw()
{
    if (!m_dcRT || !m_memDC) return;

    RECT rc = {0, 0, m_screenW, m_screenH};
    if (FAILED(m_dcRT->BindDC(m_memDC, &rc))) return;

    m_dcRT->BeginDraw();
    m_dcRT->Clear(D2D1::ColorF(0, 0, 0, 0));  // fully transparent

    // Breathing pulse: anim * (0.75 + 0.25 * sin(pulse))
    const float breathe = m_anim * (0.75f + 0.25f * std::sin(m_pulse));

    // Choose color based on state
    D2D1_COLOR_F color;
    switch (m_state) {
        case OverlayState::Recording:
            color = D2D1::ColorF(0.27f, 0.42f, 1.0f);   // Blue-indigo
            break;
        case OverlayState::Processing:
            color = D2D1::ColorF(0.55f, 0.27f, 1.0f);   // Purple
            break;
        case OverlayState::Done:
            color = D2D1::ColorF(0.10f, 0.82f, 0.46f);  // Green
            break;
        case OverlayState::Error:
            color = D2D1::ColorF(1.0f, 0.27f, 0.27f);   // Red
            break;
        default:
            color = D2D1::ColorF(0.27f, 0.42f, 1.0f);
            break;
    }

    const float W  = static_cast<float>(m_screenW);
    const float H  = static_cast<float>(m_screenH);
    const float D  = EDGE_DEPTH;

    // Top edge: gradient flows from top downward
    drawEdgeBand(0, 0, W, D, true, false, color, breathe);
    // Bottom edge: gradient flows from bottom upward
    drawEdgeBand(0, H - D, W, H, true, true, color, breathe);
    // Left edge: gradient flows from left rightward
    drawEdgeBand(0, 0, D, H, false, false, color, breathe);
    // Right edge: gradient flows from right leftward
    drawEdgeBand(W - D, 0, W, H, false, true, color, breathe);

    m_dcRT->EndDraw();
    present();
}

void ScreenEdgeGlow::present()
{
    POINT ptSrc   = {0, 0};
    POINT ptDst   = {0, 0};
    SIZE  szWnd   = {m_screenW, m_screenH};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, nullptr, &ptDst, &szWnd,
                        m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
}

void ScreenEdgeGlow::onTimer()
{
    if (m_dismissing) {
        m_anim -= DISMISS_SPD;
        if (m_anim <= 0.0f) {
            m_anim       = 0.0f;
            m_dismissing = false;
            ShowWindow(m_hwnd, SW_HIDE);
            return;
        }
    } else {
        if (m_anim < 1.0f) m_anim += APPEAR_SPD;
        if (m_anim > 1.0f) m_anim = 1.0f;
        m_pulse += PULSE_SPD;
    }

    draw();
}

void ScreenEdgeGlow::shutdown()
{
    if (m_hwnd)     { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    if (m_dcRT)     { m_dcRT->Release();     m_dcRT = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
    if (m_memDC)    { DeleteDC(m_memDC);     m_memDC = nullptr; }
    if (m_hBitmap)  { DeleteObject(m_hBitmap); m_hBitmap = nullptr; }
}

LRESULT CALLBACK ScreenEdgeGlow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


