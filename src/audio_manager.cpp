// audio_manager.cpp
// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include "audio_manager.h"
#include "overlay.h"        // for g_overlayPtr->pushRMS

#include "miniaudio.h"
#include "readerwriterqueue.h"
#include <cmath>

// ------------------------------------------------------------------
// Forward-declared global overlay pointer (defined in main.cpp).
// The audio callback runs on a time-critical thread; calling pushRMS
// is a single atomic store — safe from any thread.
// ------------------------------------------------------------------
extern Overlay* g_overlayPtr;

// ------------------------------------------------------------------
// Lock-free ring buffer: 30 seconds of 16 kHz mono PCM = 480 000 floats.
// Declared static so it lives for the duration of the process.
// ------------------------------------------------------------------
static moodycamel::ReaderWriterQueue<float> g_ring(16000 * 30);

// Internal helper struct so we can pass *this* through the C callback.
struct DeviceHolder {
    ma_device    device;
    AudioManager* owner;
};

static void data_callback(ma_device* dev, void* /*output*/,
                          const void* input, ma_uint32 frameCount)
{
    auto* h = static_cast<DeviceHolder*>(dev->pUserData);
    h->owner->onAudioData(static_cast<const float*>(input), frameCount);
}

// ------------------------------------------------------------------

void AudioManager::onAudioData(const float* data, size_t frames)
{
    float sumSq = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        sumSq += data[i] * data[i];
        if (!g_ring.try_enqueue(data[i]))
            m_dropped.fetch_add(1, std::memory_order_relaxed);
    }

    const float rms = (frames > 0) ? std::sqrt(sumSq / static_cast<float>(frames)) : 0.0f;
    m_rms.store(rms, std::memory_order_relaxed);

    if (g_overlayPtr)
        g_overlayPtr->pushRMS(rms);

    if (m_callback)
        m_callback(data, frames);
}

bool AudioManager::init(SampleCallback cb)
{
    m_callback = cb;
    m_recordBuffer.reserve(16000 * 30);   // pre-alloc once

    auto* h  = new DeviceHolder();
    h->owner = this;

    ma_device_config cfg       = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format         = ma_format_f32;
    cfg.capture.channels       = 1;
    cfg.sampleRate             = 16000;
    cfg.dataCallback           = data_callback;
    cfg.pUserData              = h;
    cfg.periodSizeInFrames     = 1600;    // 100 ms chunks

    if (ma_device_init(nullptr, &cfg, &h->device) != MA_SUCCESS) {
        delete h;
        return false;
    }

    m_device = h;
    return true;
}

bool AudioManager::startCapture()
{
    m_recordBuffer.clear();
    resetDropCounter();

    // Drain any stale samples left from a previous (cancelled) session.
    float dummy;
    while (g_ring.try_dequeue(dummy)) {}

    auto* h = static_cast<DeviceHolder*>(m_device);
    return ma_device_start(&h->device) == MA_SUCCESS;
}

void AudioManager::stopCapture()
{
    auto* h = static_cast<DeviceHolder*>(m_device);
    if (h) ma_device_stop(&h->device);
}

std::vector<float> AudioManager::drainBuffer()
{
    float s;
    while (g_ring.try_dequeue(s))
        m_recordBuffer.push_back(s);
    return m_recordBuffer;
}

void AudioManager::shutdown()
{
    if (m_device) {
        auto* h = static_cast<DeviceHolder*>(m_device);
        ma_device_uninit(&h->device);
        delete h;
        m_device = nullptr;
    }
}
