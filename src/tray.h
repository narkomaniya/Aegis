#pragma once
// tray.h — системный трей Aegis v6.0.
// Windows: настоящая иконка в трее (Shell_NotifyIconW) с контекстным меню
//          и balloon-уведомлениями об аномалиях.
// POSIX:   лёгкий мост уведомлений (notify-send / osascript) + файл статуса
//          ~/.aegis/tray.status — без внешних GUI-зависимостей и новых lib.
#include <functional>
#include <memory>
#include <string>

namespace ae {

struct TrayCallbacks {
    std::function<void()> status;
    std::function<void()> quarantine;
    std::function<void()> toggleMax;
    std::function<void()> stop;
};

class Tray {
public:
    struct Impl; // publicly named so the platform helper functions can use it

    Tray();
    ~Tray();

    bool start(const TrayCallbacks& cb);
    void notify(const std::wstring& title, const std::wstring& message);
    void setMaxSecurity(bool on);
    void stop();
    bool running() const;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ae
