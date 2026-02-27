#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <windows.h>

// WM_TRANSCRIPTION_DONE lParam is a heap-allocated std::string* the receiver
// must delete.

class Transcriber {
public:
    // modelPath: e.g. "models/ggml-tiny.en.bin" (relative to CWD or absolute).
    // Tries GPU first; falls back to CPU silently.
    bool init(const char* modelPath);
    void shutdown();

    // Non-blocking: spins up a worker thread that calls whisper_full, then
    // posts WM_TRANSCRIPTION_DONE to hwnd when done.
    // Returns false if already busy (drop this call — the FSM prevents double-
    // recording, but guard again here for safety).
    bool transcribeAsync(HWND hwnd, std::vector<float> pcm, UINT doneMsg);

    bool isBusy() const { return m_busy.load(std::memory_order_acquire); }

private:
    void* m_ctx = nullptr;              // whisper_context* — opaque
    std::atomic<bool> m_busy{false};
};
