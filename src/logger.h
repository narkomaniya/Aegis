#pragma once
// logger.h — простой логгер Aegis: цветной вывод в консоль + файл
#include <string>
#include <mutex>
#include <deque>
#include <vector>

namespace ae {

enum class Level { Info, Ok, Warn, Alert };

// Печать строки (с переводом строки). Умная: в консоли — широкий вывод,
// при перенаправлении — ANSI-байты без цветовых кодов.
void console(const std::wstring& msg);

// Печать без перевода строки (приглашение ко вводу `aegis> `)
void prompt(const std::wstring& msg);

class Logger {
public:
    static Logger& instance();

    void enableFile(const std::wstring& path);
    void setTheme(const std::wstring& theme); // default, white, black (mods/tweaks; in development)
    void write(Level lvl, const std::wstring& msg);

    // Recent alert/warn messages for UI (menu-safe viewing)
    std::vector<std::wstring> recentAlerts(size_t maxN = 20);
    void clearRecentAlerts();

private:
    std::mutex m_;
    std::wstring filepath_;
    std::wstring theme_ = L"default";
    std::deque<std::wstring> recent_;
};

inline void info(const std::wstring& m)  { Logger::instance().write(Level::Info,  m); }
inline void ok(const std::wstring& m)    { Logger::instance().write(Level::Ok,    m); }
inline void warn(const std::wstring& m)  { Logger::instance().write(Level::Warn,  m); }
inline void alert(const std::wstring& m) { Logger::instance().write(Level::Alert, m); }

} // namespace ae
