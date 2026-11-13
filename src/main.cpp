// main.cpp — точка входа Aegis (CLI)
#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <clocale>
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <ctime>
#include <utility>

#include "logger.h"
#include "watcher.h"
#include "engine.h"
#include "quarantine.h"
#include "version.h"
#include "updater.h"
#include "autostart.h"
#include "config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#else
#include <locale>
#include <codecvt>
#include <fstream>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace ae;

#ifdef _WIN32
static std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
#else
static std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
}
#endif

static void setupConsole() {
    std::setlocale(LC_ALL, "");
#ifdef _WIN32
    if (_isatty(_fileno(stdout))) {
        _setmode(_fileno(stdout), _O_U16TEXT);
        _setmode(_fileno(stderr), _O_U16TEXT);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

static void banner() {
#ifdef _WIN32
    const wchar_t* platform = L"windows";
#elif defined(__APPLE__)
    const wchar_t* platform = L"macOS";
#else
    const wchar_t* platform = L"linux";
#endif
    console(L"");
    console(L"        ________________________________");
    console(L"        ||                            ||");
    console(L"        ||                            ||");
    console(L"        ||     A   E   G   I   S      ||");
    console(L"        ||============================||");
    console(L"        ||s h i e l d   o f   Z e u s ||");
    console(L"        ||                            ||");
    console(L"        ||____________________________||");
    console(L"          \\____________________________/");
    console(L"            \\________________________/  ");
    console(L"              \\____________________/    ");
    console(L"                \\________________/      ");
    console(L"                  \\____________/        ");
    console(L"                    \\________/          ");
    console(L"                      \\____/            ");
    console(L"                        \\/              ");
    console(L"");
    console(std::wstring(L"    --- file guardian | ") + ae::kVersion +
            L" (" + platform + L") | c++17 ---");
    console(L"                       by narkomaniya");
    console(L"");
}

static void clearScreen() {
    std::wcout << L"\033[2J\033[H";
    std::wcout.flush();
}

static void waitEnter(const std::wstring& msg = L"Press Enter to return...") {
    console(L"");
    console(msg);
    std::wstring dummy;
    std::getline(std::wcin, dummy);
}

static std::wstring trimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

static std::wstring homeDir() {
#ifdef _WIN32
    const char* up = std::getenv("USERPROFILE");
    return up ? toW(up) : L"";
#else
    const char* h = std::getenv("HOME");
    return h ? toW(h) : L"";
#endif
}

static std::wstring aegisDir() {
    std::wstring home = homeDir();
    if (home.empty()) home = L".";
#ifdef _WIN32
    return home + L"\\.aegis";
#else
    return home + L"/.aegis";
#endif
}

static std::wstring quarantineRoot() {
    return (fs::path(aegisDir()) / L"quarantine").wstring();
}

static std::wstring logPath() {
    return (fs::path(aegisDir()) / L"aegis.log").wstring();
}

static void pushDefaultDir(std::vector<std::wstring>& dirs, int which) {
    std::wstring path;
#ifdef _WIN32
    static const KNOWNFOLDERID kIds[3] = { FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads };
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(kIds[which], 0, nullptr, &raw) == S_OK) {
        path = raw;
        CoTaskMemFree(raw);
    }
#else
    static const char* kXdgKeys[3] = { "XDG_DESKTOP_DIR", "XDG_DOCUMENTS_DIR", "XDG_DOWNLOADS_DIR" };
    const wchar_t* kSubs[3] = { L"Desktop", L"Documents", L"Downloads" };
    std::wstring home = homeDir();
    if (home.empty()) return;
    path = home + L"/" + kSubs[which];
    const char* h = std::getenv("HOME");
    if (h) {
        std::ifstream cfg(std::string(h) + "/.config/user-dirs.dirs");
        if (cfg) {
            std::string line;
            while (std::getline(cfg, line)) {
                size_t hash = line.find('#');
                if (hash != std::string::npos) line = line.substr(0, hash);
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                if (line.compare(0, eq, kXdgKeys[which]) != 0) continue;
                std::string val = line.substr(eq + 1);
                val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
                if (val.compare(0, 5, "$HOME") == 0) val = std::string(h) + val.substr(5);
                if (!val.empty()) { path = toW(val); break; }
            }
        }
    }
#endif
    if (!path.empty()) dirs.push_back(path);
}

static std::vector<WatchFolder> defaultWatchFolders() {
    std::vector<std::wstring> d;
    pushDefaultDir(d, 0); pushDefaultDir(d, 1); pushDefaultDir(d, 2);
    std::vector<WatchFolder> out;
    for (auto& p : d) { WatchFolder wf; wf.path = p; wf.sensitivity = -1; out.push_back(wf); }
    return out;
}

static std::vector<WatchFolder> effectiveWatchFolders(const Settings& s) {
    if (!s.watchFolders.empty()) return s.watchFolders;
    return defaultWatchFolders();
}

static std::vector<std::wstring> effectiveWatchPaths(const Settings& s) {
    auto wf = effectiveWatchFolders(s);
    std::vector<std::wstring> out;
    for (auto& w : wf) out.push_back(w.path);
    return out;
}

static void showHeader() {
    clearScreen();
    banner();
}

static void printHelp() {
    console(L"Commands:");
    console(L"  aegis watch [dir1 dir2 ...]      watch folders (default or saved/custom)");
    console(L"  aegis quarantine                 show quarantine contents");
    console(L"  aegis restore                    list quarantine records with numbers");
    console(L"  aegis restore <number>           restore a quarantined file");
    console(L"  aegis update [-y]                check GitHub and update Aegis itself");
    console(L"  aegis autostart on|off           start Aegis automatically with the system");
    console(L"  aegis help                       this help");
    console(L"");
    console(L"Commands while watching (CLI mode):");
    console(L"  status  quarantine  restore [n]  help  q");
}

static bool openPath(const std::wstring& path) {
    std::error_code ec;
    if (!fs::exists(fs::path(path), ec)) return false;
#ifdef _WIN32
    HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
#elif defined(__APPLE__)
    std::string cmd = "open \"" + std::string(path.begin(), path.end()) + "\"";
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open \"" + std::string(path.begin(), path.end()) + "\"";
    return std::system(cmd.c_str()) == 0;
#endif
}

static void printRecordList(const Quarantine& q) {
    auto recs = q.records();
    if (recs.empty()) { console(L"  quarantine is empty"); return; }
    int i = 1;
    for (const auto& r : recs) {
        console(L"  [" + std::to_wstring(i++) + L"] " + r.file);
        console(L"      from:   " + r.from);
        console(L"      reason: " + r.reason);
        if (!r.time.empty()) console(L"      time:   " + r.time);
    }
}

static int doRestoreByNumber(Quarantine& q, Engine* engine, const std::wstring& numStr) {
    std::wstring s = trimW(numStr);
    size_t num = 0;
    if (s.empty()) { console(L"no number given"); return 1; }
    for (wchar_t c : s) { if (c < L'0' || c > L'9') { console(L"number must be digits: " + s); return 1; } num = num * 10 + (size_t)(c - L'0'); }
    auto recs = q.records();
    if (num == 0 || num > recs.size()) { console(L"no such record: " + std::to_wstring(num)); return 1; }
    Quarantine::Record rec = recs[num - 1];
    if (engine) engine->ignorePath(rec.from);
    if (q.restore(rec)) { ok(L"restored: " + rec.from); return 0; }
    warn(L"could not restore: " + rec.file);
    return 1;
}

static void quarantineMenu(Quarantine& q, Engine* engine) {
    while (true) {
        showHeader();
        console(L"  [ QUARANTINE ]");
        console(L"");
        auto recs = q.records();
        if (recs.empty()) console(L"  quarantine is empty");
        else {
            int i = 1;
            for (const auto& r : recs) {
                console(L"  [" + std::to_wstring(i++) + L"] " + r.file);
                console(L"      from:   " + r.from);
                console(L"      reason: " + r.reason);
            }
        }
        console(L"");
        console(L"  Enter number to restore, d<number> to delete, a restore-all, c clear-all");
        console(L"  o open quarantine folder, 0 back");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"o" || line == L"O") { openPath(quarantineRoot()); continue; }
        if (line == L"a" || line == L"A") {
            auto all = q.records();
            int restored = 0;
            for (auto& r : all) { if (engine) engine->ignorePath(r.from); if (q.restore(r)) restored++; }
            ok(L"Restored " + std::to_wstring(restored) + L" file(s).");
            waitEnter(); continue;
        }
        if (line == L"c" || line == L"C") {
            std::error_code ec; fs::remove_all(fs::path(quarantineRoot()), ec);
            fs::create_directories(fs::path(quarantineRoot()), ec);
            ok(L"Quarantine cleared.");
            waitEnter(); continue;
        }
        if (!line.empty() && (line[0] == L'd' || line[0] == L'D')) {
            std::wstring n = trimW(line.substr(1));
            size_t num = 0; bool okn = !n.empty();
            for (wchar_t c : n) if (c < L'0' || c > L'9') { okn = false; break; }
            if (okn) for (wchar_t c : n) num = num * 10 + (size_t)(c - L'0');
            auto all = q.records();
            if (okn && num >= 1 && num <= all.size()) {
                std::error_code ec; fs::remove_all(fs::path(all[num-1].folder), ec);
                ok(ec ? L"Could not delete record." : L"Record deleted.");
            } else warn(L"No such record.");
            waitEnter(); continue;
        }
        bool allDigits = !line.empty();
        for (wchar_t c : line) if (c < L'0' || c > L'9') { allDigits = false; break; }
        if (allDigits) {
            showHeader(); console(L"  [ QUARANTINE ]"); console(L"");
            doRestoreByNumber(q, engine, line);
            waitEnter();
        }
    }
}

