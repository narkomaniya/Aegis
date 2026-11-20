#pragma once
// animals.h — скрытый «зоопарк»: животные с настоящей in-place анимацией.
// Модуль самодостаточен и не зависит от остального кода Aegis.
#include <string>
#include <vector>

namespace ae {

struct Animal {
    std::wstring id;      // внутренний ключ: cat, dog, fox, ...
    std::wstring name;    // имя для меню
    std::wstring hint;    // скрытая фраза-вызов
    std::wstring sound;   // финальная фраза
    std::vector<std::wstring> lines;   // 3 строки арта с плейсхолдерами {E} и {M}
    std::wstring inlineLine;           // однострочный запасной вариант
    std::vector<std::wstring> moves;   // 8 кадров «движущейся части»
};

const std::vector<Animal>& allAnimals();

// Ищет животное по id, имени, подсказке или алиасу. Возвращает nullptr, если ничего не найдено.
const Animal* findAnimal(const std::wstring& text);

// Скрытая команда, открывая меню зоопарка.
bool isZooCommand(const std::wstring& text);

// Крутит анимацию `seconds` секунд, затем оставляет последний кадр и звук.
void playAnimal(const Animal& animal, int seconds = 10);

// Интерактивное меню зоопарка (вызывается скрытой командой `zoo`).
void zooMenu();

} // namespace ae
