#pragma once
#include <vector>
#include <atomic>
#include <functional>

// Forward-declared so overlay.h never needs to be #included here
class Overlay;

class AudioManager {
public:
    using SampleCallback = std::function<void(const float*, size_t)>;

    // init() opens the microphone at 16 kHz mono.
    // cb is called from the miniaudio thread; keep it very short.
    bool init(SampleCallback cb);

    bool startCapture();    // Arms recording; drains any stale ring buffer
    void stopCapture();

    // Transfer all buffered samples since last startCapture() and return them.
    // Call once from the main thread immediately after stopCapture().
    std::vector<float> drainBuffer();

    void shutdown();

    // RMS of the last audio chunk — updated from the audio thread;
    // safe to read from any thread via relaxed load.
    float getRMS()            const { return m_rms.load(std::memory_order_relaxed); }
    int   getDroppedSamples() const { return m_dropped.load(std::memory_order_relaxed); }
    void  resetDropCounter()        { m_dropped.store(0, std::memory_order_relaxed); }

    // Called internally from the miniaudio callback — do not call directly.
    void onAudioData(const float* data, size_t frames);

private:
    void*          m_device   = nullptr;   // DeviceHolder* — opaque
    SampleCallback m_callback;
    std::atomic<int>   m_dropped{0};
    std::atomic<float> m_rms{0.0f};
    std::vector<float> m_recordBuffer;    // pre-allocated, 30 s max at 16 kHz
};