static void historyMenu() {
    while (true) {
        showHeader();
        console(L"  [ HISTORY / STATS ]");
        console(L"");
        Quarantine q(quarantineRoot());
        auto recs = q.records();
        console(L"  Quarantine items: " + std::to_wstring(recs.size()));
        console(L"  Quarantine path:  " + quarantineRoot());
        console(L"  Log file:         " + logPath());
        console(L"");
        console(L"  1. Show all records");
        console(L"  2. Search/filter records");
        console(L"  3. Open quarantine folder");
        console(L"  4. Open log file location");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"3") { openPath(quarantineRoot()); continue; }
        if (line == L"4") { openPath(fs::path(logPath()).parent_path().wstring()); continue; }
        if (line == L"1" || line == L"2") {
            std::wstring qstr;
            if (line == L"2") {
                showHeader(); console(L"  [ SEARCH ]"); console(L"");
                console(L"  Enter text to search (name/reason/path/time), or empty for all:");
                prompt(L"  > ");
                if (!std::getline(std::wcin, qstr)) return;
                qstr = lowerW(trimW(qstr));
            }
            showHeader(); console(L"  [ HISTORY / STATS ]"); console(L"");
            int shown = 0, idx = 1;
            for (const auto& r : recs) {
                std::wstring blob = lowerW(r.file + L" " + r.from + L" " + r.reason + L" " + r.time);
                if (!qstr.empty() && blob.find(qstr) == std::wstring::npos) { idx++; continue; }
                console(L"  [" + std::to_wstring(idx++) + L"] " + r.file);
                if (!r.time.empty()) console(L"      time:   " + r.time);
                console(L"      reason: " + r.reason);
                console(L"      from:   " + r.from);
                shown++;
            }
            if (shown == 0) console(L"  No matching records.");
            waitEnter();
        }
    }
}

