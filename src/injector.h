#pragma once
#include <string>

// Injects text into the currently focused application.
// - <= 200 chars with no surrogate pairs → SendInput (per-char UNICODE events)
// - Otherwise → clipboard paste via Ctrl+V
// Must be called from the main Win32 thread only.
void InjectText(const std::wstring& text);
