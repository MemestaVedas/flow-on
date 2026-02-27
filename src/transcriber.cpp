// transcriber.cpp
#include "transcriber.h"
#include "whisper.h"
#include <thread>
#include <algorithm>

bool Transcriber::init(const char* modelPath)
{
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = true;

    m_ctx = whisper_init_from_file_with_params(modelPath, cp);
    if (!m_ctx) {
        // GPU init failed — retry on CPU (GGML will log the failure internally)
        cp.use_gpu = false;
        m_ctx       = whisper_init_from_file_with_params(modelPath, cp);
    }
    return m_ctx != nullptr;
}

void Transcriber::shutdown()
{
    if (m_ctx) {
        whisper_free(static_cast<whisper_context*>(m_ctx));
        m_ctx = nullptr;
    }
}

bool Transcriber::transcribeAsync(HWND hwnd, std::vector<float> pcm, UINT doneMsg)
{
    // Single-flight guard — if already transcribing, drop this call.
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return false;

    std::thread([this, hwnd, pcm = std::move(pcm), doneMsg]() mutable {
        auto* ctx = static_cast<whisper_context*>(m_ctx);

        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        // Use all available logical cores for maximum speed
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        p.n_threads   = std::max(1, hw);  // Use ALL cores for transcription
        p.language    = "en";
        p.translate   = false;
        p.no_context  = true;

        // === SPEED OPTIMIZATIONS ===
        p.single_segment   = true;   // Process as single segment (faster for dictation)
        p.no_timestamps    = true;   // Skip timestamp generation (not needed, saves compute)
        p.token_timestamps = false;  // Skip token-level timestamps (not needed)
        p.audio_ctx        = 512;    // Reduce audio context (default 1500, lower = faster)
        p.max_len          = 0;      // No character limit (0 = unlimited)
        p.print_progress   = false;  // Disable progress prints (slight overhead)
        p.print_realtime   = false;  // Disable realtime prints

        // Seed the decoder with tech vocabulary — meaningfully improves
        // accuracy for developer dictation (camelCase, function names, etc.)
        p.initial_prompt =
            "camelCase, useState, useEffect, async, await, "
            "TypeScript, Python, function, const, return, "
            "interface, component, API, endpoint, database, "
            "nullptr, std, vector, string, struct";

        whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size()));

        std::string result;
        const int nSeg = whisper_full_n_segments(ctx);
        for (int i = 0; i < nSeg; ++i)
            result += whisper_full_get_segment_text(ctx, i);

        m_busy.store(false, std::memory_order_release);

        // Heap-allocate the string; the message handler owns it and must delete it.
        auto* s = new std::string(std::move(result));
        PostMessage(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(s));
    }).detach();

    return true;
}
