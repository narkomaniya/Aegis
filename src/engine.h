#pragma once
// engine.h — мозг Aegis: превращает события файловой системы в вердикты.
//
// Эвристики:
//   1. опасное расширение            -> высокая угроза -> ПЕРЕМЕСТИТЬ в карантин
//   2. высокая энтропия нового файла -> высокая угроза -> ПЕРЕМЕСТИТЬ в карантин
//   3. массовое изменение файлов     -> аварийный режим 30 с -> новые файлы КОПИРОВАТЬ
//   4. canary-ловушки                -> любое касание = тревога
//   5. всплеск переименований        -> ransomware-подозрение
//   6. записки "how to decrypt"      -> поставщик шифровальщика
#include <deque>
#include <mutex>
#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "watcher.h"
#include "quarantine.h"

namespace ae {

class Engine {
public:
    Engine(Quarantine& q, std::vector<std::wstring> protectedRoots)
        : q_(q), protectedRoots_(std::move(protectedRoots)) {}

    // Обработать одно событие (вызывается из потоков Watcher).
    void onEvent(const FileEvent& e);

    unsigned long long quarantinedCount() const { return quarantined_; }
    unsigned long long alertCount() const { return alerts_; }
    bool emergencyActive() const;

    // Не трогать этот путь (файл только что вернули из карантина через restore)
    void ignorePath(const std::wstring& full);

    // Расставить ловушки (canary-файлы) во все наблюдаемые папки.
    // Вызвать ДО начала наблюдения, чтобы не поймать собственное создание.
    void seedCanaries();

private:
    double sampleEntropy(const std::wstring& full) const;   // Шеннон по первым 8 КиБ, -1 = нет данных
    bool insideQuarantine(const std::wstring& full) const;  // событие внутри самого карантина?
    void take(const FileEvent& e, const std::wstring& full, const std::wstring& reason, bool copyOnly);
    void noteRate(bool isRename = false);  // накапливает скорость (или переименований)
    void checkRate();                      // аварийный режим по скорости
    void checkRenames();                   // аварийный режим по всплеску rename
    void trimWorkingSet();                 // Windows: выгрузить страницы, держим RSS малым

    Quarantine& q_;
    std::vector<std::wstring> protectedRoots_;

    mutable std::mutex m_;
    std::set<std::wstring> ignored_;   // пути после restore — не забирать обратно
    std::deque<std::chrono::steady_clock::time_point> rate_;     // все события за 1 с
    std::deque<std::chrono::steady_clock::time_point> renames_;  // только rename за 1 с
    unsigned long long events_ = 0;    // счётчик для периодического trimWorkingSet
    bool emergency_ = false;
    std::chrono::steady_clock::time_point emergencyUntil_{};
    unsigned long long quarantined_ = 0;
    unsigned long long alerts_ = 0;
};

} // namespace ae
