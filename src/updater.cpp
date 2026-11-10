// updater.cpp — самообновление Aegis из GitHub Releases.
//
// Схема:
//   1) GET  https://api.github.com/repos/narkomaniya/Aegis/releases/latest
//      -> парсим "tag_name" ("v0.3"), сравниваем с локальной kVersion
//   2) если на GitHub новее — качаем бинарник платформы
//      (https://github.com/narkomaniya/Aegis/releases/download/<tag>/<asset>)
//   3) замена под ногами: свой бинарь переименовываем в *.old (rename
//      у работающего процесса разрешён на всех трёх ОС), *.new -> на место,
//      несостоявшийся ход откатываем.
//   4) мусор *.old чистим при следующем старте (см. main.cpp).
#include "updater.h"

#include "logger.h"
#include "version.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;
namespace ae {

// --- версия берётся из version.h (kVersion), чтобы баннер и updater не расходились ---

static const wchar_t* kApiUrl =
    L"https://api.github.com/repos/narkomaniya/Aegis/releases/latest";

static std::wstring platformAsset() {
#ifdef _WIN32
    return L"aegis.exe";
#elif defined(__APPLE__)
    return L"aegis-macos";
#else
    return L"aegis-linux";
#endif
}

// --- HTTP GET на файл ---
static bool httpGetToFile(const std::wstring& url, const std::wstring& dest) {
#ifdef _WIN32
    // urlmon через GetProcAddress: не тянем лишнюю линку
    typedef long HRESULT__;
    typedef HRESULT__ (WINAPI *URLDownloadToFileW_t)(void*, const wchar_t*,
                                                     const wchar_t*, DWORD, void*);
    HMODULE um = LoadLibraryW(L"urlmon.dll");
    if (!um) return false;
    auto dl = (URLDownloadToFileW_t)GetProcAddress(um, "URLDownloadToFileW");
    if (!dl) return false;
    return dl(nullptr, url.c_str(), dest.c_str(), 0, nullptr) == 0; // S_OK==0
#else
    // Linux/macOS: у всех есть curl (на macOS — в системе по умолчанию)
    std::string u(url.begin(), url.end());
    std::string d(dest.begin(), dest.end());
    std::string cmd = "curl -fsSL --connect-timeout 15 -o \"" + d + "\" \"" + u + "\" 2>/dev/null";
    std::system(cmd.c_str());
    std::error_code ec;
    if (fs::exists(fs::path(dest), ec) && fs::file_size(fs::path(dest), ec) > 0)
        return true;
    // запасной wget
    cmd = "wget -q --timeout=15 -O \"" + d + "\" \"" + u + "\" 2>/dev/null";
    std::system(cmd.c_str());
    return fs::exists(fs::path(dest), ec) && fs::file_size(fs::path(dest), ec) > 0;
#endif
}

// Путь к собственному бинарю
std::wstring selfPath() {
#ifdef _WIN32
    wchar_t buf[2048] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, 2048);
    return (n > 0 && n < 2048) ? std::wstring(buf) : L"";
#elif defined(__linux__)
    char buf[2048];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return std::wstring(buf, buf + n); }
    return L"";
#elif defined(__APPLE__)
    char buf[2048];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        size_t n = 0;
        while (n < size && buf[n]) ++n;
        return std::wstring(buf, buf + n);
    }
    return L"";
#else
    return L"";
#endif
}

static std::wstring wideOfUtf8(const std::string& s) {
    return std::wstring(s.begin(), s.end());  // теги ASCII, побайтово безопасно
}

// Вытаскиваем "tag_name" из JSON релиза
static std::wstring parseTag(const std::string& json) {
    const char key[] = "\"tag_name\":";
    std::string::size_type k = json.find(key);
    if (k == std::string::npos) return L"";
    std::string::size_type colon = json.find(':', k);
    if (colon == std::string::npos) return L"";
    std::string::size_type q1 = json.find('"', colon);
    if (q1 == std::string::npos) return L"";
    std::string::size_type q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return L"";
    return wideOfUtf8(json.substr(q1 + 1, q2 - q1 - 1));
}

// "v0.10.1" -> {0,10,1} — числовое сравнение (не строковое!)
static std::vector<long> parseVersion(const std::wstring& tag) {
    std::vector<long> out;
    std::wstring s = tag;
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s.erase(0, 1);
    long cur = 0; bool any = false;
    for (wchar_t c : s) {
        if (c >= L'0' && c <= L'9') { cur = cur * 10 + (long)(c - L'0'); any = true; }
        else if (c == L'.' && any)  { out.push_back(cur); cur = 0; any = false; }
        else if (any == false)       break;   // мусорный хвост
        else                          break;
    }
    if (any) out.push_back(cur);
    return out;
}

