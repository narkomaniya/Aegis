// main.cpp — точка входа Aegis (CLI)
#ifdef _WIN32
// Требуется ДО любых windows.h: SHGetKnownFolderPath виден только с Vista+
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

#include "logger.h"
#include "watcher.h"
#include "engine.h"
#include "quarantine.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601  // SHGetKnownFolderPath (Vista+)
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <io.h>
#include <fcntl.h>
#else
#include <locale>
#include <codecvt>
#endif

namespace fs = std::filesystem;
using namespace ae;

#ifdef _WIN32
// командная строка Windows приходит в ANSI (CP1251)
static std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
#else
// Linux/macOS: аргументы приходят в UTF-8 — конвертируем в wide
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

// Локаль везде, а на Windows ещё и UTF-16-консоль
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
    console(L"    ___   ____   ____  ___ ____    _   ___   ___");
    console(L"   / _ \\ / ___| / ___|/ _ \\__  |  / \\ | _ \\ / _ \\");
    console(L"  | | | |\\___ \\| |  _| | | |/ /  / _ \\| |_)| | | |");
    console(L"  | |_| | ___) | |_| | |_| / /_ / ___ \\  _/| |_| |");
    console(L"   \\___/ |____/ \\____|\\___/____/_/   \\_\\_|  \\___/");
    console(std::wstring(L"  --- страж твоих файлов | v0.2 (") + platform +
            L") | c++17 ---");
    console(L"");
}

