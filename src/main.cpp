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
    console(L"                      by drug addict");
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

static std::wstring homeDir() {
#ifdef _WIN32
    const char* up = std::getenv("USERPROFILE");
    return up ? toW(up) : L"";
#else
    const char* h = std::getenv("HOME");
    return h ? toW(h) : L"";
#endif
}

static std::wstring quarantineRoot() {
    std::wstring home = homeDir();
    if (home.empty()) home = L".";
#ifdef _WIN32
    return home + L"\\.aegis\\quarantine";
#else
    return home + L"/.aegis/quarantine";
#endif
}

static std::wstring logPath() {
    std::wstring home = homeDir();
    if (home.empty()) home = L".";
#ifdef _WIN32
    return home + L"\\.aegis\\aegis.log";
#else
    return home + L"/.aegis/aegis.log";
#endif
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

static std::vector<std::wstring> defaultDirs() {
    std::vector<std::wstring> d;
    pushDefaultDir(d, 0); pushDefaultDir(d, 1); pushDefaultDir(d, 2);
    return d;
}

static std::vector<std::wstring> effectiveWatchDirs(const Settings& s) {
    if (!s.watchDirs.empty()) return s.watchDirs;
    return defaultDirs();
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
    console(L"  aegis restore <number>           take a file back out of quarantine");
    console(L"  aegis update [-y]                check GitHub and update Aegis itself");
    console(L"  aegis autostart on|off           start Aegis automatically with the system");
    console(L"  aegis help                       this help");
    console(L"");
    console(L"Commands while watching (CLI mode):");
    console(L"  status  quarantine  restore [n]  help  q");
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
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') { console(L"number must be digits: " + s); return 1; }
        num = num * 10 + (size_t)(c - L'0');
    }
    auto recs = q.records();
    if (num == 0 || num > recs.size()) {
        console(L"no such record: " + std::to_wstring(num) + L" (quarantine holds " + std::to_wstring(recs.size()) + L")");
        return 1;
    }
    Quarantine::Record rec = recs[num - 1];
    if (engine) engine->ignorePath(rec.from);
    if (q.restore(rec)) { ok(L"restored from quarantine: " + rec.from); return 0; }
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
        if (!recs.empty()) console(L"  Enter number to restore, or 0 to go back");
        else console(L"  0. Back");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
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
    showHeader();
    console(L"  [ HISTORY / STATS ]");
    console(L"");
    Quarantine q(quarantineRoot());
    auto recs = q.records();
    console(L"  Quarantine items: " + std::to_wstring(recs.size()));
    console(L"  Quarantine path:  " + quarantineRoot());
    console(L"  Log file:         " + logPath());
    console(L"");
    if (recs.empty()) {
        console(L"  No quarantined files yet.");
    } else {
        console(L"  Recent quarantine records:");
        console(L"");
        int i = 1;
        for (const auto& r : recs) {
            console(L"  [" + std::to_wstring(i++) + L"] " + r.file);
            if (!r.time.empty()) console(L"      time:   " + r.time);
            console(L"      reason: " + r.reason);
            console(L"      from:   " + r.from);
        }
    }
    waitEnter();
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

static void foldersMenu(Settings& settings) {
    while (true) {
        showHeader();
        console(L"  [ WATCHED FOLDERS ]");
        console(L"");
        auto eff = effectiveWatchDirs(settings);
        bool usingDefaults = settings.watchDirs.empty();
        console(usingDefaults ? L"  Mode: default folders (Desktop, Documents, Downloads)" : L"  Mode: custom folder list");
        console(L"");
        for (size_t i = 0; i < eff.size(); ++i)
            console(L"  " + std::to_wstring(i + 1) + L". " + eff[i]);
        if (eff.empty()) console(L"  (no folders)");
        console(L"");
        console(L"  a. Add a folder path");
        console(L"  r. Remove a folder by number (switches to custom list)");
        console(L"  d. Reset to default folders");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"a" || line == L"A") {
            showHeader(); console(L"  [ ADD FOLDER ]"); console(L"");
            console(L"  Enter full folder path (or empty to cancel):");
            prompt(L"  > ");
            std::wstring p; if (!std::getline(std::wcin, p)) return;
            p = trimW(p);
            if (!p.empty()) {
                if (settings.watchDirs.empty()) settings.watchDirs = defaultDirs();
                settings.watchDirs.push_back(p);
                saveSettings(settings);
                ok(L"Folder added.");
                waitEnter();
            }
        } else if (line == L"r" || line == L"R") {
            showHeader(); console(L"  [ REMOVE FOLDER ]"); console(L"");
            console(L"  Enter folder number to remove (or 0 to cancel):");
            prompt(L"  > ");
            std::wstring n; if (!std::getline(std::wcin, n)) return;
            n = trimW(n);
            size_t num = 0; bool okn = !n.empty();
            for (wchar_t c : n) if (c < L'0' || c > L'9') { okn = false; break; }
            if (okn) {
                for (wchar_t c : n) num = num * 10 + (size_t)(c - L'0');
                if (settings.watchDirs.empty()) settings.watchDirs = defaultDirs();
                if (num >= 1 && num <= settings.watchDirs.size()) {
                    settings.watchDirs.erase(settings.watchDirs.begin() + (num - 1));
                    saveSettings(settings);
                    ok(L"Folder removed.");
                } else if (num != 0) {
                    warn(L"No such folder number.");
                }
            }
            waitEnter();
        } else if (line == L"d" || line == L"D") {
            settings.watchDirs.clear();
            saveSettings(settings);
            ok(L"Reset to default folders.");
            waitEnter();
        }
    }
}

