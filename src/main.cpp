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

static std::wstring backupRoot() {
    return (fs::path(aegisDir()) / L"backups").wstring();
}

static std::wstring rulesPath() {
    return (fs::path(aegisDir()) / L"rules.txt").wstring();
}

static std::wstring modsDir() {
    return (fs::path(aegisDir()) / L"mods").wstring();
}

static void applyUiTheme(const Settings& s) {
    Logger::instance().setTheme(s.uiTheme);
}

static std::string to8(const std::wstring& w){
    std::string s;
#ifdef _WIN32
    if (w.empty()) return s;
    int n = WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    s.resize(n); if(n>0) WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);
#else
    s.assign(w.begin(), w.end());
#endif
    return s;
}

// (reserved for future mod parsing helpers)

struct ModInfo {
    std::wstring file;
    std::wstring name;
    std::wstring id;
    std::wstring type;
    std::wstring status;
    std::wstring applies;
};

static ModInfo readModInfo(const fs::path& p) {
    ModInfo m;
    m.file = p.filename().wstring();
    m.name = p.stem().wstring();
    m.status = L"unknown";
    std::ifstream f(p, std::ios::binary);
    if (!f) return m;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq+1);
        if (k=="name") m.name = toW(v);
        else if (k=="id") m.id = toW(v);
        else if (k=="type") m.type = toW(v);
        else if (k=="status") m.status = toW(v);
        else if (k=="applies") m.applies = toW(v);
    }
    return m;
}

static std::vector<ModInfo> listMods() {
    std::vector<ModInfo> out;
    std::error_code ec;
    fs::path md = fs::path(modsDir());
    if (!fs::exists(md, ec)) return out;
    for (auto& e : fs::directory_iterator(md, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto ext = lowerW(e.path().extension().wstring());
        if (ext == L".aegismod" || ext == L".mod" || ext == L".txt") out.push_back(readModInfo(e.path()));
    }
    std::sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b){ return a.file < b.file; });
    return out;
}

static int runModCommand(const std::vector<std::wstring>& argsIn) {
    auto args = argsIn;
    std::error_code ec;
    fs::create_directories(fs::path(modsDir()), ec);
    if (args.empty() || args[0] == L"list") {
        console(L"Mods folder: " + modsDir());
        auto mods = listMods();
        if (mods.empty()) { console(L"  (no mods found)"); console(L"  Tip: create one with: aegis mod create my-tweak"); return 0; }
        for (auto& m : mods) {
            std::wstring line = L"  - " + m.name + L" [" + m.file + L"]";
            if (!m.type.empty()) line += L" type=" + m.type;
            if (!m.status.empty()) line += L" status=" + m.status;
            console(line);
            if (!m.applies.empty()) console(L"      applies: " + m.applies);
        }
        return 0;
    }
    if (args[0] == L"path") {
        console(modsDir());
        return 0;
    }
    if (args[0] == L"create") {
        if (args.size() < 2) { warn(L"usage: aegis mod create <name>"); return 1; }
        std::wstring name = trimW(args[1]);
        for (auto& c : name) if (c==L' '||c==L'\\'||c==L'/') c = L'-';
        if (name.empty()) { warn(L"invalid mod name"); return 1; }
        fs::path out = fs::path(modsDir()) / (name + L".aegismod");
        if (fs::exists(out, ec)) { warn(L"mod already exists: " + out.wstring()); return 1; }
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) { warn(L"could not create mod file"); return 1; }
        f << "name=" << to8(name) << "\n";
        f << "id=" << to8(name) << "\n";
        f << "author=\n";
        f << "version=0.1.0\n";
        f << "type=tweak\n";
        f << "status=in-development\n";
        f << "description=\n";
        f << "applies=\n";
        f << "\n# Aegis mod/tweak manifest (author tool scaffold)\n";
        f << "# This is a starter file for tweak authors. Runtime mod loading is limited/in development.\n";
        f << "# For UI theme tweaks, you can set: applies=ui_theme:white or ui_theme:black and run: aegis mod apply " << to8(name) << "\n";
        ok(L"Created mod scaffold: " + out.wstring());
        console(L"Edit the file to describe your tweak. (Mod system in development)");
        return 0;
    }
    if (args[0] == L"apply") {
        if (args.size() < 2) { warn(L"usage: aegis mod apply <name|file|path>"); return 1; }
        std::wstring key = trimW(args[1]);
        fs::path target;
        for (auto& m : listMods()) {
            if (m.file == key || m.name == key || m.id == key || fs::path(m.file).stem().wstring() == key) { target = fs::path(modsDir()) / m.file; break; }
        }
        if (target.empty()) {
            fs::path p(key);
            if (p.is_absolute() || key.find(L'\\') != std::wstring::npos || key.find(L'/') != std::wstring::npos) target = p;
            else target = fs::path(modsDir()) / key;
        }
        if (!fs::exists(target, ec)) { warn(L"mod not found: " + key); return 1; }
        ModInfo mi = readModInfo(target);
        if (mi.applies == L"ui_theme:white" || mi.applies == L"ui_theme:black" || mi.applies == L"ui_theme:default") {
            Settings s = loadSettings();
            s.uiTheme = mi.applies.substr(std::wstring(L"ui_theme:").size());
            saveSettings(s);
            applyUiTheme(s);
            ok(L"Applied tweak: " + mi.name + L" (ui theme = " + s.uiTheme + L")");
            console(L"Note: tweak/mod system is in development; currently supports simple UI theme tweaks.");
            return 0;
        }
        warn(L"This mod does not declare a supported applies= action yet (mod system in development).");
        console(L"Supported now: applies=ui_theme:white | ui_theme:black | ui_theme:default");
        return 1;
    }
    warn(L"unknown mod subcommand: " + args[0]);
    console(L"usage: aegis mod [list|path|create <name>|apply <name|path>]");
    return 1;
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

