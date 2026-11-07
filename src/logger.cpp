// logger.cpp
#include "logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace ae {

#ifdef _WIN32
static const bool kIsTty = _isatty(_fileno(stdout)) != 0;
#endif

// убрать ANSI-цвета для небычного вывода (труба, файл)
static std::wstring stripColors(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == L'\x1b' && s[i + 1] == L'[') {
            // пропускаем \x1b[ ... m
            while (i < s.size() && s[i] != L'm') ++i;
            if (i < s.size()) ++i;
            continue;
        }
        out += s[i];
        ++i;
    }
    return out;
}

#ifdef _WIN32
static std::string toAnsi(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                            s.data(), n, nullptr, nullptr);
    return s;
}
#endif

// Единая точка вывода: консоль понимает русский+цвета, труба — обычные байты
static void emit(const std::wstring& line, bool newline) {
#ifdef _WIN32
    if (kIsTty) {
        std::wcout << line;
    } else {
        std::string bytes = toAnsi(stripColors(line));
        fwrite(bytes.data(), 1, bytes.size(), stdout);
    }
#else
    std::wcout << stripColors(line);
#endif
    if (newline) {
#ifdef _WIN32
        if (kIsTty) std::wcout << L"\n";
        else fputs("\n", stdout);
#else
        std::wcout << L"\n";
#endif
    }
    fflush(stdout);
}

void console(const std::wstring& msg) { emit(msg, true); }
void prompt(const std::wstring& msg)  { emit(msg, false); }

static std::wstring timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    wchar_t buf[32] = {};
    wcsftime(buf, 32, L"%H:%M:%S", &lt);
    return buf;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
#ifdef _WIN32
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
#else
    return std::string(w.begin(), w.end());
#endif
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::enableFile(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_);
    filepath_ = path;
}

void Logger::write(Level lvl, const std::wstring& msg) {
    const wchar_t* color = L"";
    const wchar_t* tag   = L"";
    switch (lvl) {
        case Level::Ok:    color = L"\x1b[32m"; tag = L" OK   "; break;
        case Level::Warn:  color = L"\x1b[33m"; tag = L" WARN "; break;
        case Level::Alert: color = L"\x1b[31m"; tag = L" ALERT"; break;
        case Level::Info:  color = L"\x1b[36m"; tag = L" INFO "; break;
    }
    emit(timestamp() + color + tag + L" \x1b[0m" + msg, true);

    if (!filepath_.empty()) {
        std::lock_guard<std::mutex> lock(m_);
        std::ofstream f(std::filesystem::path(filepath_), std::ios::app);
        if (f)
            f << toUtf8(timestamp()) << " [" << toUtf8(tag) << "] "
              << toUtf8(msg) << "\n";
    }
}

} // namespace ae