static int cmpVersion(const std::vector<long>& a, const std::vector<long>& b) {
    size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        long x = i < a.size() ? a[i] : 0;
        long y = i < b.size() ? b[i] : 0;
        if (x != y) return (x > y) ? 1 : -1;
    }
    return 0;
}

static std::wstring tempDir() {
    const char* t = std::getenv("TEMP");
#ifdef _WIN32
    if (!t) t = std::getenv("TMP");
#endif
    if (!t) t = std::getenv("TMPDIR");
    if (!t) t = std::getenv("HOME");
    if (!t) t = "/tmp";
    std::wstring d;
#ifdef _WIN32
    // ANSI/CP1251 домашние пути -> широкие корректно (может быть кириллица)
    int n = MultiByteToWideChar(CP_ACP, 0, t, -1, nullptr, 0);
    d.resize((size_t)(n > 0 ? n - 1 : 0));
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, t, -1, d.data(), n);
#else
    d = wideOfUtf8(t);
#endif
    return d;
}

void cleanupOldBinary() {
    std::wstring self = selfPath();
    if (self.empty()) return;
    std::error_code ec;
    fs::path old(self + L".old");
    if (fs::exists(old, ec)) { ec.clear(); fs::remove(old, ec); }
}

int runUpdate(bool assumeYes) {
    console(L"checking the latest GitHub release...");

    std::wstring tmp = tempDir();
#ifdef _WIN32
    std::wstring jsonPath = tmp + L"\\aegis-release.json";
#else
    std::wstring jsonPath = tmp + L"/aegis-release.json";
#endif
    std::error_code ec;
    if (!httpGetToFile(kApiUrl, jsonPath)) {
        warn(L"could not reach GitHub (offline? api rate-limited?)");
        std::system("true");
        return 2;
    }

    std::wstring tag;
    {
        std::ifstream f(fs::path(jsonPath), std::ios::binary);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            tag = parseTag(ss.str());
        }
    }
    if (tag.empty()) {
        warn(L"could not read tag_name from the GitHub answer");
        return 2;
    }

    std::vector<long> remote = parseVersion(tag);
    std::vector<long> local  = parseVersion(kVersion);

    if (cmpVersion(remote, local) <= 0) {
        ok(std::wstring(L"Aegis ") + kVersion + L" is up to date" +
           (cmpVersion(remote, local) < 0 ? L" (installed build is newer than the last release)" : L""));
        return 0;
    }

    console(L"new version available: " + tag + L"   (you have " +
            kVersion + L")");
    if (!assumeYes) {
        console(L"update now? [y/N]");
        std::wstring ans;
        if (!std::getline(std::wcin, ans)) return 2;
        for (auto& c : ans) c = towlower(c);
        if (ans != L"y" && ans != L"yes") {
            console(L"skipping update");
            return 0;
        }
    }

    const std::wstring url =
        std::wstring(L"https://github.com/narkomaniya/Aegis/releases/download/") +
        tag + L"/" + platformAsset();

    std::wstring self = selfPath();
    if (self.empty()) { warn(L"cannot determine own path"); return 2; }

    const std::wstring newPath = self + L".new";
    const std::wstring oldPath = self + L".old";

    info(L"downloading " + platformAsset() + L" (" + tag + L")...");
    fs::remove(fs::path(newPath), ec);
    if (!httpGetToFile(url, newPath)) {
        warn(L"download failed");
        return 2;
    }

    // замена: старый -> .old, новый -> на место. rename у работающего
    // процесса разрешён на Win/Linux/macOS; на ошибках откатываем.
    ec.clear();
    fs::rename(fs::path(self), fs::path(oldPath), ec);
    if (ec) { warn(L"cannot swap: " + wideOfUtf8(ec.message())); return 2; }
    ec.clear();
    fs::rename(fs::path(newPath), fs::path(self), ec);
    if (ec) {
        // откат: вернуть старый на место
        std::error_code ec2;
        fs::rename(fs::path(oldPath), fs::path(self), ec2);
        warn(L"cannot install new file: " + wideOfUtf8(ec.message()));
        return 2;
    }

    // на Windows работающий .exe можно убрать только после смерти процесса —
    // пробуем сейчас, если не выйдет, подберём при следующем запуске
#ifdef _WIN32
    DeleteFileW(oldPath.c_str());
#endif

    ok(L"updated to " + tag + L". Restart Aegis to load the new build.");
    return 1;
}

} // namespace ae