static void exportReport() {
    showHeader();
    console(L"  [ EXPORT REPORT ]");
    console(L"");
    Quarantine q(quarantineRoot());
    auto recs = q.records();
    fs::path out = fs::path(aegisDir()) / L"aegis-report.txt";
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) { warn(L"Could not write report."); waitEnter(); return; }
    auto to8 = [](const std::wstring& w){
        std::string s;
#ifdef _WIN32
        if (w.empty()) return s;
        int n = WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
        s.resize(n); if(n>0) WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);
#else
        s.assign(w.begin(), w.end());
#endif
        return s;
    };
    f << "Aegis report\n";
    f << "Version: " << to8(ae::kVersion) << "\n";
    f << "Quarantine items: " << recs.size() << "\n\n";
    int i=1;
    for (const auto& r : recs) {
        f << "["<< (i++) << "] " << to8(r.file) << "\n";
        f << "  time:   " << to8(r.time) << "\n";
        f << "  reason: " << to8(r.reason) << "\n";
        f << "  from:   " << to8(r.from) << "\n\n";
    }
    f.close();
    ok(L"Report saved: " + out.wstring());
    waitEnter();
}

static void diagnosticsMenu(const Settings& settings) {
    showHeader();
    console(L"  [ DIAGNOSTICS / CHECK SETUP ]");
    console(L"");
    auto dirs = effectiveWatchPaths(settings);
    int okCount = 0, warnCount = 0;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (fs::is_directory(fs::path(d), ec)) { console(L"  [OK]   Folder exists: " + d); okCount++; }
        else { console(L"  [WARN] Folder missing: " + d); warnCount++; }
    }
    std::error_code ec;
    fs::create_directories(fs::path(quarantineRoot()), ec);
    if (!ec) { console(L"  [OK]   Quarantine writable: " + quarantineRoot()); okCount++; }
    else { console(L"  [WARN] Quarantine issue: " + quarantineRoot()); warnCount++; }
    fs::path lp = fs::path(logPath()).parent_path();
    fs::create_directories(lp, ec);
    if (fs::exists(lp)) { console(L"  [OK]   Config/log dir: " + lp.wstring()); okCount++; }
    else { console(L"  [WARN] Config/log dir issue: " + lp.wstring()); warnCount++; }
    for (const auto& d : dirs) {
        fs::path c = fs::path(d) / L".aegis-canary.txt";
        if (fs::exists(c, ec)) { console(L"  [OK]   Canary present: " + c.wstring()); okCount++; }
        else { console(L"  [INFO] Canary not yet planted: " + c.wstring()); }
    }
    console(L"");
    console(L"  Summary: " + std::to_wstring(okCount) + L" OK, " + std::to_wstring(warnCount) + L" warning(s).");
    console(L"  Tip: canaries are planted when protection starts.");
    waitEnter();
}

