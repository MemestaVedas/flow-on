// transcriber.cpp — performance-tuned for i5-10210U (4C/8T, no CUDA)
#include "transcriber.h"
#include "whisper.h"
#include <thread>
#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>
#include <cstdio>

// ------------------------------------------------------------------
// Remove hallucinated repetitions from Whisper output.
// Whisper's tiny model sometimes repeats the same phrase N times when
// it gets stuck in a greedy-decode loop. This detects the shortest
// repeating unit (length 4+ chars) and returns only one copy.
// ------------------------------------------------------------------
static std::string removeRepetitions(const std::string& text)
{
    const int n = static_cast<int>(text.size());
    if (n < 8) return text;  // too short to have meaningful repetitions

    // Try every possible repeating-unit length from small to large
    for (int unitLen = 4; unitLen <= n / 2; ++unitLen) {
        const std::string unit = text.substr(0, unitLen);
        // Check if the entire string (or nearly all of it) is just this unit repeated
        int pos = 0;
        int reps = 0;
        while (pos + unitLen <= n) {
            if (text.substr(pos, unitLen) == unit) {
                ++reps;
                pos += unitLen;
            } else {
                break;
            }
        }
        // If 3+ repetitions detected, the remainder is ≤ 1 unit (partial)
        // → collapse to a single occurrence
        if (reps >= 3 && pos >= n - unitLen) {
            // Trim trailing space from the unit before returning
            std::string clean = unit;
            while (!clean.empty() && clean.back() == ' ') clean.pop_back();
            return clean;
        }
    }
    return text;
}

// ------------------------------------------------------------------
// Trim leading/trailing silence (below threshold) so Whisper processes
// only the voiced region. This is the single biggest win for short
// recordings with long pauses at start/end.
// ------------------------------------------------------------------
static void trimSilence(std::vector<float>& pcm, float threshold = 0.008f,
                        int guardSamples = 1600 /* 100 ms at 16 kHz */)
{
    const int n = static_cast<int>(pcm.size());
    if (n == 0) return;

    // --- find first sample above threshold ---
    int start = 0;
    for (; start < n; ++start)
        if (std::fabs(pcm[start]) > threshold) break;

    // --- find last sample above threshold ---
    int end = n - 1;
    for (; end > start; --end)
        if (std::fabs(pcm[end]) > threshold) break;

    // Add a small guard window so we don't clip the onset/release
    start = std::max(0, start - guardSamples);
    end   = std::min(n - 1, end + guardSamples);

    if (start > 0 || end < n - 1) {
        pcm.erase(pcm.begin() + end + 1, pcm.end());
        pcm.erase(pcm.begin(), pcm.begin() + start);
    }
}

bool Transcriber::init(const char* modelPath)
{
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu    = true;
    cp.flash_attn = true;   // fused attention — less memory traffic
    
    // Performance: Enable GPU acceleration if available
    #ifdef GGML_USE_CUDA
    cp.use_gpu = true;
    #endif

    m_ctx = whisper_init_from_file_with_params(modelPath, cp);
    if (!m_ctx) {
        // GPU init failed — retry on CPU
        cp.use_gpu    = false;
        cp.flash_attn = true;
        m_ctx = whisper_init_from_file_with_params(modelPath, cp);
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
    // Single-flight guard — prevent re-entry
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return false;

    // Capture a copy of pcm before moving, validate it's not empty
    if (pcm.empty()) {
        m_busy.store(false, std::memory_order_release);
        auto* s = new std::string("");
        PostMessage(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(s));
        return true;
    }

    std::thread([this, hwnd, pcm = std::move(pcm), doneMsg]() mutable {
        auto* ctx = static_cast<whisper_context*>(m_ctx);

        // ============================================================
        // 1. Trim silence — avoid wasting compute on dead air
        // ============================================================
        trimSilence(pcm);

        // Bail out if the trimmed audio is too short (<0.25 s)
        if (pcm.size() < 4000) {
            m_busy.store(false, std::memory_order_release);
            auto* s = new std::string("");
            PostMessage(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(s));
            return;
        }

        // ============================================================
        // 2. Configure whisper for maximum throughput
        // ============================================================
        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        // -- Threading: reserve 1 core for the UI / OS --
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        p.n_threads   = std::max(1, hw - 1);   // 7 of 8 logical cores

        p.language    = "en";
        p.translate   = false;
        p.no_context  = true;

        // -- Segment / timestamp optimisations --
        p.single_segment   = true;
        p.no_timestamps    = true;
        p.token_timestamps = false;
        p.print_special    = false;
        p.print_progress   = false;
        p.print_realtime   = false;
        p.print_timestamps = false;

        // -- Audio context: scale with actual audio length --
        // Short clips (<5s) need less context; longer clips get more.
        // Optimized: use even smaller contexts for very short clips to maximize speed
        const float durationSec = static_cast<float>(pcm.size()) / 16000.0f;
        if      (durationSec < 2.0f)  p.audio_ctx = 64;   // Ultra-fast for short commands
        else if (durationSec < 5.0f)  p.audio_ctx = 128;  // Fast for brief dictation
        else if (durationSec < 10.0f) p.audio_ctx = 256;  // Balanced for medium clips
        else if (durationSec < 20.0f) p.audio_ctx = 384;  // Standard for longer clips
        else                          p.audio_ctx = 512;  // Max context for long audio

        // -- Decoding: single candidate, no fallback re-decode --
        p.greedy.best_of    = 1;     // 1 candidate instead of default 5 → ~5x faster decode
        p.temperature       = 0.0f;  // start greedy
        p.temperature_inc   = 0.2f;  // re-enable fallback: retries with sampling if repetition detected
        p.entropy_thold     = 2.0f;  // tighter: catch repetitive outputs earlier
        p.logprob_thold     = -1.0f;
        p.no_speech_thold   = 0.6f;

        // -- Blank suppression --
        p.suppress_blank = true;
        p.suppress_nst   = true;    // suppress non-speech tokens

        // -- Token limit for short dictation (cap output, speeds up decode) --
        p.max_tokens = 128;

        // -- No initial prompt (saves token encoding overhead) --
        p.initial_prompt = nullptr;

        // ============================================================
        // 3. Run inference
        // ============================================================
        whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size()));

        // ============================================================
        // 4. Collect result — only take FIRST segment due to single_segment=true
        //    If multiple segments exist (whisper.cpp bug?), deduplicate them.
        // ============================================================
        std::string result;
        const int nSeg = whisper_full_n_segments(ctx);
        
        if (nSeg > 0) {
            // Take only the first segment text
            const char* firstSeg = whisper_full_get_segment_text(ctx, 0);
            result = firstSeg ? firstSeg : "";
            
            // Log if we got unexpected multiple segments
            if (nSeg > 1) {
                char debugBuf[128];
                snprintf(debugBuf, sizeof(debugBuf), 
                    "WHISPER BUG: Got %d segments despite single_segment=true, using only first\n", nSeg);
                OutputDebugStringA(debugBuf);
            }
        }

        // Safety net: remove hallucinated repetitions (Whisper tiny greedy-loop bug)
        {
            const std::string deduped = removeRepetitions(result);
            if (deduped != result) {
                OutputDebugStringA(("FLOW-ON: collapsed repetition: [" + result + "] -> [" + deduped + "]\n").c_str());
                result = deduped;
            }
        }

        m_busy.store(false, std::memory_order_release);

        auto* s = new std::string(std::move(result));
        PostMessage(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(s));
    }).detach();

    return true;
}
