#pragma once
// autostart.h — `aegis autostart on|off`: добавить/убрать Aegis в автозагрузку ОС.
#include <string>

namespace ae {

// on = включить автозапуск, off = выключить. Возврат: 0 = успех, 1 = ошибка.
int runAutostart(const std::wstring& mode);

} // namespace ae
