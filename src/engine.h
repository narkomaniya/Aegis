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
//   7. фразы выкупа в содержимом     -> YARA-минимум по первым 16 КиБ
//   8. белый список/исключения       -> пропуск доверенных путей/расширений
//   9. серия сильных срабатываний    -> быстрая эскалация (атака)
#include <deque>
#include <mutex>
#include <chrono>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <map>

#include "watcher.h"
#include "quarantine.h"

namespace ae {

struct IntegrityResult {
    std::vector<std::wstring> ok;
    std::vector<std::wstring> missing;
    std::vector<std::wstring> changed;
};

class Engine {
public:
    Engine(Quarantine& q, std::vector<std::wstring> protectedRoots)
        : q_(q), protectedRoots_(std::move(protectedRoots)) {}

    // Обработать одно событие (вызывается из потоков Watcher).
    void onEvent(const FileEvent& e);

    void setSensitivity(int s) { sensitivity_ = s; } // 0 low, 1 medium, 2 high
    void setUltraLight(bool v) { ultraLight_ = v; }
    void setBackupRoot(std::wstring r) { backupRoot_ = std::move(r); }
    void setExclusions(std::vector<std::wstring> paths, std::vector<std::wstring> exts);
    void setFolderSensitivities(std::vector<std::pair<std::wstring,int>> map);

    unsigned long long quarantinedCount() const { return quarantined_; }
    unsigned long long alertCount() const { return alerts_; }
    unsigned long long eventCount() const { return events_; }
    bool emergencyActive() const;

    // Не трогать этот путь (файл только что вернули из карантина через restore)
    void ignorePath(const std::wstring& full);

    // Расставить ловушки (canary-файлы) во все наблюдаемые папки.
    void seedCanaries();

    // Simple integrity check for canary files (snapshot/verify)
    IntegrityResult checkIntegrity() const;

private:
    double sampleEntropy(const std::wstring& full) const;   // Шеннон по первым 8 КиБ, -1 = нет данных
    bool insideQuarantine(const std::wstring& full) const;  // событие внутри самого карантина?
    bool isExcluded(const std::wstring& full, const std::wstring& name) const;
    int sensitivityFor(const std::wstring& full) const;
    void take(const FileEvent& e, const std::wstring& full, const std::wstring& reason, bool copyOnly);
    void noteRate(bool isRename = false);
    void checkRate();
    void checkRenames();
    void trimWorkingSet();

    Quarantine& q_;
    std::vector<std::wstring> protectedRoots_;

    mutable std::mutex m_;
    std::set<std::wstring> ignored_;   // пути после restore — не забирать обратно
    std::deque<std::chrono::steady_clock::time_point> rate_;
    std::deque<std::chrono::steady_clock::time_point> renames_;
    std::deque<std::chrono::steady_clock::time_point> hits_;
    unsigned long long events_ = 0;
    bool emergency_ = false;
    std::chrono::steady_clock::time_point emergencyUntil_{};
    unsigned long long quarantined_ = 0;
    unsigned long long alerts_ = 0;
    int sensitivity_ = 1; // 0 low, 1 medium, 2 high
    bool ultraLight_ = false;
    std::wstring backupRoot_;

    std::vector<std::wstring> excludePaths_;
    std::vector<std::wstring> excludeExts_;
    std::vector<std::pair<std::wstring,int>> folderSensitivity_;
};

} // namespace ae
