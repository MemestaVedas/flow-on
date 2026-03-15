// transcriber.cpp — performance-tuned for i5-10210U (4C/8T, no CUDA)
#include "transcriber.h"
#include "whisper.h"
#include <thread>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <vector>
#include <windows.h>
#include <cstdio>

// ------------------------------------------------------------------
// Remove hallucinated repetitions from Whisper output.
// Whisper models sometimes get stuck in repetition loops. This detects
// and collapses repeated phrases at word, phrase, and character levels.
// ------------------------------------------------------------------
static std::string removeRepetitions(const std::string& text)
{
    if (text.size() < 10) return text;

    std::string result = text;
    bool changed = true;
    int iterations = 0;
    const int MAX_ITERATIONS = 5;

    while (changed && iterations < MAX_ITERATIONS) {
        changed = false;
        iterations++;

        // Strategy 1: Detect exact word-level repetitions (3+ times)
        // Example: "hello hello hello" -> "hello"
        {
            std::vector<std::string> words;
            std::istringstream iss(result);
            std::string word;
            while (iss >> word) words.push_back(word);

            if (words.size() >= 6) {
                for (size_t i = 0; i < words.size() - 2; i++) {
                    // Check for 3+ identical consecutive words
                    if (words[i] == words[i+1] && words[i] == words[i+2]) {
                        size_t repeatEnd = i + 3;
                        while (repeatEnd < words.size() && words[repeatEnd] == words[i]) repeatEnd++;
                        // Collapse to single occurrence
                        words.erase(words.begin() + i + 1, words.begin() + repeatEnd);
                        changed = true;
                        break;
                    }
                }
            }

            if (changed) {
                result.clear();
                for (size_t i = 0; i < words.size(); i++) {
                    if (i > 0) result += " ";
                    result += words[i];
                }
                continue;
            }
        }

        // Strategy 2: Detect phrase-level repetitions (2+ times)
        // Example: "at the finger at the finger" -> "at the finger"
        {
            std::vector<std::string> words;
            std::istringstream iss(result);
            std::string word;
            while (iss >> word) words.push_back(word);

            for (size_t phraseLen = 2; phraseLen <= words.size() / 2 && phraseLen <= 8; phraseLen++) {
                for (size_t i = 0; i <= words.size() - phraseLen * 2; i++) {
                    bool match = true;
                    for (size_t j = 0; j < phraseLen && match; j++) {
                        if (words[i + j] != words[i + phraseLen + j]) match = false;
                    }
                    if (match) {
                        // Found repeated phrase, collapse to single
                        words.erase(words.begin() + i + phraseLen, words.begin() + i + phraseLen * 2);
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }

            if (changed) {
                result.clear();
                for (size_t i = 0; i < words.size(); i++) {
                    if (i > 0) result += " ";
                    result += words[i];
                }
                continue;
            }
        }

        // Strategy 3: Detect substring repetitions at character level
        // Example: "the finger the finger the finger" -> "the finger"
        {
            const int n = static_cast<int>(result.size());
            for (int unitLen = 5; unitLen <= n / 2 && unitLen <= 50; ++unitLen) {
                for (int start = 0; start <= n - unitLen * 2; ++start) {
                    std::string unit = result.substr(start, unitLen);
                    // Trim trailing space for comparison
                    while (!unit.empty() && unit.back() == ' ') unit.pop_back();
                    if (unit.size() < 4) continue;

                    int pos = start;
                    int reps = 0;
                    while (pos + static_cast<int>(unit.size()) <= n) {
                        std::string candidate = result.substr(pos, unit.size());
                        while (!candidate.empty() && candidate.back() == ' ') candidate.pop_back();
                        if (candidate == unit) {
                            reps++;
                            pos += unit.size();
                            // Skip whitespace
                            while (pos < n && result[pos] == ' ') pos++;
                        } else {
                            break;
                        }
                    }

                    if (reps >= 2) {
                        // Remove repetitions, keep first occurrence
                        int removeStart = start + unit.size();
                        while (removeStart < n && result[removeStart] == ' ') removeStart++;
                        int removeEnd = pos;
                        result.erase(removeStart, removeEnd - removeStart);
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }
        }
    }

    // Final cleanup: remove excessive trailing repetitions of short words
    {
        std::vector<std::string> words;
        std::istringstream iss(result);
        std::string word;
        while (iss >> word) words.push_back(word);

        // Remove trailing single-character words or very short repeated endings
        while (words.size() > 3) {
            const std::string& last = words.back();
            const std::string& prev = words[words.size() - 2];
            
            // Remove if last word is very short and repeats previous
            if (last.size() <= 2 && last == prev) {
                words.pop_back();
            }
            // Remove nonsensical trailing fragments
            else if (last.size() <= 2 && words.size() > 5) {
                words.pop_back();
            }
            else break;
        }

        result.clear();
        for (size_t i = 0; i < words.size(); i++) {
            if (i > 0) result += " ";
            result += words[i];
        }
    }

    return result;
}

// ------------------------------------------------------------------
// Trim leading/trailing silence (below threshold) so Whisper processes
// only the voiced region. This is the single biggest win for short
// recordings with long pauses at start/end.
// ------------------------------------------------------------------
static void trimSilence(std::vector<float>& pcm, float threshold = 0.009f,
                        int guardSamples = 1200 /* 100 ms at 16 kHz */)
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
    if (!modelPath || !*modelPath) return false;
    if (m_ctx) shutdown();
    m_modelPath = modelPath;

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
    if (m_ctx) {
        m_lastUseMs.store(GetTickCount64(), std::memory_order_release);
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

void Transcriber::unloadIfIdle(uint64_t nowMs, uint64_t idleMs)
{
    if (!m_ctx) return;
    if (m_busy.load(std::memory_order_acquire)) return;
    const uint64_t last = m_lastUseMs.load(std::memory_order_acquire);
    if (nowMs - last < idleMs) return;
    shutdown();
}

bool Transcriber::transcribeAsync(HWND hwnd, std::vector<float> pcm, UINT doneMsg)
{
    // Single-flight guard — prevent re-entry
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return false;

    // Lazy re-init if model was unloaded while idle
    if (!m_ctx) {
        if (!m_modelPath.empty()) {
            if (!init(m_modelPath.c_str())) {
                m_busy.store(false, std::memory_order_release);
                return false;
            }
        } else {
            m_busy.store(false, std::memory_order_release);
            return false;
        }
    }

    m_lastUseMs.store(GetTickCount64(), std::memory_order_release);

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
        // For prompt dictation, keep context tight for speed
        const float durationSec = static_cast<float>(pcm.size()) / 16000.0f;
        if      (durationSec < 3.0f)  p.audio_ctx = 192;   // Fast for short clips
        else if (durationSec < 8.0f)  p.audio_ctx = 256;   // Short-medium
        else if (durationSec < 15.0f) p.audio_ctx = 384;   // Medium
        else if (durationSec < 30.0f) p.audio_ctx = 512;   // Longer
        else                          p.audio_ctx = 768;   // Cap for long audio

        // -- Decoding: use best_of=3 for better quality with base model --
        p.greedy.best_of    = 1;     // 1 candidate for speed
        p.temperature       = 0.0f;  // start greedy
        p.temperature_inc   = 0.2f;  // higher increment for better fallback
        p.entropy_thold     = 2.4f;  // slightly relaxed for base model
        p.logprob_thold     = -1.0f;
        p.no_speech_thold   = 0.6f;

        // -- Blank suppression --
        p.suppress_blank = true;
        p.suppress_nst   = true;    // suppress non-speech tokens

        // -- Token limit for short dictation (cap output, speeds up decode) --
        p.max_tokens = 96;

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

                // Safety net: remove hallucinated repetitions (only for longer outputs)
        if (result.size() > 120) {
            const std::string deduped = removeRepetitions(result);
            if (deduped != result) {
                OutputDebugStringA(("FLOW-ON: collapsed repetition: [" + result + "] -> [" + deduped + "]\n").c_str());
                result = deduped;
            }
        }

        m_lastUseMs.store(GetTickCount64(), std::memory_order_release);
        m_busy.store(false, std::memory_order_release);

        auto* s = new std::string(std::move(result));
        PostMessage(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(s));
    }).detach();

    return true;
}