static void alertsMenu() {
    showHeader();
    console(L"  [ RECENT ALERTS ]");
    console(L"");
    auto alerts = Logger::instance().recentAlerts(30);
    if (alerts.empty()) console(L"  No recent alerts/warnings in this session.");
    else for (const auto& a : alerts) console(L"  " + a);
    console(L"");
    console(L"  1. Clear alert buffer");
    console(L"  0. Back");
    prompt(L"  > ");
    std::wstring line; if (!std::getline(std::wcin, line)) return;
    if (trimW(line) == L"1") { Logger::instance().clearRecentAlerts(); ok(L"Alert buffer cleared."); waitEnter(); }
}

static void autostartMenu() {
    while (true) {
        showHeader();
        console(L"  [ AUTOSTART ]");
        console(L"");
        console(L"  1. Enable autostart");
        console(L"  2. Disable autostart");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1" || line == L"2") {
            showHeader(); console(L"  [ AUTOSTART ]"); console(L"");
            runAutostart(line == L"1" ? L"on" : L"off");
            waitEnter();
        }
    }
}

static void updateMenu() {
    showHeader();
    console(L"  [ UPDATE ]");
    console(L"");
    console(L"  1. Check and update (ask before installing)");
    console(L"  2. Check and update (auto-yes)");
    console(L"  0. Back");
    console(L"");
    prompt(L"  > ");
    std::wstring line; if (!std::getline(std::wcin, line)) return;
    line = trimW(line);
    if (line == L"1" || line == L"2") {
        showHeader(); console(L"  [ UPDATE ]"); console(L"");
        runUpdate(line == L"2");
        waitEnter();
    }
}

static void exclusionsMenu(Settings& settings) {
    while (true) {
        showHeader();
        console(L"  [ EXCLUSIONS / WHITELIST ]");
        console(L"");
        console(L"  Excluded paths (prefix match):");
        for (size_t i=0;i<settings.excludePaths.size();++i) console(L"    P" + std::to_wstring(i+1) + L". " + settings.excludePaths[i]);
        if (settings.excludePaths.empty()) console(L"    (none)");
        console(L"  Excluded extensions:");
        for (size_t i=0;i<settings.excludeExts.size();++i) console(L"    E" + std::to_wstring(i+1) + L". " + settings.excludeExts[i]);
        if (settings.excludeExts.empty()) console(L"    (none)");
        console(L"");
        console(L"  1. Add excluded path");
        console(L"  2. Add excluded extension");
        console(L"  3. Remove excluded path (by number)");
        console(L"  4. Remove excluded extension (by number)");
        console(L"  0. Back");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1") {
            console(L"  Enter path/prefix to exclude:"); prompt(L"  > ");
            std::wstring p; if (!std::getline(std::wcin,p)) return; p=trimW(p);
            if(!p.empty()){ settings.excludePaths.push_back(p); saveSettings(settings); ok(L"Added."); waitEnter(); }
        } else if (line == L"2") {
            console(L"  Enter extension to exclude (e.g. .log):"); prompt(L"  > ");
            std::wstring e; if (!std::getline(std::wcin,e)) return; e=trimW(e);
            if(!e.empty()){ if(e[0]!=L'.') e=L"."+e; settings.excludeExts.push_back(e); saveSettings(settings); ok(L"Added."); waitEnter(); }
        } else if (line == L"3") {
            console(L"  Path number to remove:"); prompt(L"  > ");
            std::wstring n; if(!std::getline(std::wcin,n)) return; size_t num=0; for(wchar_t c:trimW(n)) if(c>=L'0'&&c<=L'9') num=num*10+(c-L'0'); else {num=0;break;}
            if(num>=1 && num<=settings.excludePaths.size()){ settings.excludePaths.erase(settings.excludePaths.begin()+num-1); saveSettings(settings); ok(L"Removed."); }
            waitEnter();
        } else if (line == L"4") {
            console(L"  Extension number to remove:"); prompt(L"  > ");
            std::wstring n; if(!std::getline(std::wcin,n)) return; size_t num=0; for(wchar_t c:trimW(n)) if(c>=L'0'&&c<=L'9') num=num*10+(c-L'0'); else {num=0;break;}
            if(num>=1 && num<=settings.excludeExts.size()){ settings.excludeExts.erase(settings.excludeExts.begin()+num-1); saveSettings(settings); ok(L"Removed."); }
            waitEnter();
        }
    }
}

