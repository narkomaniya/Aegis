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

static int parseSens(const std::string& v) {
    if (v == "low" || v == "0") return 0;
    if (v == "high" || v == "2") return 2;
    if (v == "medium" || v == "1") return 1;
    return -1;
}

Settings loadSettings() {
    Settings s;
    std::ifstream f(fs::path(configPath()), std::ios::binary);
    if (!f) return s;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("sensitivity=", 0) == 0) {
            int v = parseSens(line.substr(12));
            if (v >= 0) s.sensitivity = v;
        } else if (line.rfind("ultralight=", 0) == 0) {
            std::string v = line.substr(11);
            s.ultraLight = (v == "1" || v == "true" || v == "on");
        } else if (line.rfind("autoupdate=", 0) == 0) {
            std::string v = line.substr(11);
            s.autoUpdateCheck = !(v == "0" || v == "false" || v == "off");
        } else if (line.rfind("exclude_path=", 0) == 0) {
            std::wstring w = utf8ToW(line.substr(13));
            if (!w.empty()) s.excludePaths.push_back(w);
        } else if (line.rfind("exclude_ext=", 0) == 0) {
            std::wstring w = utf8ToW(line.substr(12));
            if (!w.empty()) s.excludeExts.push_back(w);
        } else if (line.rfind("last_update_available=", 0) == 0) {
            std::string v = line.substr(22);
            s.lastUpdateAvailable = (v == "1" || v == "true");
        } else if (line.rfind("last_update_version=", 0) == 0) {
            s.lastUpdateVersion = utf8ToW(line.substr(20));
        } else if (line.rfind("watch=", 0) == 0) {
            // new format: watch=<sens or ->|<path>  old: watch=<path>
            std::string rest = line.substr(6);
            size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                std::string sv = rest.substr(0, bar);
                std::wstring path = utf8ToW(rest.substr(bar + 1));
                if (!path.empty()) {
                    WatchFolder wf; wf.path = path; wf.sensitivity = parseSens(sv);
                    s.watchFolders.push_back(wf);
                }
            } else {
                std::wstring w = utf8ToW(rest);
                if (!w.empty()) { WatchFolder wf; wf.path = w; wf.sensitivity = -1; s.watchFolders.push_back(wf); }
            }
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
    f << "ultralight=" << (s.ultraLight ? "1" : "0") << "\n";
    f << "autoupdate=" << (s.autoUpdateCheck ? "1" : "0") << "\n";
    f << "last_update_available=" << (s.lastUpdateAvailable ? "1" : "0") << "\n";
    if (!s.lastUpdateVersion.empty())
        f << "last_update_version=" << wToUtf8(s.lastUpdateVersion) << "\n";
    for (const auto& wf : s.watchFolders) {
        std::string sv = (wf.sensitivity < 0) ? "-" : wToUtf8(sensitivityName(wf.sensitivity));
        f << "watch=" << sv << "|" << wToUtf8(wf.path) << "\n";
    }
    for (const auto& p : s.excludePaths) f << "exclude_path=" << wToUtf8(p) << "\n";
    for (const auto& e : s.excludeExts) f << "exclude_ext=" << wToUtf8(e) << "\n";
    return true;
}

} // namespace ae
