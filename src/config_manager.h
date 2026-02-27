#pragma once
#include <string>
#include <unordered_map>
#include "formatter.h"   // AppMode

// Persisted application settings.  Stored as JSON in:
//   %APPDATA%\FLOW-ON\settings.json
struct AppSettings {
    std::string hotkey           = "Alt+V";
    std::string modeStr          = "auto";   // "auto" | "prose" | "code"
    std::string model            = "tiny.en";
    bool        useGPU           = true;
    bool        startWithWindows = true;
    std::unordered_map<std::string, std::string> snippets = {
        { "insert email",     "you@yourdomain.com" },
        { "insert todo",      "// TODO: " },
        { "insert fixme",     "// FIXME: " },
    };
};

class ConfigManager {
public:
    // Loads %APPDATA%\FLOW-ON\settings.json; creates defaults if missing.
    bool load();

    // Persists current settings back to disk.
    bool save() const;

    const AppSettings& settings() const { return m_settings; }
    AppSettings&       settings()       { return m_settings; }

    // Apply / remove the HKCU Run registry key for start-with-Windows.
    void applyAutostart(const std::wstring& exePath) const;
    void removeAutostart() const;

private:
    AppSettings m_settings;
    std::wstring settingsPath() const;   // full path to settings.json
};
