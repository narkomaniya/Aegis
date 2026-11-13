#pragma once
#include <string>
#include <vector>

namespace ae {

struct Settings {
    std::vector<std::wstring> watchDirs; // empty = defaults
    int sensitivity = 1; // 0=low, 1=medium, 2=high
};

std::wstring configPath();
Settings loadSettings();
bool saveSettings(const Settings& s);
std::wstring sensitivityName(int s);

} // namespace ae