static void printHelp() {
    console(L"Команды:");
    console(L"  aegis watch [папка1 папка2 ...]  следить за папками");
    console(L"                                   (без аргументов: Десктоп, Документы, Загрузки)");
    console(L"  aegis quarantine                 показать содержимое карантина");
    console(L"  aegis restore                    показать записи карантина с номерами");
    console(L"  aegis restore <номер>            вернуть файл из карантина на место");
    console(L"  aegis help                       эта справка");
    console(L"");
    console(L"Команды во время наблюдения:");
    console(L"  status       статистика          quarantine   список карантина");
    console(L"  restore [n]  достать файл        help         справка");
    console(L"  q            выход");
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

// Демо-набор папок "по умолчанию" — через Known Folders API:
// правильно отдаёт Desktop/Documents/Downloads даже если их увёз OneDrive.
static void pushDefaultDir(std::vector<std::wstring>& dirs, int which) {
    // which: 0 = Desktop, 1 = Documents, 2 = Downloads
    std::wstring path;

#ifdef _WIN32
    static const KNOWNFOLDERID kIds[3] = {
        FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads };
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(kIds[which], 0, nullptr, &raw) == S_OK) {
        path = raw;
        CoTaskMemFree(raw);
    }
#endif

    if (path.empty()) {  // fallback: то, что было раньше (%HOME%/Desktop и т.п.)
        std::wstring home = homeDir();
        if (home.empty()) return;
        const wchar_t* subs[3] = { L"Desktop", L"Documents", L"Downloads" };
        std::wstring sep =
#ifdef _WIN32
            L"\\";
#else
            L"/";
#endif
        path = home + sep + subs[which];
    }
    dirs.push_back(path);
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

static std::wstring trimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static void printRecordList(const Quarantine& q) {
    auto recs = q.records();
    if (recs.empty()) {
        console(L"  карантин пуст");
        return;
    }
    int i = 1;
    for (const auto& r : recs) {
        console(L"  [" + std::to_wstring(i++) + L"] " + r.file);
        console(L"      откуда: " + r.from);
        console(L"      почему: " + r.reason);
    }
}

static int doRestoreByNumber(Quarantine& q, Engine* engine,
                             const std::wstring& numStr) {
    size_t num = 0;
    for (wchar_t c : numStr) {
        if (c < L'0' || c > L'9') {
            console(L"номер должен быть числом: " + numStr);
            return 1;
        }
        num = num * 10 + (size_t)(c - L'0');
    }
    auto recs = q.records();
    if (num == 0 || num > recs.size()) {
        console(L"нет такой записи: " + std::to_wstring(num) +
                L" (в карантине " + std::to_wstring(recs.size()) + L")");
        return 1;
    }
    Quarantine::Record rec = recs[num - 1];

    // вернуть файл в наблюдаемую папку = родить событие "Created" с опасным
    // расширением/энтропией — надо заранее сказать Engine не трогать этот путь
    if (engine) engine->ignorePath(rec.from);

    if (q.restore(rec)) {
        ok(L"вернул файл из карантина: " + rec.from);
        return 0;
    }
    warn(L"не удалось вернуть: " + rec.file);
    return 1;
}

static int runWatch(std::vector<std::wstring> dirs) {
    if (dirs.empty()) {  // стандартный набор: то, что чаще всего атакуют
        pushDefaultDir(dirs, 0);  // Desktop
        pushDefaultDir(dirs, 1);  // Documents
        pushDefaultDir(dirs, 2);  // Downloads
    }

    // Карантин и лог живут в ~/.aegis — сами за собой не следим
    const std::wstring qRoot = quarantineRoot();
    std::error_code ec;
    fs::create_directories(fs::path(qRoot).parent_path(), ec);

    Logger::instance().enableFile((fs::path(qRoot).parent_path() / L"aegis.log").wstring());

    Quarantine q(qRoot);
    Engine engine(q, dirs);
    Watcher watcher([&engine](const FileEvent& e) { engine.onEvent(e); });

    size_t added = 0;
    for (const auto& d : dirs) {
        if (watcher.add(d)) {
            ++added;
        } else {
            warn(L"папка не найдена, пропускаю: " + d);
        }
    }
    if (added == 0) {
        warn(L"нечего наблюдать — выхожу");
        return 1;
    }

    ok(L"Aegis на страже. Карантин: " + qRoot);
    info(L"команды: status | quarantine | help | q(выход)");
    console(L"");

    watcher.start();

    // Командный цикл в главном потоке
    std::wstring line;
    while (true) {
        prompt(L"aegis> ");
        if (!std::getline(std::wcin, line)) break;  // Ctrl+C / Ctrl+Z
        for (auto& c : line) c = towlower(c);

        if (line == L"q" || line == L"quit" || line == L"exit" || line == L"выход") {
            break;
        } else if (line == L"status") {
            console(L"  забрано в карантин: " + std::to_wstring(engine.quarantinedCount()));
            console(L"  тревог:             " + std::to_wstring(engine.alertCount()));
            console(L"  аварийный режим:    " +
                    std::wstring(engine.emergencyActive() ? L"ДА" : L"нет"));
            console(L"  папок под наблюдением: " + std::to_wstring(added));
        } else if (line == L"quarantine") {
            auto items = q.list();
            if (items.empty()) console(L"  карантин пуст");
            for (const auto& it : items) console(L"  - " + it);
        } else if (line == L"restore") {
            printRecordList(q);
            console(L"  чтобы достать файл: restore <номер>");
        } else if (line.rfind(L"restore ", 0) == 0) {
            doRestoreByNumber(q, &engine, trimW(line.substr(8)));
        } else if (line == L"help") {
            console(L"");
            printHelp();
        } else if (!line.empty()) {
            console(L"неизвестная команда: попробуй help");
        }
    }

    watcher.stop();
    ok(L"Aegis остановлен. До связи!");
    return 0;
}

static int runQuarantineList() {
    Quarantine q(quarantineRoot());
    auto items = q.list();
    if (items.empty()) {
        console(L"Карантин пуст.");
        return 0;
    }
    console(L"Карантин (" + std::to_wstring(items.size()) + L") в " +
            quarantineRoot() + L":");
    for (const auto& it : items) console(L"  - " + it);
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    setupConsole();
#endif
    banner();

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(toW(argv[i]));

    if (args.empty() || args[0] == L"help" || args[0] == L"--help" || args[0] == L"/?") {
        printHelp();
        return 0;
    }
    if (args[0] == L"watch") {
        args.erase(args.begin());
        return runWatch(args);
    }
    if (args[0] == L"quarantine") return runQuarantineList();
    if (args[0] == L"restore") {
        args.erase(args.begin());
        Quarantine q(quarantineRoot());
        if (args.empty()) {  // только список с номерами
            printRecordList(q);
            return 0;
        }
        return doRestoreByNumber(q, nullptr, args[0]);
    }

    console(L"Неизвестная команда: " + args[0]);
    console(L"");
    printHelp();
    return 1;
}
