// overlay.cpp — Direct2D pill bar overlay
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <algorithm>
#include "overlay.h"

static constexpr int FPS_MS = 16;   // ~62.5 fps

// ------------------------------------------------------------------
// init
// ------------------------------------------------------------------
bool Overlay::init(HINSTANCE hInst)
{
    // D2D factory
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory)))
        return false;

    // DirectWrite factory + text format for "thinking…" label
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));

    if (m_dwFactory) {
        m_dwFactory->CreateTextFormat(
            L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &m_textFormat);
        if (m_textFormat) {
            m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Window class
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"FLOWON_OVERLAY";
    wc.hbrBackground  = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    // Layered, click-through, always-on-top popup
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FLOWON_OVERLAY", L"", WS_POPUP,
        0, 0, PILL_W, PILL_H,
        nullptr, nullptr, hInst, nullptr);

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!createDeviceResources()) return false;
    SetTimer(m_hwnd, TIMER_ID, FPS_MS, nullptr);
    return true;
}

// ------------------------------------------------------------------
// Device resources (can be recreated after D2DERR_RECREATE_TARGET)
// ------------------------------------------------------------------
bool Overlay::createDeviceResources()
{
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }

    auto rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    auto hrtp = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(PILL_W, PILL_H));

    return SUCCEEDED(m_d2dFactory->CreateHwndRenderTarget(rtp, hrtp, &m_renderTarget));
}

// ------------------------------------------------------------------
// Positioning
// ------------------------------------------------------------------
void Overlay::positionWindow()
{
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - PILL_W) / 2;
    int y = screenH - PILL_H - 72;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, PILL_W, PILL_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ------------------------------------------------------------------
// setState
// ------------------------------------------------------------------
void Overlay::setState(OverlayState s)
{
    m_state.store(s, std::memory_order_release);
    if (s == OverlayState::Hidden) {
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        if (s == OverlayState::Done || s == OverlayState::Error)
            m_flashFrames = 45;   // ~750 ms at 60 fps then auto-hide
        positionWindow();
    }
}

void Overlay::pushRMS(float rms)
{
    m_latestRMS.store(rms, std::memory_order_relaxed);
}