static void foldersMenu(Settings& settings) {
    while (true) {
        showHeader();
        console(L"  [ WATCHED FOLDERS ]");
        console(L"");
        auto eff = effectiveWatchFolders(settings);
        bool usingDefaults = settings.watchFolders.empty();
        console(usingDefaults ? L"  Mode: default folders (Desktop, Documents, Downloads)" : L"  Mode: custom folder list");
        console(L"");
        for (size_t i = 0; i < eff.size(); ++i) {
            std::wstring sens = (eff[i].sensitivity < 0) ? (L"global/" + sensitivityName(settings.sensitivity)) : sensitivityName(eff[i].sensitivity);
            console(L"  " + std::to_wstring(i + 1) + L". " + eff[i].path + L"  [sens: " + sens + L"]");
        }
        if (eff.empty()) console(L"  (no folders)");
        console(L"");
        console(L"  a. Add folder   r. Remove folder   s. Set folder sensitivity");
        console(L"  d. Reset to defaults   0. Back");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"a" || line == L"A") {
            console(L"  Enter full folder path:"); prompt(L"  > ");
            std::wstring p; if (!std::getline(std::wcin, p)) return; p = trimW(p);
            if (!p.empty()) {
                if (settings.watchFolders.empty()) settings.watchFolders = defaultWatchFolders();
                WatchFolder wf; wf.path = p; wf.sensitivity = -1; settings.watchFolders.push_back(wf);
                saveSettings(settings); ok(L"Folder added."); waitEnter();
            }
        } else if (line == L"r" || line == L"R") {
            console(L"  Folder number to remove:"); prompt(L"  > ");
            std::wstring n; if (!std::getline(std::wcin, n)) return; n=trimW(n);
            size_t num=0; for(wchar_t c:n){ if(c<L'0'||c>L'9'){num=0;break;} num=num*10+(c-L'0'); }
            if (settings.watchFolders.empty()) settings.watchFolders = defaultWatchFolders();
            if (num>=1 && num<=settings.watchFolders.size()) { settings.watchFolders.erase(settings.watchFolders.begin()+num-1); saveSettings(settings); ok(L"Removed."); }
            waitEnter();
        } else if (line == L"s" || line == L"S") {
            console(L"  Folder number:"); prompt(L"  > ");
            std::wstring n; if (!std::getline(std::wcin, n)) return; n=trimW(n);
            size_t num=0; for(wchar_t c:n){ if(c<L'0'||c>L'9'){num=0;break;} num=num*10+(c-L'0'); }
            if (settings.watchFolders.empty()) settings.watchFolders = defaultWatchFolders();
            if (num>=1 && num<=settings.watchFolders.size()) {
                console(L"  Sensitivity for this folder: 0=global, 1=low, 2=medium, 3=high");
                prompt(L"  > ");
                std::wstring c; if(!std::getline(std::wcin,c)) return; c=trimW(c);
                if(c==L"0") settings.watchFolders[num-1].sensitivity = -1;
                else if(c==L"1") settings.watchFolders[num-1].sensitivity = 0;
                else if(c==L"2") settings.watchFolders[num-1].sensitivity = 1;
                else if(c==L"3") settings.watchFolders[num-1].sensitivity = 2;
                saveSettings(settings); ok(L"Updated."); 
            }
            waitEnter();
        } else if (line == L"d" || line == L"D") {
            settings.watchFolders.clear(); saveSettings(settings); ok(L"Reset to defaults."); waitEnter();
        }
    }
}

