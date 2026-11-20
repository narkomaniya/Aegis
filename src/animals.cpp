// animals.cpp — скрытый зоопарк Aegis.
// Анимация перерисовывается на одном месте: Win32 SetConsoleCursorPosition +
// WriteConsoleW на Windows, ANSI-курсор на POSIX, `\r` + однострочный кадр
// в запасном режиме (перенаправленный вывод, IDE, старый терминал).
#include "animals.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ae {
namespace {

std::wstring lowerText(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = wchar_t(c + 32);
        else if (c >= L'\u0410' && c <= L'\u042F') c = wchar_t(c + 32); // А-Я
        else if (c == L'\u0401') c = L'\u0451';                         // Ё -> ё
    }
    return s;
}

std::wstring replaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

const std::vector<std::wstring>& eyesFrames() {
    static const std::vector<std::wstring> k = {
        L"o.o", L"o.o", L"o.o", L"-.-", L"o.o", L"o.o", L"^.^", L"o.o"
    };
    return k;
}

std::wstring renderInline(const Animal& a, size_t frame) {
    std::wstring s = a.inlineLine;
    s = replaceAll(s, L"{E}", eyesFrames()[frame % eyesFrames().size()]);
    s = replaceAll(s, L"{M}", a.moves[frame % a.moves.size()]);
    return s;
}

std::vector<std::wstring> renderFrame(const Animal& a, size_t frame) {
    const std::wstring e = eyesFrames()[frame % eyesFrames().size()];
    const std::wstring m = a.moves[frame % a.moves.size()];
    std::vector<std::wstring> out;
    out.reserve(a.lines.size());
    for (const std::wstring& line : a.lines) {
        std::wstring s = replaceAll(line, L"{E}", e);
        s = replaceAll(s, L"{M}", m);
        out.push_back(s);
    }
    return out;
}

size_t frameWidth(const Animal& a) {
    size_t w = renderInline(a, 0).size();
    for (size_t i = 0; i < a.moves.size(); ++i) {
        for (const std::wstring& line : renderFrame(a, i)) w = std::max(w, line.size());
        w = std::max(w, renderInline(a, i).size());
    }
    return w;
}

std::wstring pad(std::wstring s, size_t width) {
    if (s.size() < width) s.append(width - s.size(), L' ');
    return s;
}

std::wstring joinLines(const std::vector<std::wstring>& lines) {
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += L"\r\n";
        out += lines[i];
    }
    return out;
}

} // namespace

const std::vector<Animal>& allAnimals() {
    static const std::vector<Animal> k = {
        {
            L"cat", L"Cat", L"pspsps", L"...mrrp.",
            { L"  /\\_/\\", L" ( {E} )", L"  > ^ < {M}" },
            L"  /\\_/\\  ( {E} )  > ^ < {M}",
            { L"~~~", L"~~/", L"~//", L"/| ", L"|  ", L"\\  ", L"~\\\\", L"~~~" },
        },
        {
            L"dog", L"Dog", L"good boy", L"...woof!",
            { L"  /\\_/\\", L" ( {E} ) {M}", L"  > ^ <" },
            L"  /\\_/\\  ( {E} ) {M}  > ^ <",
            { L"~~~", L"~~/", L"~//", L"//~", L"/~~", L"\\~~", L"~\\\\", L"~~~" },
        },
        {
            L"fox", L"Fox", L"chirp chirp", L"...chirp!",
            { L"  /\\_/\\", L" ( {E} )  {M}", L"  > v <" },
            L"  /\\_/\\  ( {E} )  > v < {M}",
            { L"~~~", L"~~/", L"~//", L"///", L"\\\\\\", L"~\\\\", L"~~\\", L"~~~" },
        },
        {
            L"owl", L"Owl", L"who goes there", L"...hoot.",
            { L" {M}  ( {E} )", L"  /\\_/\\", L"  > ^ <" },
            L"{M} ( {E} )  /\\_/\\  > ^ <",
            { L"/\\", L"/ ", L"| ", L"\\ ", L"\\/", L" \\", L"  ", L"/\\" },
        },
        {
            L"penguin", L"Penguin", L"honk", L"...honk!",
            { L"   {M}", L"  ( {E} )", L"  (> ^ <)" },
            L"{M} ( {E} )  (> ^ <)",
            { L"\\ /", L"\\ |", L"\\| ", L" | ", L" |/", L"|/ ", L"/  ", L"\\ /" },
        },
        {
            L"dragon", L"Dragon", L"fsss", L"...rawr!",
            { L" {M} ( {E} )", L"  /\\_/\\", L"  > ^ <" },
            L"{M} ( {E} )  /\\_/\\  > ^ <",
            { L"/\\ ", L"/\\~", L"\\/~", L"\\/!", L"!!\\", L"~!\\", L"~\\\\", L"/\\ " },
        },
    };
    return k;
}