static void settingsMenu(Settings& settings) {
    while (true) {
        showHeader();
        console(L"  [ SETTINGS ]");
        console(L"");
        console(L"  1. Sensitivity: " + sensitivityName(settings.sensitivity) + L"  (low = fewer false positives, high = stricter)");
        console(L"  2. Watched folders (" + std::to_wstring(effectiveWatchDirs(settings).size()) + L")");
        console(L"  3. Show config path");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1") {
            showHeader(); console(L"  [ SENSITIVITY ]"); console(L"");
            console(L"  1. Low    - fewer false positives, slower to trigger");
            console(L"  2. Medium - balanced (default)");
            console(L"  3. High   - stricter/faster detection");
            console(L"  0. Back");
            console(L"");
            prompt(L"  > ");
            std::wstring c; if (!std::getline(std::wcin, c)) return;
            c = trimW(c);
            if (c == L"1") settings.sensitivity = 0;
            else if (c == L"2") settings.sensitivity = 1;
            else if (c == L"3") settings.sensitivity = 2;
            if (c == L"1" || c == L"2" || c == L"3") { saveSettings(settings); ok(L"Sensitivity set to " + sensitivityName(settings.sensitivity) + L"."); waitEnter(); }
        } else if (line == L"2") {
            foldersMenu(settings);
        } else if (line == L"3") {
            showHeader(); console(L"  [ CONFIG ]"); console(L"");
            console(L"  Config file: " + configPath());
            console(L"  Log file:    " + logPath());
            console(L"  Quarantine:  " + quarantineRoot());
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
    console(L"  - Detection uses canaries, entropy, rename bursts, ransom notes,");
    console(L"    suspicious extensions and content patterns.");
    console(L"  - Tune sensitivity in Settings if you see false positives.");
    console(L"  - While watching, live alerts may print asynchronously; check Status/Quarantine.");
    waitEnter();
}

static int runWatch(std::vector<std::wstring> dirs, bool fromMenu, int sensitivity) {
    if (dirs.empty()) dirs = defaultDirs();

    const std::wstring qRoot = quarantineRoot();
    std::error_code ec;
    fs::create_directories(fs::path(qRoot).parent_path(), ec);

    Logger::instance().enableFile(logPath());

    Quarantine q(qRoot);
    Engine engine(q, dirs);
    engine.setSensitivity(sensitivity);
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
            console(L"  Protected folders: " + std::to_wstring(added) + L"    Sensitivity: " + sensitivityName(sensitivity));
            console(L"  Events: " + std::to_wstring(engine.eventCount()) +
                    L"  Alerts: " + std::to_wstring(engine.alertCount()) +
                    L"  Quarantined: " + std::to_wstring(engine.quarantinedCount()) +
                    (engine.emergencyActive() ? L"  [EMERGENCY]" : L""));
            console(L"  (Live alerts may appear between screens; Status/Quarantine show details.)");
            console(L"");
            console(L"  1. Status (details)");
            console(L"  2. Quarantine / restore");
            console(L"  3. Help");
            console(L"  0. Stop watching (back to main menu)");
            console(L"");
            prompt(L"  > ");
        } else {
            prompt(L"aegis> ");
        }
        std::wstring line; if (!std::getline(std::wcin, line)) break;
        line = trimW(line);
        std::wstring low = line;
        for (auto& c : low) c = towlower(c);

        bool stop = false;
        if (fromMenu) {
            if (low == L"0" || low == L"q" || low == L"quit" || low == L"exit" || low.empty()) {
                stop = true;
            } else if (low == L"1" || low == L"status") {
                showHeader(); console(L"  [ STATUS ]"); console(L"");
                console(L"  events:          " + std::to_wstring(engine.eventCount()));
                console(L"  quarantined:     " + std::to_wstring(engine.quarantinedCount()));
                console(L"  alerts:          " + std::to_wstring(engine.alertCount()));
                console(L"  emergency mode:  " + std::wstring(engine.emergencyActive() ? L"YES" : L"no"));
                console(L"  folders watched: " + std::to_wstring(added));
                console(L"  sensitivity:     " + sensitivityName(sensitivity));
                console(L"  log file:        " + logPath());
                waitEnter();
            } else if (low == L"2" || low == L"quarantine" || low == L"restore") {
                quarantineMenu(q, &engine);
            } else if (low.rfind(L"restore ", 0) == 0) {
                showHeader(); console(L"  [ RESTORE ]"); console(L"");
                doRestoreByNumber(q, &engine, trimW(line.substr(8)));
                waitEnter();
            } else if (low == L"3" || low == L"help") {
                helpScreen();
            }
        } else {
            if (low == L"q" || low == L"quit" || low == L"exit") stop = true;
            else if (low == L"status") {
                console(L"  events:              " + std::to_wstring(engine.eventCount()));
                console(L"  quarantined:         " + std::to_wstring(engine.quarantinedCount()));
                console(L"  alerts:              " + std::to_wstring(engine.alertCount()));
                console(L"  emergency mode:      " + std::wstring(engine.emergencyActive() ? L"YES" : L"no"));
                console(L"  folders watched:     " + std::to_wstring(added));
                console(L"  sensitivity:         " + sensitivityName(sensitivity));
            } else if (low == L"quarantine") {
                auto items = q.list();
                if (items.empty()) console(L"  quarantine is empty");
                for (const auto& it : items) console(L"  - " + it);
            } else if (low == L"restore") {
                printRecordList(q);
                console(L"  to take a file back: restore <number>");
            } else if (low.rfind(L"restore ", 0) == 0) {
                doRestoreByNumber(q, &engine, trimW(line.substr(8)));
            } else if (low == L"help") {
                console(L""); printHelp();
            } else if (!low.empty()) {
                console(L"unknown command: try help");
            }
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
        auto dirs = effectiveWatchDirs(settings);
        console(L"  [ WATCH ]");
        console(L"");
        console(L"  Sensitivity: " + sensitivityName(settings.sensitivity));
        console(L"  Folders to protect (" + std::to_wstring(dirs.size()) + L"):");
        for (size_t i = 0; i < dirs.size(); ++i)
            console(L"    - " + dirs[i]);
        console(L"");
        console(L"  1. Start watching now");
        console(L"  2. Configure watched folders");
        console(L"  3. Change sensitivity");
        console(L"  0. Back");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line == L"0" || line.empty()) return;
        if (line == L"1") {
            runWatch(effectiveWatchDirs(settings), true, settings.sensitivity);
        } else if (line == L"2") {
            foldersMenu(settings);
        } else if (line == L"3") {
            showHeader(); console(L"  [ SENSITIVITY ]"); console(L"");
            console(L"  1. Low    - fewer false positives, slower to trigger");
            console(L"  2. Medium - balanced (default)");
            console(L"  3. High   - stricter/faster detection");
            console(L"  0. Back");
            console(L"");
            prompt(L"  > ");
            std::wstring c; if (!std::getline(std::wcin, c)) return;
            c = trimW(c);
            if (c == L"1") settings.sensitivity = 0;
            else if (c == L"2") settings.sensitivity = 1;
            else if (c == L"3") settings.sensitivity = 2;
            if (c == L"1" || c == L"2" || c == L"3") { saveSettings(settings); }
        }
    }
}

