#pragma once
// watcher.h — наблюдение за папками через события ОС (Windows: ReadDirectoryChangesW)
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

namespace ae {

enum class EventAction { Created, Modified, Deleted, Renamed };

struct FileEvent {
    std::wstring dir;      // папка, где произошло событие (корень наблюдения)
    std::wstring name;     // имя файла (для Renamed — новое имя)
    std::wstring oldName;  // старое имя (только для Renamed)
    EventAction action;
};

using EventCallback = std::function<void(const FileEvent&)>;

class Watcher {
public:
    explicit Watcher(EventCallback cb) : cb_(std::move(cb)) {}

    // Добавить папку под наблюдение (наблюдение рекурсивное).
    bool add(const std::wstring& dir);

    void start();  // запустить фоновые потоки
    void stop();   // остановить (потоки завершаются вместе с процессом)

    const std::vector<std::wstring>& dirs() const { return dirs_; }

private:
    void loop(int index);  // один поток = одна папка

    EventCallback cb_;
    std::vector<std::wstring> dirs_;   // корневые папки наблюдения
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
};

} // namespace ae