const Animal* findAnimal(const std::wstring& text) {
    const std::wstring t = lowerText(text);
    const auto& list = allAnimals();
    for (const Animal& a : list) {
        if (t == lowerText(a.id) || t == lowerText(a.name) || t == lowerText(a.hint))
            return &a;
        // первое слово подсказки тоже считается алиасом ("good boy" -> "good")
        size_t sp = lowerText(a.hint).find(L' ');
        if (sp != std::wstring::npos && t == lowerText(a.hint).substr(0, sp)) return &a;
    }
    if (t == L"kitten" || t == L"kitty" || t == L"meow" || t == L"мяу" ||
        t == L"зызызы" || t == L"pspsps" || t == L"pspsps!" || t == L"psps" ||
        t == L"p-s-p-s" || t == L"кот" || t == L"котик" || t == L"котенок" ||
        t == L"котёнок") return &list[0];
    if (t == L"puppy" || t == L"woof" || t == L"гав" || t == L"пёс" ||
        t == L"пес" || t == L"собака" || t == L"щенок") return &list[1];
    if (t == L"лиса" || t == L"лисенок" || t == L"лисёнок" || t == L"лисонька") return &list[2];
    if (t == L"hoot" || t == L"совка" || t == L"сова") return &list[3];
    if (t == L"пингвин" || t == L"пингвинка") return &list[4];
    if (t == L"rawr" || t == L"дракон" || t == L"дракончик") return &list[5];
    return nullptr;
}

bool isZooCommand(const std::wstring& text) {
    const std::wstring t = lowerText(text);
    return t == L"zoo" || t == L"animals" || t == L"menagerie" ||
           t == L"зоопарк" || t == L"животные";
}

