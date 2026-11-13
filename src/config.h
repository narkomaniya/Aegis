#pragma once
#include <string>
#include <vector>
#include <utility>

namespace ae {

struct WatchFolder {
    std::wstring path;
    int sensitivity = -1; // -1 = use global
};

struct Settings {
    std::vector<WatchFolder> watchFolders; // empty = defaults
    int sensitivity = 1; // 0=low, 1=medium, 2=high (global)
    bool ultraLight = false; // reduce checks/frequency for very low resource usage
    bool autoUpdateCheck = true; // show update notice on interactive start
    bool backupBeforeQuarantine = true; // keep backup copy before moving to quarantine
    std::wstring language = L"en"; // ui language: en, ru (reserved/partial)
    std::wstring uiTheme = L"default"; // default, white, black (tweaks/mods)
    std::vector<std::wstring> excludePaths; // exact/prefix paths to ignore
    std::vector<std::wstring> excludeExts;  // extensions like .log
    std::vector<std::wstring> disabledMods; // mod ids disabled by user
    bool notifyOnAlerts = true; // best-effort desktop/log notifications (v5)
    bool lastUpdateAvailable = false;
    std::wstring lastUpdateVersion;
};

std::wstring configPath();
Settings loadSettings();
bool saveSettings(const Settings& s);
std::wstring sensitivityName(int s);

} // namespace ae