static int interactiveMenu() {
    Settings settings = loadSettings();
    while (true) {
        showHeader();
        console(L"  [ MAIN MENU ]");
        console(L"");
        console(L"  1. Watch / protect folders");
        console(L"  2. Quarantine (view / restore)");
        console(L"  3. History / stats");
        console(L"  4. Settings (folders, sensitivity)");
        console(L"  5. Update Aegis");
        console(L"  6. Autostart");
        console(L"  7. Help");
        console(L"  0. Exit");
        console(L"");
        prompt(L"  > ");
        std::wstring line; if (!std::getline(std::wcin, line)) break;
        line = trimW(line);
        for (auto& c : line) c = towlower(c);

        if (line == L"1" || line == L"watch") {
            watchMenu(settings);
        } else if (line == L"2" || line == L"quarantine") {
            Quarantine q(quarantineRoot());
            quarantineMenu(q, nullptr);
        } else if (line == L"3" || line == L"history" || line == L"stats") {
            historyMenu();
        } else if (line == L"4" || line == L"settings") {
            settingsMenu(settings);
        } else if (line == L"5" || line == L"update") {
            updateMenu();
        } else if (line == L"6" || line == L"autostart") {
            autostartMenu();
        } else if (line == L"7" || line == L"help") {
            helpScreen();
        } else if (line == L"0" || line == L"q" || line == L"exit") {
            clearScreen();
            banner();
            ok(L"Bye!");
            return 0;
        }
        settings = loadSettings();
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

    if (args.empty()) {
        return interactiveMenu();
    }
    if (args[0] == L"help" || args[0] == L"--help" || args[0] == L"/?") {
        banner();
        printHelp();
        return 0;
    }
    if (args[0] == L"watch") {
        banner();
        args.erase(args.begin());
        Settings s = loadSettings();
        if (args.empty()) return runWatch(effectiveWatchDirs(s), false, s.sensitivity);
        return runWatch(args, false, s.sensitivity);
    }
    if (args[0] == L"quarantine") { banner(); return runQuarantineList(); }
    if (args[0] == L"update") {
        banner();
        args.erase(args.begin());
        bool assumeYes = false;
        for (const auto& a : args) if (a == L"-y" || a == L"--yes") assumeYes = true;
        return runUpdate(assumeYes);
    }
    if (args[0] == L"autostart") {
        banner();
        args.erase(args.begin());
        if (args.empty()) { warn(L"usage: aegis autostart on|off"); return 1; }
        return runAutostart(args[0]);
    }
    if (args[0] == L"restore") {
        banner();
        args.erase(args.begin());
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
