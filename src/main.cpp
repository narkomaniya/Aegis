// main.cpp — точка входа Aegis (CLI)
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
#include <windows.h>
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
    console(L"");
    console(L"    ___   ____   ____  ___ ____    _   ___   ___");
    console(L"   / _ \\ / ___| / ___|/ _ \\__  |  / \\ | _ \\ / _ \\");
    console(L"  | | | |\\___ \\| |  _| | | |/ /  / _ \\| |_)| | | |");
    console(L"  | |_| | ___) | |_| | |_| / /_ / ___ \\  _/| |_| |");
    console(L"   \\___/ |____/ \\____|\\___/____/_/   \\_\\_|  \\___/");
    console(L"  --- страж твоих файлов | v0.1 (windows) | c++17 ---");
    console(L"");
}

static void printHelp() {
    console(L"Команды:");
    console(L"  aegis watch [папка1 папка2 ...]  следить за папками");
    console(L"                                   (без аргументов: Десктоп, Документы, Загрузки)");
    console(L"  aegis quarantine                 показать содержимое карантина");
    console(L"  aegis help                       эта справка");
    console(L"");
    console(L"Команды во время наблюдения:");
    console(L"  status       статистика          quarantine   список карантина");
    console(L"  help         справка             q            выход");
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

static int runWatch(std::vector<std::wstring> dirs) {
    if (dirs.empty()) {  // стандартный набор: то, что чаще всего атакуют
        std::wstring home = homeDir();
        if (!home.empty()) {
            std::wstring sep =
#ifdef _WIN32
                L"\\";
#else
                L"/";
#endif
            dirs.push_back(home + sep + L"Desktop");
            dirs.push_back(home + sep + L"Documents");
            dirs.push_back(home + sep + L"Downloads");
        }
    }

    // Карантин и лог живут в ~/.aegis — сами за собой не следим
    const std::wstring qRoot = quarantineRoot();
    std::error_code ec;
    fs::create_directories(fs::path(qRoot).parent_path(), ec);

    Logger::instance().enableFile(fs::path(qRoot).parent_path() / L"aegis.log");

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

    console(L"Неизвестная команда: " + args[0]);
    console(L"");
    printHelp();
    return 1;
}