static void settingsMenu(Settings& settings) {
    while (true) {
        showHeader();
        console(L"  [ SETTINGS ]");
        console(L"");
        console(L"  1. Global sensitivity: " + sensitivityName(settings.sensitivity));
        console(L"  2. Ultra-light mode:   " + std::wstring(settings.ultraLight ? L"ON" : L"OFF") + L" (lower CPU/IO)");
        console(L"  3. Watched folders:    " + std::to_wstring(effectiveWatchFolders(settings).size()) + L" (with per-folder sensitivity)");
        console(L"  4. Exclusions (whitelist): " + std::to_wstring(settings.excludePaths.size()) + L" paths, " + std::to_wstring(settings.excludeExts.size()) + L" extensions");
        console(L"  5. Auto-update check on start: " + std::wstring(settings.autoUpdateCheck ? L"ON" : L"OFF"));
        console(L"  6. Show paths (config/log/quarantine)");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1") {
            console(L"  1. Low  2. Medium  3. High  0. Back"); prompt(L"  > ");
            std::wstring c; if(!std::getline(std::wcin,c)) return; c=trimW(c);
            if(c==L"1") settings.sensitivity=0; else if(c==L"2") settings.sensitivity=1; else if(c==L"3") settings.sensitivity=2;
            if(c==L"1"||c==L"2"||c==L"3"){ saveSettings(settings); ok(L"Sensitivity: "+sensitivityName(settings.sensitivity)); waitEnter(); }
        } else if (line == L"2") {
            settings.ultraLight = !settings.ultraLight; saveSettings(settings);
            ok(std::wstring(L"Ultra-light mode ") + (settings.ultraLight?L"enabled":L"disabled") + L"."); waitEnter();
        } else if (line == L"3") {
            foldersMenu(settings);
        } else if (line == L"4") {
            exclusionsMenu(settings);
        } else if (line == L"5") {
            settings.autoUpdateCheck = !settings.autoUpdateCheck; saveSettings(settings);
            ok(std::wstring(L"Auto-update check ") + (settings.autoUpdateCheck?L"enabled":L"disabled") + L"."); waitEnter();
        } else if (line == L"6") {
            showHeader(); console(L"  [ PATHS ]"); console(L"");
            console(L"  Config:     " + configPath());
            console(L"  Log:        " + logPath());
            console(L"  Quarantine: " + quarantineRoot());
            waitEnter();
        }
    }
}

static void helpScreen() {
    showHeader();
    console(L"  [ HELP ]");
    console(L"");
    console(L"  Aegis watches your folders and moves ransomware-like threats");
    console(L"  to quarantine before they can do damage.");
    console(L"");
    printHelp();
    console(L"");
    console(L"  Notes:");
    console(L"  - Detection: canaries, entropy, rename/rate bursts, ransom notes,");
    console(L"    suspicious/double extensions, content patterns, hit escalation.");
    console(L"  - Use Settings for sensitivity, per-folder rules, exclusions, ultra-light mode.");
    console(L"  - Live alerts may appear asynchronously; use Status/Recent Alerts in Watch menu.");
    waitEnter();
}

