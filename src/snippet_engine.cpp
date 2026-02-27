// snippet_engine.cpp
#include "snippet_engine.h"
#include <algorithm>
#include <cctype>

std::string SnippetEngine::apply(const std::string& text) const
{
    if (m_snippets.empty()) return text;

    std::string result = text;
    std::string lower  = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (auto& [trigger, value] : m_snippets) {
        std::string lt = trigger;
        std::transform(lt.begin(), lt.end(), lt.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        size_t pos = 0;
        while ((pos = lower.find(lt, pos)) != std::string::npos) {
            result.replace(pos, trigger.size(), value);
            lower.replace(pos, trigger.size(), std::string(value.size(), ' '));
            pos += value.size();
        }
    }
    return result;
}

// ------------------------------------------------------------------

#include <windows.h>
#include "formatter.h"   // AppMode

AppMode DetectModeFromActiveWindow()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return AppMode::PROSE;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return AppMode::PROSE;

    wchar_t exePath[MAX_PATH] = {};
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc) {
        DWORD sz = MAX_PATH;
        QueryFullProcessImageNameW(proc, 0, exePath, &sz);
        CloseHandle(proc);
    }

    // Known code editors / terminals — matched as a substring of the full path.
    static const wchar_t* CODE_APPS[] = {
        L"Code.exe",           // VS Code (stable)
        L"Code - Insiders.exe",
        L"cursor.exe",         // Cursor AI
        L"nvim.exe",           // Neovim
        L"vim.exe",
        L"WindowsTerminal.exe",
        L"devenv.exe",         // Visual Studio
        L"rider64.exe",        // JetBrains Rider
        L"clion64.exe",        // JetBrains CLion
        L"goland64.exe",       // JetBrains GoLand
        L"pycharm64.exe",
        L"idea64.exe",
        L"conhost.exe",
        L"wt.exe",
        L"powershell.exe",
        L"pwsh.exe",
        L"cmd.exe",
        L"git-bash.exe",
        L"mintty.exe",
    };

    std::wstring path(exePath);
    for (auto* name : CODE_APPS)
        if (path.find(name) != std::wstring::npos)
            return AppMode::CODING;

    return AppMode::PROSE;
}