void playAnimal(const Animal& animal, int seconds) {
    std::wcout.clear();
    // Aegis prints banners/menus through std::wcout. Sync it before writing
    // straight to the console handle: otherwise buffered text can be flushed
    // *after* the animation and appear above it (looks like bottom-to-top).
    std::wcout.flush();
    std::fflush(stdout);

    const size_t width = frameWidth(animal);
    const size_t nlines = animal.lines.empty() ? 1 : animal.lines.size();

#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    const bool canMove = hOut != INVALID_HANDLE_VALUE &&
                         GetConsoleScreenBufferInfo(hOut, &csbi) != 0;
    COORD home{0, 0};
    if (canMove) {
        // Reserve room below the art. Without this, the last frames scroll
        // the console and the absolute `home` row drifts upwards — exactly
        // the "frames are printed one above another" bug.
        const SHORT need = static_cast<SHORT>(nlines - 1);
        if (csbi.dwCursorPosition.Y >= need) {
            home = COORD{0, static_cast<SHORT>(csbi.dwCursorPosition.Y - need)};
            const std::wstring gap(need, L'\n');
            DWORD w = 0;
            WriteConsoleW(hOut, gap.c_str(), (DWORD)gap.size(), &w, nullptr);
        } else {
            home = COORD{0, 0};
        }
    }
    // ConPTY / redirected console: no Win32 cursor API, but ANSI still works.
    const bool ansi = !canMove && _isatty(_fileno(stdout)) != 0;
#else
    const bool canMove = isatty(1) != 0;
    const bool ansi = false;
#endif

    const auto finishAt = std::chrono::steady_clock::now() +
                          std::chrono::seconds(seconds);
    size_t frame = 0;
    while (std::chrono::steady_clock::now() < finishAt) {
#ifdef _WIN32
        if (canMove) {
            std::vector<std::wstring> f = renderFrame(animal, frame);
            for (std::wstring& line : f) line = pad(line, width);
            const std::wstring s = joinLines(f);
            SetConsoleCursorPosition(hOut, home);
            DWORD written = 0;
            WriteConsoleW(hOut, s.c_str(), (DWORD)s.size(), &written, nullptr);
        } else if (ansi) {
            if (frame) std::wcout << L"\033[1A\033[2K\r";
            std::wcout << pad(renderInline(animal, frame), width);
            std::wcout.flush();
        } else {
            std::wcout << L'\r' << pad(renderInline(animal, frame), width);
            std::wcout.flush();
        }
#else
        if (canMove) {
            if (frame) std::wcout << L"\033[" << (nlines - 1) << L"A\r";
            std::vector<std::wstring> f = renderFrame(animal, frame);
            for (std::wstring& line : f) line = pad(line, width);
            std::wcout << joinLines(f);
        } else {
            std::wcout << L'\r' << pad(renderInline(animal, frame), width);
        }
        std::wcout.flush();
#endif
        ++frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }

    // Оставляем зверя на экране и дописываем его фразу.
    std::vector<std::wstring> last = renderFrame(animal, 0);
    for (std::wstring& line : last) line = pad(line, width);
    const std::wstring finalArt = joinLines(last);
    const std::wstring ending = L"\r\n   " + animal.sound + L"\n";
#ifdef _WIN32
    if (canMove) {
        SetConsoleCursorPosition(hOut, home);
        const std::wstring s = finalArt + ending;
        DWORD written = 0;
        WriteConsoleW(hOut, s.c_str(), (DWORD)s.size(), &written, nullptr);
    } else if (ansi) {
        if (frame) std::wcout << L"\033[1A\033[2K\r";
        std::wcout << pad(renderInline(animal, 0), width) << ending;
        std::wcout.flush();
    } else {
        std::wcout << L'\r' << pad(renderInline(animal, 0), width) << ending;
        std::wcout.flush();
    }
#else
    if (canMove) std::wcout << L"\033[" << (nlines - 1) << L"A\r";
    else std::wcout << L'\r';
    std::wcout << finalArt << ending;
    std::wcout.flush();
#endif
}

void zooMenu() {
    std::wcout.clear();
    const auto& list = allAnimals();
    for (;;) {
        std::wcout << L"\n  ==========  S E C R E T   Z O O  ==========\n";
        for (size_t i = 0; i < list.size(); ++i)
            std::wcout << L"    " << (i + 1) << L".  " << list[i].name
                       << L"   (" << list[i].hint << L")\n";
        std::wcout << L"    0.  leave the zoo\n  > ";
        std::wcout.flush();

        std::wstring line;
        if (!std::getline(std::wcin, line)) break;
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\r')) line.pop_back();
        if (line.empty()) continue;
        if (line == L"0" || lowerText(line) == L"q" || lowerText(line) == L"exit" ||
            lowerText(line) == L"back") break;

        const Animal* chosen = nullptr;
        if (line.size() == 1 && line[0] >= L'1' && line[0] <= L'9') {
            const size_t idx = size_t(line[0] - L'1');
            if (idx < list.size()) chosen = &list[idx];
        }
        if (!chosen) chosen = findAnimal(line);
        if (chosen) playAnimal(*chosen, 10);
        else std::wcout << L"  nobody answered... try 1-" << list.size() << L"\n";
    }
}

} // namespace ae
