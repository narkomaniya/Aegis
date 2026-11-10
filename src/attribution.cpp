// attribution.cpp — "кто трогал файл": в момент срабатывания Engine узнаём,
// какой процесс держит файл открытым.
//
//   Windows : Restart Manager API (rstrtmgr.dll) — официальный интерфейс,
//             которым сама Windows отвечает "файл занят процессом X".
//             Безопасно: DuplicateHandle-обходы таблиц хэндлов ВИСНУТ на
//             console/ALPC-хэндлах (проверено вживую), поэтому не используем
//             их вовсе.
//   Linux   : /proc/<pid>/fd/* симлинки (надёжно, без прав root)
//   macOS   : SIP прячет чужие дескрипторы — вернётся пусто (это ок)
#include "attribution.h"

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

#ifdef __linux__
#include <dirent.h>
#include <fstream>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ae {

#ifdef _WIN32

// Windows: спрашиваем Restart Manager, кто открыл файл.
// Дампом выверено: RM пишет записи stride = 656 байт,
//   +0  : DWORD pid,
//   +4  : FILETIME (.start, упаковано, без паддинга),
//   +12 : wchar_t app[] — имя приложения (NUL-terminated).
static std::wstring fileOwnerWin(const std::wstring& full) {
    HMODULE rm = LoadLibraryW(L"rstrtmgr.dll");
    if (!rm) return L"";

    typedef DWORD (WINAPI *RmStartSession_t)(DWORD*, DWORD, wchar_t*);
    typedef DWORD (WINAPI *RmRegisterResources_t)(DWORD, UINT, const wchar_t**,
                                                  UINT, void*, UINT, const wchar_t**);
    typedef DWORD (WINAPI *RmGetList_t)(DWORD, UINT*, UINT*, void*, DWORD*);
    typedef DWORD (WINAPI *RmEndSession_t)(DWORD);

    auto start       = (RmStartSession_t)GetProcAddress(rm, "RmStartSession");
    auto registerRes = (RmRegisterResources_t)GetProcAddress(rm, "RmRegisterResources");
    auto getList     = (RmGetList_t)GetProcAddress(rm, "RmGetList");
    auto endSess     = (RmEndSession_t)GetProcAddress(rm, "RmEndSession");
    if (!start || !registerRes || !getList || !endSess) return L"";

    struct Guard {
        DWORD s;
        RmEndSession_t e;
        ~Guard() { if (e) e(s); }
    } guard{0, endSess};

    wchar_t key[64] = {};
    if (start(&guard.s, 0, key) != 0) return L"";

    // Регистрируем ресурс: путь к жертве
    std::vector<wchar_t> pathBuf(full.size() + 1, L'\0');
    std::copy(full.begin(), full.end(), pathBuf.begin());
    const wchar_t* files[1] = { pathBuf.data() };
    if (registerRes(guard.s, 1, files, 0, nullptr, 0, nullptr) != 0) return L"";

    // Первый вызов отдаёт ERROR_MORE_DATA (234) с нужным количеством записей
    UINT needed = 0, count = 0;
    DWORD reason = 0;
    if (getList(guard.s, &needed, &count, nullptr, &reason) != 234 || needed == 0)
        return L"";

    constexpr size_t kStride = 720;   // выверено дампом: запись занимает >652,
                                      // берём с запасом — только первый владелец читается
    std::vector<unsigned char> buf((size_t)needed * kStride + 64, 0);
    count = needed;
    if (getList(guard.s, &needed, &count, buf.data(), &reason) != 0 || count == 0)
        return L"";

    // Первый же владелец нас интересует
    const unsigned char* rec = buf.data();
    const DWORD pid = *reinterpret_cast<const DWORD*>(rec);

    wchar_t app[257] = {};            // +1 гарантированный NUL
    std::memcpy(app, rec + 12, 255 * sizeof(wchar_t));
    app[255] = L'\0';
    std::wstring name(app);
    if (name.empty()) name = L"unknown process";

    return L"pid " + std::to_wstring(pid) + L" (" + name + L")";
}

#elif defined(__linux__)

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    return std::wstring(s.begin(), s.end());
}

static std::wstring procName(int pid, const std::string& base) {
    std::string comm;
    {
        std::ifstream f(base + "/comm");
        if (f) std::getline(f, comm);
    }
    if (comm.empty()) {  // comm может быть выключен — берём cmdline argv[0]
        std::ifstream f(base + "/cmdline");
        if (f) std::getline(f, comm, '\0');
    }
    size_t slash = comm.find_last_of('/');
    if (slash != std::string::npos) comm = comm.substr(slash + 1);
    return utf8ToWide(comm);
}

// Linux: /proc/<pid>/fd/* — читаем симлинки и сравниваем с целевым файлом
static std::wstring fileOwnerProc(const std::wstring& full) {
    std::error_code ec;
    std::wstring tgt = fs::canonical(fs::path(full), ec).wstring();
    if (ec || tgt.empty()) tgt = full;

    DIR* dp = opendir("/proc");
    if (!dp) return L"";

    std::wstring found;
    while (found.empty()) {
        dirent* de = readdir(dp);
        if (!de) break;
        const char* nm = de->d_name;
        bool digitsOnly = (*nm != '\0');
        for (const char* c = nm; digitsOnly && *c; ++c)
            if (*c < '0' || *c > '9') digitsOnly = false;
        if (!digitsOnly) continue;

        int pid = std::atoi(nm);
        if (pid <= 0) continue;

        std::string base = std::string("/proc/") + nm;
        std::string fdd = base + "/fd";
        DIR* df = opendir(fdd.c_str());
        if (!df) continue;
        while (dirent* fe = readdir(df)) {
            std::string lnk = fdd + "/" + fe->d_name;
            char buf[4096];
            ssize_t n = ::readlink(lnk.c_str(), buf, sizeof(buf) - 1);
            if (n <= 0) continue;
            if (utf8ToWide(std::string(buf, (size_t)n)) == tgt) {
                found = L"pid " + std::to_wstring(pid) + L" (" + procName(pid, base) + L")";
                break;
            }
        }
        closedir(df);
    }
    closedir(dp);
    return found;
}

#elif defined(__APPLE__)
// macOS: SIP прячет чужие файл-дескрипторы — честно возвращаем пусто.
static std::wstring fileOwnerMac(const std::wstring&) { return L""; }

#endif

std::wstring fileOwner(const std::wstring& full) {
#ifdef _WIN32
    return fileOwnerWin(full);
#elif defined(__linux__)
    return fileOwnerProc(full);
#elif defined(__APPLE__)
    return fileOwnerMac(full);
#else
    (void)full;
    return L"";
#endif
}

} // namespace ae
