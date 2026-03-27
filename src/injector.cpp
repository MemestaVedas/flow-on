// injector.cpp — clipboard-first text injection (WhisperFlow-style reliability)
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
    // Save existing clipboard contents so we can restore after injection
    std::wstring savedClipboard;
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* pszText = static_cast<const wchar_t*>(GlobalLock(hData));
            if (pszText) {
                savedClipboard = pszText;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }

    // Set our text on the clipboard
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
    }
    CloseClipboard();

    Sleep(15);  // Brief delay for clipboard propagation

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('V',         0, 0, 0);
    keybd_event('V',         0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);

    // Restore the user's original clipboard after a short delay
    Sleep(50);
    if (!savedClipboard.empty() && OpenClipboard(nullptr)) {
        EmptyClipboard();
        const size_t rBytes = (savedClipboard.size() + 1) * sizeof(wchar_t);
        HGLOBAL hRestore = GlobalAlloc(GMEM_MOVEABLE, rBytes);
        if (hRestore) {
            void* rPtr = GlobalLock(hRestore);
            if (rPtr) {
                std::memcpy(rPtr, savedClipboard.c_str(), rBytes);
                GlobalUnlock(hRestore);
            }
            SetClipboardData(CF_UNICODETEXT, hRestore);
        }
        CloseClipboard();
    }
}

void InjectText(const std::wstring& text)
{
    if (text.empty()) return;
    // Always use clipboard injection — universally compatible (WhisperFlow approach)
    InjectViaClipboard(text);
}
