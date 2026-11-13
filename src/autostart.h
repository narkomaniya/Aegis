#pragma once
// autostart.h — autostart/background service helpers
#include <string>

namespace ae {

// on = включить автозапуск, off = выключить. Возврат: 0 = успех, 1 = ошибка.
int runAutostart(const std::wstring& mode);

// status: prints current autostart state (best effort). Returns 0.
int autostartStatus();

// service helper (best effort, user-level): install/uninstall/status/start/stop
int runServiceCommand(const std::wstring& action);

} // namespace ae
