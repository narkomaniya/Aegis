#pragma once
// watcher.h — наблюдение за папками через события ОС
//   Windows: ReadDirectoryChangesW | macOS: FSEvents | Linux: inotify
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

#ifdef __APPLE__
#include <mutex>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

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
    void loop(int index);  // Windows: один поток = одна папка, macOS/Linux: один на все

#ifdef __APPLE__
    // колбэк FSEvents (C-функция, читает eventFlags и превращает в FileEvent)
    static void fsEventsCallback(ConstFSEventStreamRef stream, void* info,
                                 size_t numEvents, void* eventPaths,
                                 const FSEventStreamEventFlags eventFlags[],
                                 const FSEventStreamEventId eventIds[]);

    std::mutex supMu_;                 // защищает runloop_/stream_ от доступа из stop()
    CFRunLoopRef runloop_ = nullptr;   // run loop потока наблюдения
    FSEventStreamRef stream_ = nullptr; // сам stream FSEvents
#endif

    EventCallback cb_;
    std::vector<std::wstring> dirs_;   // корневые папки наблюдения
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
};

} // namespace ae
