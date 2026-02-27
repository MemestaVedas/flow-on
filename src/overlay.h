#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <atomic>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

enum class OverlayState { Hidden, Recording, Processing, Done, Error };

// Floating pill bar overlay — GPU-accelerated via Direct2D.
// Lives on the main thread (driven by WM_TIMER at ~60 fps).
class Overlay {
public:
    bool init(HINSTANCE hInst);
    void shutdown();

    void setState(OverlayState s);
    OverlayState getState() const { return m_state.load(std::memory_order_relaxed); }

    // Push latest RMS amplitude; called from the audio callback thread.
    // Uses an atomic store — safe from any thread.
    void pushRMS(float rms);

private:
    static constexpr int PILL_W       = 300;
    static constexpr int PILL_H       = 60;
    static constexpr int WAVE_SAMPLES = 48;
    static constexpr int TIMER_ID     = 42;

    HWND  m_hwnd = nullptr;
    std::atomic<OverlayState> m_state{OverlayState::Hidden};
    std::atomic<float>        m_latestRMS{0.0f};

    float m_wave[WAVE_SAMPLES] = {};
    int   m_waveHead    = 0;
    int   m_flashFrames = 0;
    float m_spinAngle   = 0.0f;

    ID2D1Factory*          m_d2dFactory   = nullptr;
    ID2D1HwndRenderTarget* m_renderTarget = nullptr;
    IDWriteFactory*        m_dwFactory    = nullptr;
    IDWriteTextFormat*     m_textFormat   = nullptr;

    bool createDeviceResources();
    void positionWindow();
    void onTimer();
    void draw();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