static void loadUserRules(Settings& s) {
    // Simple user rules file: exclude_path=..., exclude_ext=...
    std::error_code ec;
    fs::path rp = fs::path(rulesPath());
    if (!fs::exists(rp, ec)) return;
    std::ifstream f(rp, std::ios::binary);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("exclude_path=", 0) == 0) {
            std::wstring w = toW(line.substr(13));
            if (!w.empty()) s.excludePaths.push_back(w);
        } else if (line.rfind("exclude_ext=", 0) == 0) {
            std::wstring w = toW(line.substr(12));
            if (!w.empty()) {
                if (w[0] != L'.') w = L"." + w;
                s.excludeExts.push_back(w);
            }
        }
    }
}

static void applyProfile(Settings& s, const std::wstring& name) {
    if (name == L"performance") {
        s.sensitivity = 0; s.ultraLight = true;
    } else if (name == L"balanced") {
        s.sensitivity = 1; s.ultraLight = false;
    } else if (name == L"strict") {
        s.sensitivity = 2; s.ultraLight = false;
    } else if (name == L"documents") {
        s.sensitivity = 2; s.ultraLight = false;
    }
    saveSettings(s);
}

static void showHeader() {
    clearScreen();
    banner();
}

static void printHelp() {
    console(L"Commands:");
    console(L"  aegis                            interactive menu");
    console(L"  aegis watch [dir1 dir2 ...]      watch folders (default or saved/custom)");
    console(L"  aegis watch --daemon [dirs...]   watch without interactive command loop (service-friendly)");
    console(L"  aegis status                     quick health/status summary");
    console(L"  aegis quarantine                 show quarantine contents");
    console(L"  aegis restore [number]           list or restore quarantined files");
    console(L"  aegis export [path]              export quarantine/report (txt; .json for JSON)");
    console(L"  aegis mod [list|path|create|apply]  mods/tweaks (author tool; in development)");
    console(L"  aegis update [-y]                check GitHub and update Aegis itself");
    console(L"  aegis autostart on|off           enable/disable automatic background protection at login");
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

static bool isRecentRecord(const Quarantine::Record& r, int minutes) {
    // try parse taken-at stamp like YYYYMMDD_HHMMSS
    std::wstring t = r.time;
    if (t.size() < 15) {
        // fallback: folder name prefix
        fs::path p(r.folder);
        t = p.filename().wstring();
    }
    if (t.size() < 15) return false;
    auto digit = [](wchar_t c){return c>=L'0'&&c<=L'9';};
    if(!(digit(t[0])&&digit(t[1])&&digit(t[2])&&digit(t[3])&&digit(t[4])&&digit(t[5])&&digit(t[6])&&digit(t[7])&&t[8]==L'_'&&digit(t[9])&&digit(t[10])&&digit(t[11])&&digit(t[12])&&digit(t[13])&&digit(t[14]))) return false;
    std::tm tm{};
    tm.tm_year = (t[0]-L'0')*1000 + (t[1]-L'0')*100 + (t[2]-L'0')*10 + (t[3]-L'0') - 1900;
    tm.tm_mon  = (t[4]-L'0')*10 + (t[5]-L'0') - 1;
    tm.tm_mday = (t[6]-L'0')*10 + (t[7]-L'0');
    tm.tm_hour = (t[9]-L'0')*10 + (t[10]-L'0');
    tm.tm_min  = (t[11]-L'0')*10 + (t[12]-L'0');
    tm.tm_sec  = (t[13]-L'0')*10 + (t[14]-L'0');
    std::time_t then = std::mktime(&tm);
    if (then == (std::time_t)-1) return false;
    std::time_t now = std::time(nullptr);
    double diff = std::difftime(now, then);
    return diff >= 0 && diff <= minutes * 60;
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
        console(L"  Enter number to restore, d<number> to delete, a restore-all, r restore recent (30m), c clear-all");
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
        if (line == L"r" || line == L"R") {
            auto all = q.records();
            int restored = 0;
            for (auto& r : all) {
                if (!isRecentRecord(r, 30)) continue;
                if (engine) engine->ignorePath(r.from);
                if (q.restore(r)) restored++;
            }
            ok(L"Restored " + std::to_wstring(restored) + L" recent file(s) (last 30 min).");
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

// (reserved for future report escaping helpers)

static int exportReportTo(const std::wstring& pathW, bool json) {
    Quarantine q(quarantineRoot());
    auto recs = q.records();
    std::ofstream f(fs::path(pathW), std::ios::binary | std::ios::trunc);
    if (!f) return 1;
    if (!json) {
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
    } else {
        auto esc = [](const std::string& s){
            std::string o; o.reserve(s.size()+8);
            for(char c: s){
                if(c=='\\' || c=='"'){ o.push_back('\\'); o.push_back(c); }
                else if(c=='\n'){ o += "\\n"; }
                else if(c=='\r'){ o += "\\r"; }
                else if(c=='\t'){ o += "\\t"; }
                else { o.push_back(c); }
            }
            return o;
        };
        f << "{\n  \"version\": \"" << esc(to8(ae::kVersion)) << "\",\n  \"quarantine_items\": " << recs.size() << ",\n  \"records\": [\n";
        for (size_t i=0;i<recs.size();++i){
            const auto& r=recs[i];
            f << "    {\"file\":\""<<esc(to8(r.file))<<"\",\"time\":\""<<esc(to8(r.time))<<"\",\"reason\":\""<<esc(to8(r.reason))<<"\",\"from\":\""<<esc(to8(r.from))<<"\"}";
            if(i+1<recs.size()) f << ",";
            f << "\n";
        }
        f << "  ]\n}\n";
    }
    return 0;
}

static void exportReport() {
    showHeader();
    console(L"  [ EXPORT REPORT ]");
    console(L"");
    fs::path out = fs::path(aegisDir()) / L"aegis-report.txt";
    if (exportReportTo(out.wstring(), false) != 0) { warn(L"Could not write report."); waitEnter(); return; }
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
    fs::create_directories(fs::path(backupRoot()), ec);
    console(L"  [INFO] Backups dir: " + backupRoot() + (settings.backupBeforeQuarantine ? L" (enabled)" : L" (disabled)"));
    fs::path lp = fs::path(logPath()).parent_path();
    fs::create_directories(lp, ec);
    if (fs::exists(lp)) { console(L"  [OK]   Config/log dir: " + lp.wstring()); okCount++; }
    else { console(L"  [WARN] Config/log dir issue: " + lp.wstring()); warnCount++; }
    if (fs::exists(fs::path(configPath()), ec)) { console(L"  [OK]   Config file: " + configPath()); okCount++; }
    else { console(L"  [INFO] Config file will be created on save: " + configPath()); }
    if (fs::exists(fs::path(rulesPath()), ec)) { console(L"  [OK]   User rules file: " + rulesPath()); okCount++; }
    else { console(L"  [INFO] No user rules file (optional): " + rulesPath()); }
    for (const auto& d : dirs) {
        fs::path c = fs::path(d) / L".aegis-canary.txt";
        if (fs::exists(c, ec)) { console(L"  [OK]   Canary present: " + c.wstring()); okCount++; }
        else { console(L"  [INFO] Canary not yet planted: " + c.wstring()); }
    }
    // basic write test in quarantine
    {
        fs::path test = fs::path(quarantineRoot()) / L".aegis_write_test";
        std::ofstream tf(test, std::ios::binary | std::ios::trunc);
        if (tf) { tf << "ok"; tf.close(); fs::remove(test, ec); console(L"  [OK]   Quarantine write test passed"); okCount++; }
        else { console(L"  [WARN] Quarantine write test failed"); warnCount++; }
    }
    console(L"");
    console(L"  Summary: " + std::to_wstring(okCount) + L" OK, " + std::to_wstring(warnCount) + L" warning(s).");
    console(L"  Tip: canaries are planted when protection starts. Ultra-light mode reduces CPU/IO.");
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
        console(L"  3. Backup before quarantine: " + std::wstring(settings.backupBeforeQuarantine ? L"ON" : L"OFF"));
        console(L"  4. Watched folders:    " + std::to_wstring(effectiveWatchFolders(settings).size()) + L" (with per-folder sensitivity)");
        console(L"  5. Exclusions (whitelist): " + std::to_wstring(settings.excludePaths.size()) + L" paths, " + std::to_wstring(settings.excludeExts.size()) + L" extensions");
        console(L"  6. Auto-update check on start: " + std::wstring(settings.autoUpdateCheck ? L"ON" : L"OFF"));
        console(L"  7. Language (UI): " + settings.language + L" (en/ru foundation)");
        console(L"  8. UI theme/tweak: " + settings.uiTheme + L" (white/black in development)");
        console(L"  9. Profiles / presets");
        console(L"  10. Export settings");
        console(L"  11. Import settings");
        console(L"  12. Mods/tweaks (author tools, in development)");
        console(L"  13. Show paths (config/log/quarantine/rules/backup/mods)");
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
            settings.backupBeforeQuarantine = !settings.backupBeforeQuarantine; saveSettings(settings);
            ok(std::wstring(L"Backup before quarantine ") + (settings.backupBeforeQuarantine?L"enabled":L"disabled") + L"."); waitEnter();
        } else if (line == L"4") {
            foldersMenu(settings);
        } else if (line == L"5") {
            exclusionsMenu(settings);
        } else if (line == L"6") {
            settings.autoUpdateCheck = !settings.autoUpdateCheck; saveSettings(settings);
            ok(std::wstring(L"Auto-update check ") + (settings.autoUpdateCheck?L"enabled":L"disabled") + L"."); waitEnter();
        } else if (line == L"7") {
            showHeader(); console(L"  [ LANGUAGE ]"); console(L"");
            console(L"  1. English (en)");
            console(L"  2. Russian (ru) - foundation/partial");
            console(L"  0. Back");
            prompt(L"  > ");
            std::wstring c; if(!std::getline(std::wcin,c)) return; c=trimW(c);
            if(c==L"1") { settings.language=L"en"; saveSettings(settings); ok(L"Language set to en."); waitEnter(); }
            else if(c==L"2") { settings.language=L"ru"; saveSettings(settings); ok(L"Language set to ru (foundation; full UI translation can be expanded)."); waitEnter(); }
        } else if (line == L"8") {
            showHeader(); console(L"  [ UI THEME / TWEAK ]"); console(L"");
            console(L"  Theme tweak for UI log/tag accents (in development):");
            console(L"  1. Default");
            console(L"  2. White tweak (in development)");
            console(L"  3. Black tweak (in development)");
            console(L"  0. Back");
            prompt(L"  > ");
            std::wstring c; if(!std::getline(std::wcin,c)) return; c=trimW(c);
            if(c==L"1") { settings.uiTheme=L"default"; saveSettings(settings); applyUiTheme(settings); ok(L"UI theme: default"); waitEnter(); }
            else if(c==L"2") { settings.uiTheme=L"white"; saveSettings(settings); applyUiTheme(settings); ok(L"UI theme tweak applied: white (in development)"); waitEnter(); }
            else if(c==L"3") { settings.uiTheme=L"black"; saveSettings(settings); applyUiTheme(settings); ok(L"UI theme tweak applied: black (in development)"); waitEnter(); }
        } else if (line == L"9") {
            showHeader(); console(L"  [ PROFILES ]"); console(L"");
            console(L"  1. Balanced (default)");
            console(L"  2. Strict protection");
            console(L"  3. Performance / ultra-light");
            console(L"  4. Documents (strict)");
            console(L"  0. Back");
            prompt(L"  > ");
            std::wstring c; if(!std::getline(std::wcin,c)) return; c=trimW(c);
            if(c==L"1") { applyProfile(settings, L"balanced"); ok(L"Applied: Balanced"); waitEnter(); }
            else if(c==L"2") { applyProfile(settings, L"strict"); ok(L"Applied: Strict protection"); waitEnter(); }
            else if(c==L"3") { applyProfile(settings, L"performance"); ok(L"Applied: Performance/ultra-light"); waitEnter(); }
            else if(c==L"4") { applyProfile(settings, L"documents"); ok(L"Applied: Documents (strict)"); waitEnter(); }
        } else if (line == L"10") {
            showHeader(); console(L"  [ EXPORT SETTINGS ]"); console(L"");
            console(L"  Enter destination file path (or empty to cancel):"); prompt(L"  > ");
            std::wstring p; if(!std::getline(std::wcin,p)) return; p=trimW(p);
            if(!p.empty()){
                std::error_code ec; fs::copy_file(fs::path(configPath()), fs::path(p), fs::copy_options::overwrite_existing, ec);
                if(ec) warn(L"Export failed."); else ok(L"Settings exported to: " + p);
                waitEnter();
            }
        } else if (line == L"11") {
            showHeader(); console(L"  [ IMPORT SETTINGS ]"); console(L"");
            console(L"  Enter source file path (or empty to cancel):"); prompt(L"  > ");
            std::wstring p; if(!std::getline(std::wcin,p)) return; p=trimW(p);
            if(!p.empty()){
                std::error_code ec; fs::create_directories(fs::path(configPath()).parent_path(), ec);
                ec.clear(); fs::copy_file(fs::path(p), fs::path(configPath()), fs::copy_options::overwrite_existing, ec);
                if(ec) warn(L"Import failed."); else { settings = loadSettings(); applyUiTheme(settings); ok(L"Settings imported."); }
                waitEnter();
            }
        } else if (line == L"12") {
            showHeader(); console(L"  [ MODS / TWEAKS ] (in development)"); console(L"");
            std::error_code ec; fs::create_directories(fs::path(modsDir()), ec);
            console(L"  Mods folder: " + modsDir());
            console(L"  Use CLI author tools: aegis mod list | path | create <name> | apply <name>");
            console(L"");
            auto mods = listMods();
            if (mods.empty()) console(L"  No mods found. Create one: aegis mod create my-tweak");
            else for (auto& m : mods) console(L"  - " + m.name + L" [" + m.file + L"] status=" + m.status);
            waitEnter();
        } else if (line == L"13") {
            showHeader(); console(L"  [ PATHS ]"); console(L"");
            console(L"  Config:     " + configPath());
            console(L"  Rules:      " + rulesPath());
            console(L"  Log:        " + logPath());
            console(L"  Quarantine: " + quarantineRoot());
            console(L"  Backups:    " + backupRoot());
            console(L"  Mods:       " + modsDir());
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

static int runWatch(const Settings& inSettings, bool fromMenu, std::vector<std::wstring> overrideDirs = {}, bool daemonMode = false) {
    Settings settings = inSettings;
    applyUiTheme(settings);
    loadUserRules(settings); // merge extra exclusions from ~/.aegis/rules.txt

    auto wfs = effectiveWatchFolders(settings);
    std::vector<std::wstring> dirs = overrideDirs.empty() ? effectiveWatchPaths(settings) : overrideDirs;
    if (dirs.empty()) dirs = effectiveWatchPaths(settings);

    const std::wstring qRoot = quarantineRoot();
    std::error_code ec;
    fs::create_directories(fs::path(qRoot).parent_path(), ec);
    if (settings.backupBeforeQuarantine) fs::create_directories(fs::path(backupRoot()), ec);

    Logger::instance().enableFile(logPath());

    Quarantine q(qRoot);
    Engine engine(q, dirs);
    engine.setSensitivity(settings.sensitivity);
    engine.setUltraLight(settings.ultraLight);
    engine.setExclusions(settings.excludePaths, settings.excludeExts);
    if (settings.backupBeforeQuarantine) engine.setBackupRoot(backupRoot());
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
    if (!daemonMode) console(L"");
    watcher.start();

    if (daemonMode) {
        console(L"Running in daemon/non-interactive mode. Press Ctrl+C to stop.");
        // block until stdin closes or interrupted; simple loop
        std::wstring dummy;
        while (std::getline(std::wcin, dummy)) {
            // ignore input in daemon mode
        }
        watcher.stop();
        ok(L"Aegis stopped. Talk soon!");
        return 0;
    }

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
    applyUiTheme(settings);
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
    if (args[0] == L"status") {
        banner();
        Settings s = loadSettings();
        applyUiTheme(s);
        Quarantine q(quarantineRoot());
        console(L"Version: " + std::wstring(ae::kVersion));
        console(L"Config:  " + configPath());
        console(L"Log:     " + logPath());
        console(L"Quarantine: " + quarantineRoot() + L" (" + std::to_wstring(q.count()) + L" items)");
        console(L"Watched folders: " + std::to_wstring(effectiveWatchPaths(s).size()));
        console(L"Sensitivity: " + sensitivityName(s.sensitivity) + (s.ultraLight?L" (ultra-light)":L""));
        console(L"UI theme: " + s.uiTheme + L" (mods/tweaks in development)");
        console(L"Mods folder: " + modsDir());
        console(L"Exclusions: " + std::to_wstring(s.excludePaths.size()) + L" paths, " + std::to_wstring(s.excludeExts.size()) + L" exts");
        return 0;
    }
    if (args[0] == L"watch") {
        args.erase(args.begin());
        Settings s = loadSettings();
        applyUiTheme(s);
        bool daemon = false;
        std::vector<std::wstring> dirs;
        for (auto& a : args) {
            if (a == L"--daemon" || a == L"-d") daemon = true;
            else dirs.push_back(a);
        }
#ifdef _WIN32
        if (daemon) {
            // Hide console for background protection (autostart/service-like usage)
            HWND hw = GetConsoleWindow();
            if (hw) ShowWindow(hw, SW_HIDE);
        }
#endif
        if (!daemon) banner();
        if (dirs.empty()) return runWatch(s, false, {}, daemon);
        return runWatch(s, false, dirs, daemon);
    }
    if (args[0] == L"quarantine") { banner(); return runQuarantineList(); }
    if (args[0] == L"export") {
        banner(); args.erase(args.begin());
        bool json = false;
        std::wstring path = (fs::path(aegisDir()) / L"aegis-report.txt").wstring();
        for (auto& a : args) {
            if (a == L"--json") json = true;
            else path = a;
        }
        if (json && path.find(L".json") == std::wstring::npos && args.size() <= 1) path = (fs::path(aegisDir()) / L"aegis-report.json").wstring();
        int rc = exportReportTo(path, json);
        if (rc == 0) ok(L"Report exported: " + path); else warn(L"Export failed.");
        return rc;
    }
    if (args[0] == L"mod" || args[0] == L"mods") {
        Settings s = loadSettings(); applyUiTheme(s);
        banner(); args.erase(args.begin());
        return runModCommand(args);
    }
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
