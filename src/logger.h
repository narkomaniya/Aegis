#pragma once
// logger.h — простой логгер Aegis: цветной вывод в консоль + файл
#include <string>
#include <mutex>

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
    void write(Level lvl, const std::wstring& msg);

private:
    std::mutex m_;
    std::wstring filepath_;
};

inline void info(const std::wstring& m)  { Logger::instance().write(Level::Info,  m); }
inline void ok(const std::wstring& m)    { Logger::instance().write(Level::Ok,    m); }
inline void warn(const std::wstring& m)  { Logger::instance().write(Level::Warn,  m); }
inline void alert(const std::wstring& m) { Logger::instance().write(Level::Alert, m); }

} // namespace ae