static int runWatch(const Settings& settings, bool fromMenu, std::vector<std::wstring> overrideDirs = {}) {
    auto wfs = effectiveWatchFolders(settings);
    std::vector<std::wstring> dirs = overrideDirs.empty() ? effectiveWatchPaths(settings) : overrideDirs;
    if (dirs.empty()) dirs = effectiveWatchPaths(settings);

    const std::wstring qRoot = quarantineRoot();
    std::error_code ec;
    fs::create_directories(fs::path(qRoot).parent_path(), ec);

    Logger::instance().enableFile(logPath());

    Quarantine q(qRoot);
    Engine engine(q, dirs);
    engine.setSensitivity(settings.sensitivity);
    engine.setUltraLight(settings.ultraLight);
    engine.setExclusions(settings.excludePaths, settings.excludeExts);
    std::vector<std::pair<std::wstring,int>> fsens;
    if (overrideDirs.empty()) {
        for (auto& wf : wfs) fsens.push_back({wf.path, wf.sensitivity});
    }
    engine.setFolderSensitivities(fsens);
    engine.seedCanaries();
    Watcher watcher([&engine](const FileEvent& e) { engine.onEvent(e); });

    size_t added = 0;
    for (const auto& d : dirs) {
        if (watcher.add(d)) ++added;
        else warn(L"folder not found, skipping: " + d);
    }
    if (added == 0) {
        warn(L"nothing to watch — exiting");
        if (fromMenu) waitEnter();
        return 1;
    }

    ok(L"Aegis is on duty. Quarantine: " + qRoot);
    console(L"");
    watcher.start();

    while (true) {
        if (fromMenu) {
            showHeader();
            console(L"  [ WATCHING ]  Aegis is on duty");
            console(L"");
            console(L"  Folders: " + std::to_wstring(added) + L"  Sensitivity: " + sensitivityName(settings.sensitivity) +
                    (settings.ultraLight ? L"  [ULTRA-LIGHT]" : L""));
            console(L"  Events: " + std::to_wstring(engine.eventCount()) +
                    L"  Alerts: " + std::to_wstring(engine.alertCount()) +
                    L"  Quarantined: " + std::to_wstring(engine.quarantinedCount()) +
                    (engine.emergencyActive() ? L"  [EMERGENCY]" : L""));
            auto recent = Logger::instance().recentAlerts(3);
            if (!recent.empty()) {
                console(L"  Recent alerts:");
                for (auto& a : recent) console(L"    - " + a);
            }
            console(L"");
            console(L"  1. Status/details   2. Recent alerts   3. Quarantine/restore");
            console(L"  4. Help   0. Stop watching");
            console(L"");
            prompt(L"  > ");
        } else {
            prompt(L"aegis> ");
        }
        std::wstring line; if (!std::getline(std::wcin, line)) break;
        line = trimW(line);
        std::wstring low = lowerW(line);

        bool stop = false;
        if (fromMenu) {
            if (low == L"0" || low == L"q" || low == L"quit" || low == L"exit" || low.empty()) stop = true;
            else if (low == L"1" || low == L"status") {
                showHeader(); console(L"  [ STATUS ]"); console(L"");
                console(L"  events:          " + std::to_wstring(engine.eventCount()));
                console(L"  quarantined:     " + std::to_wstring(engine.quarantinedCount()));
                console(L"  alerts:          " + std::to_wstring(engine.alertCount()));
                console(L"  emergency mode:  " + std::wstring(engine.emergencyActive() ? L"YES" : L"no"));
                console(L"  folders watched: " + std::to_wstring(added));
                console(L"  sensitivity:     " + sensitivityName(settings.sensitivity) + (settings.ultraLight?L" (ultra-light)":L""));
                console(L"  exclusions:      " + std::to_wstring(settings.excludePaths.size()) + L" paths, " + std::to_wstring(settings.excludeExts.size()) + L" exts");
                console(L"  log file:        " + logPath());
                waitEnter();
            } else if (low == L"2") {
                alertsMenu();
            } else if (low == L"3" || low == L"quarantine" || low == L"restore") {
                quarantineMenu(q, &engine);
            } else if (low.rfind(L"restore ", 0) == 0) {
                showHeader(); console(L"  [ RESTORE ]"); console(L"");
                doRestoreByNumber(q, &engine, trimW(line.substr(8))); waitEnter();
            } else if (low == L"4" || low == L"help") {
                helpScreen();
            }
        } else {
            if (low == L"q" || low == L"quit" || low == L"exit") stop = true;
            else if (low == L"status") {
                console(L"  events:          " + std::to_wstring(engine.eventCount()));
                console(L"  quarantined:     " + std::to_wstring(engine.quarantinedCount()));
                console(L"  alerts:          " + std::to_wstring(engine.alertCount()));
                console(L"  emergency mode:  " + std::wstring(engine.emergencyActive() ? L"YES" : L"no"));
                console(L"  folders watched: " + std::to_wstring(added));
            } else if (low == L"quarantine") { auto items=q.list(); if(items.empty()) console(L"  quarantine is empty"); for(auto&it:items) console(L"  - "+it); }
            else if (low == L"restore") { printRecordList(q); console(L"  to restore: restore <number>"); }
            else if (low.rfind(L"restore ", 0) == 0) { doRestoreByNumber(q, &engine, trimW(line.substr(8))); }
            else if (low == L"help") { console(L""); printHelp(); }
            else if (!low.empty()) { console(L"unknown command: try help"); }
        }
        if (stop) break;
    }

    watcher.stop();
    ok(L"Aegis stopped. Talk soon!");
    if (fromMenu) waitEnter();
    return 0;
}

static int runQuarantineList() {
    Quarantine q(quarantineRoot());
    auto items = q.list();
    if (items.empty()) { console(L"Quarantine is empty."); return 0; }
    console(L"Quarantine (" + std::to_wstring(items.size()) + L") at " + quarantineRoot() + L":");
    for (const auto& it : items) console(L"  - " + it);
    return 0;
}

