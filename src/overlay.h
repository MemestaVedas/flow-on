#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <atomic>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

enum class OverlayState { Hidden, Recording, Processing, Done, Error };

// ------------------------------------------------------------------
// ScreenEdgeGlow — full-screen layered window that paints translucent
// gradient bands along all 4 screen edges. Activated by Overlay state.
// Per-pixel alpha via UpdateLayeredWindow, fully click-through.
// ------------------------------------------------------------------
class ScreenEdgeGlow {
public:
    bool init(HINSTANCE hInst);
    void shutdown();

    // Call whenever the overlay state changes
    void setState(OverlayState s);

    // Called from Overlay::onTimer — advances animation and repaints
    void onTimer();

private:
    static constexpr int   TIMER_ID    = 55;
    static constexpr float EDGE_DEPTH  = 120.0f;  // pixels from edge
    static constexpr float APPEAR_SPD  = 0.07f;
    static constexpr float DISMISS_SPD = 0.06f;
    static constexpr float PULSE_SPD   = 0.035f;  // breathing frequency

    HWND    m_hwnd    = nullptr;
    HDC     m_memDC   = nullptr;
    HBITMAP m_hBitmap = nullptr;
    int     m_screenW = 0;
    int     m_screenH = 0;

    ID2D1Factory*        m_d2dFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRT       = nullptr;

    OverlayState m_state     = OverlayState::Hidden;
    float        m_anim      = 0.0f;   // [0,1] fade in/out
    float        m_pulse     = 0.0f;   // breathing phase (radians)
    bool         m_dismissing = false;

    bool createResources();
    void draw();
    void present();
    void drawEdgeBand(float x0, float y0, float x1, float y1,
                      bool horizontal,           // true = top/bottom, false = left/right
                      bool invertDir,            // true = gradient faces inward from bottom/right
                      D2D1_COLOR_F color, float alpha);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};

// ------------------------------------------------------------------
// Floating pill overlay — Direct2D DC render target + UpdateLayeredWindow
// for true per-pixel alpha (no "boxed outline" artefacts).
// Driven by WM_TIMER at ~60 fps on the main thread.
// ------------------------------------------------------------------
class Overlay {
public:
    bool init(HINSTANCE hInst);
    void shutdown();

    void setState(OverlayState s);
    OverlayState getState() const { return m_state.load(std::memory_order_relaxed); }

    // Thread-safe: called from audio callback.
    void pushRMS(float rms);

    ScreenEdgeGlow edgeGlow;

private:
    // ---- Layout constants ----
    static constexpr int   PILL_W        = 320;
    static constexpr int   PILL_H        = 64;
    static constexpr float PILL_R        = 22.0f;   // corner radius
    static constexpr int   WAVE_SAMPLES  = 50;
    static constexpr int   TIMER_ID      = 42;

    // ---- Animation speeds (fraction per frame @ 60 fps) ----
    static constexpr float APPEAR_SPD    = 0.14f;   // 0 → 1
    static constexpr float DISMISS_SPD   = 0.13f;   // 1 → 0
    static constexpr float STATE_SPD     = 0.16f;   // 0 → 1  (Done/Error circle)

    // ---- Window / GDI resources ----
    HWND    m_hwnd    = nullptr;
    HDC     m_memDC   = nullptr;
    HBITMAP m_hBitmap = nullptr;
    int     m_winX    = 0;
    int     m_winY    = 0;

    // ---- Shared state ----
    std::atomic<OverlayState> m_state{OverlayState::Hidden};
    std::atomic<float>        m_latestRMS{0.0f};

    // ---- Wave data ----
    float m_wave    [WAVE_SAMPLES] = {};   // raw RMS ring buffer
    float m_smoothed[WAVE_SAMPLES] = {};   // per-bar exponentially smoothed
    int   m_waveHead = 0;

    // ---- Animation variables ----
    float m_spinAngle    = 0.0f;   // Processing arc angle (degrees)
    float m_dotPulse     = 0.0f;   // Record dot pulse phase (radians)
    float m_idlePhase    = 0.0f;   // Idle sine wave phase
    float m_appearAnim   = 0.0f;   // Pill open/close scale  [0,1]
    float m_stateAnim    = 0.0f;   // Done/Error circle scale [0,1]
    int   m_flashFrames  = 0;      // Frames before auto-hide
    bool  m_dismissing   = false;

    // ---- Direct2D (DC Render Target — per-pixel alpha) ----
    ID2D1Factory*        m_d2dFactory     = nullptr;
    ID2D1DCRenderTarget* m_dcRT           = nullptr;
    IDWriteFactory*      m_dwFactory      = nullptr;
    IDWriteTextFormat*   m_textFormat     = nullptr;   // "transcribing…" label

    // ---- Helpers ----
    bool createDeviceResources();
    bool createGDIResources();
    void releaseGDIResources();
    void positionWindow();
    void onTimer();
    void draw();
    void present();

    void drawRecording (float cx, float cy);
    void drawProcessing(float cx, float cy);
    void drawDone      (float cx, float cy);
    void drawError     (float cx, float cy);

    // Convenience: create a solid brush, use it, release it.
    // Returns false if brush creation failed.
    inline void fillRect  (const D2D1_ROUNDED_RECT& r, D2D1_COLOR_F c);
    inline void strokeRect(const D2D1_ROUNDED_RECT& r, D2D1_COLOR_F c, float w);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
