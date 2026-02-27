// formatter.cpp — four-pass speech-to-text formatter
#include <windows.h>
#include "formatter.h"
#include <regex>
#include <algorithm>
#include <cctype>
#include <cwctype>

// All regex objects are compiled ONCE when the module is loaded.
// Never construct std::regex inside FormatTranscription() — it is called
// on a hot path after every transcription.
namespace {

// Pass 1 — fillers safe to strip everywhere
const std::vector<std::wregex> FILLERS_GLOBAL = {
    std::wregex(LR"(\b(um|uh|ah|er|hmm)\b,?\s*)", std::regex::icase),
    std::wregex(LR"(\byou know,?\s+)",             std::regex::icase),
};

// Pass 2 — fillers safe ONLY at the start of a sentence (^ anchored).
// "so" in "Sort the array, so values are ordered" must survive.
// "like" in "like the structure means…" must also survive. These patterns
// never run against mid-sentence content.
const std::vector<std::wregex> FILLERS_SENTENCE_START = {
    std::wregex(LR"(^(so|well|okay|ok|like),?\s+)",       std::regex::icase),
    std::wregex(LR"(^(basically|kind of|sort of)\s+)",     std::regex::icase),
    std::wregex(LR"(^(right|alright|now then),?\s+)",      std::regex::icase),
};

const std::wregex RE_MULTI_SPACE{LR"(\s{2,})"};
const std::wregex RE_TRIM{LR"(^\s+|\s+$)"};
const std::wregex RE_LEADING_PUNCT{LR"(^[,;:.]\s*)"};

// Coding-mode voice commands
const std::wregex RE_CAMEL {LR"(^camel\s+case\s+(.+)$)", std::regex::icase};
const std::wregex RE_SNAKE {LR"(^snake\s+case\s+(.+)$)", std::regex::icase};
const std::wregex RE_ALLCAP{LR"(^all\s+caps?\s+(.+)$)",  std::regex::icase};

} // namespace

// ------------------------------------------------------------------

static std::wstring toWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

static std::string toNarrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                   nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                         s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring removeFillers(std::wstring t)
{
    for (auto& r : FILLERS_GLOBAL)
        t = std::regex_replace(t, r, L" ");
    for (auto& r : FILLERS_SENTENCE_START)
        t = std::regex_replace(t, r, L"");
    return t;
}

static std::wstring cleanup(std::wstring t)
{
    t = std::regex_replace(t, RE_MULTI_SPACE,   L" ");
    t = std::regex_replace(t, RE_TRIM,          L"");
    t = std::regex_replace(t, RE_LEADING_PUNCT, L"");
    if (!t.empty())
        t[0] = static_cast<wchar_t>(::towupper(t[0]));
    return t;
}

static std::wstring fixPunctuation(std::wstring t)
{
    if (t.empty()) return t;
    wchar_t last = t.back();
    if (last != L'.' && last != L'?' && last != L'!' && last != L':')
        t += L'.';
    return t;
}

static std::wstring toCamelCase(const std::wstring& s)
{
    std::wstring r;
    bool cap = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') { cap = true; continue; }
        r += cap ? static_cast<wchar_t>(::towupper(c))
                 : static_cast<wchar_t>(::towlower(c));
        cap = false;
    }
    return r;
}

static std::wstring toSnakeCase(const std::wstring& s)
{
    std::wstring r;
    for (wchar_t c : s)
        r += (c == L' ' || c == L'\t') ? L'_' : static_cast<wchar_t>(::towlower(c));
    return r;
}

static std::wstring applyCodingTransforms(std::wstring t)
{
    std::wsmatch m;

    if (std::regex_match(t, m, RE_CAMEL))
        return toCamelCase(m[1].str());

    if (std::regex_match(t, m, RE_SNAKE))
        return toSnakeCase(m[1].str());

    if (std::regex_match(t, m, RE_ALLCAP)) {
        std::wstring r = toSnakeCase(m[1].str());
        std::transform(r.begin(), r.end(), r.begin(), ::towupper);
        return r;
    }

    // Identifiers and code snippets don't end with periods.
    if (!t.empty() && t.back() == L'.') t.pop_back();
    return t;
}

// ------------------------------------------------------------------

std::string FormatTranscription(const std::string& raw, AppMode mode)
{
    std::wstring t = toWide(raw);
    t = removeFillers(t);    // Pass 1
    t = cleanup(t);          // Pass 2
    t = fixPunctuation(t);   // Pass 3
    if (mode == AppMode::CODING)
        t = applyCodingTransforms(t);  // Pass 4

    return toNarrow(t);
}
