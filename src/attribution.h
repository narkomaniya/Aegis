#pragma once
// attribution — "кто трогал файл": в момент срабатывания Engine обходим
// открытые файл-дескрипторы всех процессов и ищем владельца нашего файла.
//   Linux  : /proc/<pid>/fd/*  (надёжно, без прав суперпользователя)
//   Windows: таблица хэндлов через NtQuerySystemInformation
//   macOS  : SIP прячет чужие процессы — вернётся пусто (это ок)
#include <string>

namespace ae {

// "pid 1234 (codec.exe)" или пустая строка, если не удалось узнать
std::wstring fileOwner(const std::wstring& full);

} // namespace ae
