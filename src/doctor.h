#pragma once
// doctor.h — v6.0 «Doctor»: проверка целостности Aegis и Security Score.
#include <string>

namespace ae {

// Peчатает отчёт и возвращает 0 = всё хорошо, 1 = есть проблемы.
int doctorMenu();

// Компактная проверка для CLI (`aegis doctor`).
int runDoctor();

// Security Score 0..100 (для будущих UI/отчётов).
int securityScore();

// Сравнить текущий бинарь с ожидаемым SHA256 из release (best-effort).
bool verifyBinaryHash(std::wstring& detail);

} // namespace ae
