// injector.cpp — injects a UTF-16 string into the focused application.
#include <windows.h>
#include "injector.h"
#include <vector>

// Returns true if `text` contains any UTF-16 surrogate code units.
// Surrogates represent characters outside the Basic Multilingual Plane
// (emoji, some CJK). Older apps crash on raw KEYEVENTF_UNICODE with them,
// so we fall back to the clipboard path instead.
static bool ContainsSurrogates(const std::wstring& text)
{
    for (wchar_t ch : text)
        if (ch >= 0xD800 && ch <= 0xDFFF)
            return true;
    return false;
}

// Places text on the clipboard and synthesises Ctrl+V.
// Works in virtually every app, including terminal emulators that reject
// SendInput Unicode events.
static void InjectViaClipboard(const std::wstring& text)
{
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            std::memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
        }
        SetClipboardData(CF_UNICODETEXT, hMem);
        // GlobalFree must NOT be called after SetClipboardData — the OS owns it now.
    }
    CloseClipboard();

    // Small delay so the target window has time to receive WM_DRAWCLIPBOARD
    // before we send the keystrokes.
    Sleep(30);

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('V',         0, 0, 0);
    keybd_event('V',         0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

void InjectText(const std::wstring& text)
{
    if (text.empty()) return;

    // Fall back to clipboard for long strings or text containing emoji.
    if (text.length() > 200 || ContainsSurrogates(text)) {
        InjectViaClipboard(text);
        return;
    }

    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);

    for (wchar_t ch : text) {
        INPUT inp      = {};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wScan   = ch;
        inp.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(inp);

        inp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(inp);
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}
