// config_manager.cpp
#include <windows.h>
#include <shlobj.h>        // SHGetFolderPathW
#include "config_manager.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ------------------------------------------------------------------
// Returns %APPDATA%\FLOW-ON\settings.json
// ------------------------------------------------------------------
std::wstring ConfigManager::settingsPath() const
{
    wchar_t appData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData);
    std::wstring dir = std::wstring(appData) + L"\\FLOW-ON";
    fs::create_directories(fs::path(dir));   // no-op if already exists
    return dir + L"\\settings.json";
}

bool ConfigManager::load()
{
    std::wstring path = settingsPath();
    std::ifstream f(path.c_str());   // MSVC ofstream/ifstream accept wchar_t* on Windows
    if (!f.is_open()) {
        // First run — write defaults and return success
        return save();
    }

    try {
        json j;
        f >> j;

        if (j.contains("hotkey"))             m_settings.hotkey           = j["hotkey"];
        if (j.contains("mode"))               m_settings.modeStr          = j["mode"];
        if (j.contains("model"))              m_settings.model            = j["model"];
        if (j.contains("use_gpu"))            m_settings.useGPU           = j["use_gpu"];
        if (j.contains("start_with_windows")) m_settings.startWithWindows = j["start_with_windows"];

        if (j.contains("snippets") && j["snippets"].is_object()) {
            m_settings.snippets.clear();
            for (auto& [k, v] : j["snippets"].items()) {
                // Security: enforce max 500 chars per snippet value
                std::string val = v.get<std::string>();
                if (val.size() > 500) val.resize(500);
                m_settings.snippets[k] = val;
            }
        }
    } catch (...) {
        // Corrupted JSON — reset to defaults
        m_settings = AppSettings{};
        return save();
    }
    return true;
}

bool ConfigManager::save() const
{
    std::wstring wpath = settingsPath();
    std::ofstream f(wpath.c_str());   // Use c_str() to avoid Most Vexing Parse
    if (!f.is_open()) return false;

    json j;
    j["hotkey"]             = m_settings.hotkey;
    j["mode"]               = m_settings.modeStr;
    j["model"]              = m_settings.model;
    j["use_gpu"]            = m_settings.useGPU;
    j["start_with_windows"] = m_settings.startWithWindows;

    json snips;
    for (auto& [k, v] : m_settings.snippets)
        snips[k] = v;
    j["snippets"] = snips;

    f << j.dump(2);
    return f.good();
}

void ConfigManager::applyAutostart(const std::wstring& exePath) const
{
    HKEY hKey = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  0, KEY_SET_VALUE, &hKey);
    if (hKey) {
        std::wstring val = L"\"" + exePath + L"\"";
        RegSetValueExW(hKey, L"FLOW-ON", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(val.c_str()),
                       static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

void ConfigManager::removeAutostart() const
{
    HKEY hKey = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  0, KEY_SET_VALUE, &hKey);
    if (hKey) {
        RegDeleteValueW(hKey, L"FLOW-ON");
        RegCloseKey(hKey);
    }
}