// ------------------------------------------------------------------
// draw — the entire frame
// ------------------------------------------------------------------
void Overlay::draw()
{
    const OverlayState state = m_state.load(std::memory_order_acquire);
    if (state == OverlayState::Hidden || !m_renderTarget) return;
    if (m_renderTarget->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;

    // Drain latest RMS into the rolling waveform buffer
    const float latestRMS = m_latestRMS.load(std::memory_order_relaxed);
    m_wave[m_waveHead] = latestRMS;
    m_waveHead = (m_waveHead + 1) % WAVE_SAMPLES;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));   // fully transparent

    // ---- Pill background ----
    const D2D1_ROUNDED_RECT pill = {
        D2D1::RectF(2.0f, 2.0f, PILL_W - 2.0f, PILL_H - 2.0f), 16.0f, 16.0f
    };

    ID2D1SolidColorBrush* bgBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.07f, 0.07f, 0.09f, 0.94f), &bgBrush);
    if (bgBrush) {
        m_renderTarget->FillRoundedRectangle(pill, bgBrush);
        bgBrush->Release();
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &borderBrush);
    if (borderBrush) {
        m_renderTarget->DrawRoundedRectangle(pill, borderBrush, 1.0f);
        borderBrush->Release();
    }

    const float cx = PILL_W / 2.0f;
    const float cy = PILL_H / 2.0f;

    // ---- State-specific drawing ----
    if (state == OverlayState::Recording) {

        // Red record dot (left side)
        ID2D1SolidColorBrush* dotBrush = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.95f, 0.25f, 0.25f, 1.0f), &dotBrush);
        if (dotBrush) {
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(20.0f, cy), 5.0f, 5.0f), dotBrush);
            dotBrush->Release();
        }

        // Waveform bars
        constexpr float barW   = 3.0f;
        constexpr float barGap = 2.5f;
        const     float totalW = WAVE_SAMPLES * (barW + barGap);
        const     float startX = cx - totalW / 2.0f + 12.0f;
        const     float maxBarH = PILL_H - 20.0f;

        ID2D1SolidColorBrush* waveBrush = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.45f, 0.75f, 1.0f, 1.0f), &waveBrush);

        if (waveBrush) {
            for (int i = 0; i < WAVE_SAMPLES; ++i) {
                const int   idx    = (m_waveHead + i) % WAVE_SAMPLES;
                const float rmsVal = m_wave[idx];

                // Minimum 3 px so bars stay visible at silence
                float barH = std::max(3.0f, rmsVal * maxBarH * 3.0f);

                // Fade edges for a smooth tapered look
                float fade = 1.0f;
                if (i < 4)                      fade = static_cast<float>(i) / 4.0f;
                else if (i > WAVE_SAMPLES - 5)  fade = static_cast<float>(WAVE_SAMPLES - i) / 4.0f;
                barH   = std::min(barH * fade, maxBarH);
                barH   = std::max(barH, 1.5f);

                const float x = startX + static_cast<float>(i) * (barW + barGap);
                const D2D1_ROUNDED_RECT bar = {
                    D2D1::RectF(x, cy - barH / 2.0f, x + barW, cy + barH / 2.0f),
                    1.5f, 1.5f
                };
                waveBrush->SetOpacity(0.5f + fade * 0.5f);
                m_renderTarget->FillRoundedRectangle(bar, waveBrush);
            }
            waveBrush->Release();
        }

    } else if (state == OverlayState::Processing) {

        // Spinning arc
        m_spinAngle += 6.0f;
        if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;

        ID2D1PathGeometry* path = nullptr;
        m_d2dFactory->CreatePathGeometry(&path);
        if (path) {
            ID2D1GeometrySink* sink = nullptr;
            path->Open(&sink);
            if (sink) {
                constexpr float r       = 14.0f;
                constexpr float pi      = 3.14159265f;
                const float startRad    = (m_spinAngle - 90.0f) * (pi / 180.0f);
                const float sweepRad    = 270.0f * (pi / 180.0f);
                const float endRad      = startRad + sweepRad;

                const D2D1_POINT_2F startPt = {
                    cx + r * std::cos(startRad), cy + r * std::sin(startRad) };
                sink->BeginFigure(startPt, D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(cx + r * std::cos(endRad),
                                  cy + r * std::sin(endRad)),
                    D2D1::SizeF(r, r), 0.0f,
                    D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                sink->Release();

                ID2D1SolidColorBrush* spinBrush = nullptr;
                m_renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(0.55f, 0.42f, 1.0f, 1.0f), &spinBrush);
                if (spinBrush) {
                    m_renderTarget->DrawGeometry(path, spinBrush, 2.5f);
                    spinBrush->Release();
                }
            }
            path->Release();
        }

        // "thinking..." label to the right of the spinner
        if (m_textFormat) {
            ID2D1SolidColorBrush* textBrush = nullptr;
            m_renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f), &textBrush);
            if (textBrush) {
                const D2D1_RECT_F tr = D2D1::RectF(cx + 5.0f, cy - 9.0f,
                                                     PILL_W - 12.0f, cy + 9.0f);
                m_renderTarget->DrawTextW(L"thinking…", 9, m_textFormat, tr, textBrush);
                textBrush->Release();
            }
        }

    } else if (state == OverlayState::Done) {

        // Green success flash
        ID2D1SolidColorBrush* b = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.18f, 0.82f, 0.38f, 1.0f), &b);
        if (b) {
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), 13.0f, 13.0f), b);
            b->Release();
        }

    } else if (state == OverlayState::Error) {

        // Red error flash
        ID2D1SolidColorBrush* b = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.90f, 0.22f, 0.22f, 1.0f), &b);
        if (b) {
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), 13.0f, 13.0f), b);
            b->Release();
        }
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        createDeviceResources();   // GPU reset — recreate and redraw next frame

    // Apply layered alpha; 0 = colour-key colour (unused), 240 = opacity
    SetLayeredWindowAttributes(m_hwnd, 0, 240, LWA_ALPHA);
}

// ------------------------------------------------------------------
// Timer
// ------------------------------------------------------------------
void Overlay::onTimer()
{
    const OverlayState state = m_state.load(std::memory_order_acquire);
    if ((state == OverlayState::Done || state == OverlayState::Error)
        && m_flashFrames > 0) {
        --m_flashFrames;
        if (m_flashFrames == 0)
            setState(OverlayState::Hidden);
    }
    draw();
}

// ------------------------------------------------------------------
// Window proc
// ------------------------------------------------------------------
LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) {
        auto* self = reinterpret_cast<Overlay*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && wp == TIMER_ID) {
            self->onTimer();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------
// shutdown
// ------------------------------------------------------------------
void Overlay::shutdown()
{
    if (m_hwnd) { KillTimer(m_hwnd, TIMER_ID); DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    if (m_textFormat)   { m_textFormat->Release();   m_textFormat   = nullptr; }
    if (m_dwFactory)    { m_dwFactory->Release();    m_dwFactory    = nullptr; }
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
    if (m_d2dFactory)   { m_d2dFactory->Release();   m_d2dFactory   = nullptr; }
}
