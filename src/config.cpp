// config.cpp
#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

namespace fs = std::filesystem;

namespace ae {

#ifdef _WIN32
static std::wstring knownFolderLocal(REFKNOWNFOLDERID id) {
    PWSTR out = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &out))) {
        std::wstring s(out);
        CoTaskMemFree(out);
        return s;
    }
    return L"";
}
#endif

std::wstring configPath() {
#ifdef _WIN32
    std::wstring home = knownFolderLocal(FOLDERID_Profile);
    if (home.empty()) {
        if (const wchar_t* e = _wgetenv(L"USERPROFILE")) home = e;
    }
    if (home.empty()) return L".aegis\\config.txt";
    return (fs::path(home) / L".aegis" / L"config.txt").wstring();
#else
    const char* h = std::getenv("HOME");
    fs::path base = h ? fs::path(h) : fs::path(".");
    return (base / ".aegis" / "config.txt").wstring();
#endif
}

std::wstring sensitivityName(int s) {
    if (s <= 0) return L"low";
    if (s >= 2) return L"high";
    return L"medium";
}

static std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
#ifdef _WIN32
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
#else
    return std::string(w.begin(), w.end());
#endif
}

static std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return {};
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

Settings loadSettings() {
    Settings s;
    std::ifstream f(fs::path(configPath()), std::ios::binary);
    if (!f) return s;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("sensitivity=", 0) == 0) {
            std::string v = line.substr(12);
            if (v == "low") s.sensitivity = 0;
            else if (v == "high") s.sensitivity = 2;
            else s.sensitivity = 1;
        } else if (line.rfind("watch=", 0) == 0) {
            std::wstring w = utf8ToW(line.substr(6));
            if (!w.empty()) s.watchDirs.push_back(w);
        }
    }
    return s;
}

bool saveSettings(const Settings& s) {
    std::error_code ec;
    fs::create_directories(fs::path(configPath()).parent_path(), ec);
    std::ofstream f(fs::path(configPath()), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "sensitivity=" << wToUtf8(sensitivityName(s.sensitivity)) << "\n";
    for (const auto& d : s.watchDirs) {
        f << "watch=" << wToUtf8(d) << "\n";
    }
    return true;
}

} // namespace ae