static void watchMenu(Settings& settings) {
    while (true) {
        showHeader();
        auto wfs = effectiveWatchFolders(settings);
        console(L"  [ WATCH ]");
        console(L"");
        console(L"  Global sensitivity: " + sensitivityName(settings.sensitivity) + (settings.ultraLight?L"  (ultra-light)":L""));
        console(L"  Folders (" + std::to_wstring(wfs.size()) + L"):");
        for (size_t i = 0; i < wfs.size(); ++i) {
            std::wstring s = (wfs[i].sensitivity<0)?L"global":sensitivityName(wfs[i].sensitivity);
            console(L"    - " + wfs[i].path + L" ["+s+L"]");
        }
        if (!settings.excludePaths.empty() || !settings.excludeExts.empty())
            console(L"  Exclusions: " + std::to_wstring(settings.excludePaths.size()) + L" paths, " + std::to_wstring(settings.excludeExts.size()) + L" exts");
        console(L"");
        console(L"  1. Start watching now");
        console(L"  2. Configure watched folders");
        console(L"  3. Settings (sensitivity/exclusions/ultra-light)");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1") runWatch(settings, true);
        else if (line == L"2") foldersMenu(settings);
        else if (line == L"3") settingsMenu(settings);
    }
}

static int interactiveMenu() {
    Settings settings = loadSettings();
    if (settings.autoUpdateCheck) {
        std::wstring latest = checkLatestVersion();
        if (!latest.empty()) {
            settings.lastUpdateAvailable = true; settings.lastUpdateVersion = latest;
            saveSettings(settings);
        } else {
            // keep quiet; don't spam
        }
    }
    while (true) {
        settings = loadSettings();
        showHeader();
        console(L"  [ MAIN MENU ]");
        console(L"");
        if (settings.lastUpdateAvailable && !settings.lastUpdateVersion.empty())
            console(L"  * Update available: " + settings.lastUpdateVersion + L"  (see Update)");
        console(L"  1. Watch / protect folders");
        console(L"  2. Quarantine (view / restore / delete)");
        console(L"  3. History / stats / search");
        console(L"  4. Settings (folders, sensitivity, exclusions, ultra-light)");
        console(L"  5. Diagnostics / check setup");
        console(L"  6. Recent alerts buffer");
        console(L"  7. Export report");
        console(L"  8. Update Aegis");
        console(L"  9. Autostart");
        console(L"  h. Help");
        console(L"  0. Exit");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) break;
        line = trimW(line);
        std::wstring low = lowerW(line);

        if (low == L"1" || low == L"watch") watchMenu(settings);
        else if (low == L"2" || low == L"quarantine") { Quarantine q(quarantineRoot()); quarantineMenu(q, nullptr); }
        else if (low == L"3" || low == L"history" || low == L"stats") historyMenu();
        else if (low == L"4" || low == L"settings") settingsMenu(settings);
        else if (low == L"5" || low == L"diag" || low == L"diagnostics") diagnosticsMenu(settings);
        else if (low == L"6" || low == L"alerts") alertsMenu();
        else if (low == L"7" || low == L"export") exportReport();
        else if (low == L"8" || low == L"update") updateMenu();
        else if (low == L"9" || low == L"autostart") autostartMenu();
        else if (low == L"h" || low == L"help") helpScreen();
        else if (low == L"0" || low == L"q" || low == L"exit") {
            clearScreen(); banner(); ok(L"Bye!"); return 0;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    setupConsole();
#endif

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(toW(argv[i]));

    ae::cleanupOldBinary();

    if (args.empty()) return interactiveMenu();
    if (args[0] == L"help" || args[0] == L"--help" || args[0] == L"/?") { banner(); printHelp(); return 0; }
    if (args[0] == L"watch") {
        banner(); args.erase(args.begin());
        Settings s = loadSettings();
        if (args.empty()) return runWatch(s, false);
        return runWatch(s, false, args);
    }
    if (args[0] == L"quarantine") { banner(); return runQuarantineList(); }
    if (args[0] == L"update") {
        banner(); args.erase(args.begin());
        bool assumeYes = false; for (const auto& a : args) if (a == L"-y" || a == L"--yes") assumeYes = true;
        return runUpdate(assumeYes);
    }
    if (args[0] == L"autostart") {
        banner(); args.erase(args.begin());
        if (args.empty()) { warn(L"usage: aegis autostart on|off"); return 1; }
        return runAutostart(args[0]);
    }
    if (args[0] == L"restore") {
        banner(); args.erase(args.begin());
        Quarantine q(quarantineRoot());
        if (args.empty()) { printRecordList(q); return 0; }
        return doRestoreByNumber(q, nullptr, args[0]);
    }

    banner();
    console(L"Unknown command: " + args[0]);
    console(L"");
    printHelp();
    return 1;
}
