// watcher.cpp — реализация наблюдателя:
//   Windows : ReadDirectoryChangesW (один поток на папку)
//   macOS   : FSEvents (один поток на все папки, рекурсивно)
//   Linux   : inotify (один поток на все папки, рекурсивный проход)
#include "watcher.h"
#include "logger.h"

#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <cstring>
#include <locale>
#include <codecvt>
#include <thread>
#else
#include <sys/inotify.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <locale>
#include <codecvt>
#endif

namespace fs = std::filesystem;

namespace ae {

bool Watcher::add(const std::wstring& dir) {
    std::error_code ec;
    fs::path p(dir);
    if (!fs::exists(p, ec) || ec || !fs::is_directory(p, ec) || ec) return false;
    dirs_.push_back(fs::absolute(p, ec).wstring());
    return !ec;
}

void Watcher::start() {
    loopDone_ = false;
#ifdef _WIN32
    // Create the stop event BEFORE publishing running_=true: an early stop()
    // must never observe a null event with running_ already set.
    {
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        stopEvent_ = ev;
    }
#endif
    running_ = true;
#ifdef _WIN32
    for (int i = 0; i < (int)dirs_.size(); ++i)
        threads_.emplace_back(&Watcher::loop, this, i);
#else
    // macOS/Linux: единый поток на все добавленные папки
#if !defined(__APPLE__)
    {
        std::lock_guard<std::mutex> lock(wakeMu_);
        if (wakeFd_[0] < 0 && pipe(wakeFd_) == 0) {
            for (int i = 0; i < 2; ++i) {
                int fl = fcntl(wakeFd_[i], F_GETFL, 0);
                if (fl >= 0) fcntl(wakeFd_[i], F_SETFL, fl | O_NONBLOCK);
            }
        }
    }
#endif
    if (!dirs_.empty())
        threads_.emplace_back(&Watcher::loop, this, 0);
    else
        loopDone_ = true;
#endif
}

void Watcher::stop() {
    running_ = false;
#ifdef _WIN32
    if (stopEvent_) SetEvent(static_cast<HANDLE>(stopEvent_));
#elif defined(__APPLE__)
    // CFRunLoopStop потокобезопасен, но если вызвать его до CFRunLoopRun(),
    // остановка теряется. Повторяем до тех пор, пока поток реально не вышел.
    for (int i = 0; i < 500 && !loopDone_.load(); ++i) {
        CFRunLoopRef rl = nullptr;
        {
            std::lock_guard<std::mutex> lock(supMu_);
            rl = runloop_;
        }
        if (rl) CFRunLoopStop(rl);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    {
        std::lock_guard<std::mutex> lock(wakeMu_);
        if (wakeFd_[1] >= 0) {
            char b = 1;
            ssize_t ignored = write(wakeFd_[1], &b, 1);
            (void)ignored;
        }
    }
#endif
    // Дожидаемся потоков: они больше не вызовут cb_ после возврата из stop().
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
#ifdef _WIN32
    if (stopEvent_) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
#elif !defined(__APPLE__)
    {
        std::lock_guard<std::mutex> lock(wakeMu_);
        for (int i = 0; i < 2; ++i) {
            if (wakeFd_[i] >= 0) close(wakeFd_[i]);
            wakeFd_[i] = -1;
        }
    }
#endif
}

#ifdef _WIN32
// ============================================================
//                        WINDOWS
// ============================================================
void Watcher::loop(int index) {
    const std::wstring dir = dirs_[index];

    HANDLE h = CreateFileW(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,  // папка + асинхронный I/O
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        ae::warn(L"cannot watch: " + dir + L" (code " +
                 std::to_wstring(GetLastError()) + L")");
        return;
    }

    ae::info(L"watching: " + dir);

    constexpr DWORD BUF = 64 * 1024;
    std::vector<BYTE> buf(BUF);
    // v6.0: a queue, not a single string — OLD/NEW/OLD/NEW in one buffer
    // used to lose the first pair and mislabel the second rename.
    std::deque<std::wstring> pendingOld;

    HANDLE stopEv = static_cast<HANDLE>(stopEvent_);

    while (running_) {
        // Асинхронное чтение + ожидание на двух объектах: так stop() может
        // разбудить поток, а не оставлять его навсегда в блокирующем вызове.
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) break;

        BOOL started = ReadDirectoryChangesW(
            h, buf.data(), BUF,
            TRUE,  // рекурсивно, включая подпапки
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE,
            nullptr, &ov, nullptr);

        if (!started) {
            DWORD err = GetLastError();
            // ERROR_IO_PENDING — нормальный асинхронный запуск, не ошибка.
            if (err == ERROR_IO_PENDING) started = TRUE;
            else {
                CloseHandle(ov.hEvent);
                if (!running_ || err == ERROR_OPERATION_ABORTED) break;
                ae::warn(L"watch failed: " + dir + L" (code " + std::to_wstring(err) +
                         L"), retrying");
                Sleep(500);
                continue;
            }
        }

        HANDLE waits[2] = {ov.hEvent, stopEv};
        DWORD wr = WaitForMultipleObjects(stopEv ? 2 : 1, waits, FALSE, INFINITE);

        if (wr == WAIT_OBJECT_0 + 1) {
            // stop(): отменяем I/O и ждём, пока буфер перестанет использоваться
            CancelIoEx(h, &ov);
            WaitForSingleObject(ov.hEvent, 5000);
            CloseHandle(ov.hEvent);
            break;
        }

        DWORD bytes = 0;
        BOOL got = GetOverlappedResult(h, &ov, &bytes, FALSE);
        DWORD gerr = got ? 0 : GetLastError();
        CloseHandle(ov.hEvent);

        if (!got) {
            if (!running_ || gerr == ERROR_OPERATION_ABORTED) break;
            ae::warn(L"watch read failed: " + dir + L" (code " +
                     std::to_wstring(gerr) + L"), retrying");
            Sleep(500);
            continue;
        }

        // bytes == 0: буфер переполнен, часть событий ОС потеряла — старые
        // данные в buf разбирать нельзя.
        if (bytes == 0) {
            // Buffer overflow: the OS dropped events. Never parse stale bytes —
            // warn loudly instead of silently losing protection events.
            ae::warn(L"watch buffer overflow on " + dir + L" — some events were dropped");
            continue;
        }

        BYTE* p = buf.data();
        BYTE* end = p + bytes;

        while (p + sizeof(FILE_NOTIFY_INFORMATION) <= end) {
            auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            BYTE* nameBegin = reinterpret_cast<BYTE*>(fni->FileName);
            if (nameBegin > end || fni->FileNameLength > (size_t)(end - nameBegin))
                break;   // повреждённая/устаревшая запись

            std::wstring name(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

            FileEvent ev;
            ev.dir = dir;
            ev.action = EventAction::Modified;   // поле всегда определено, даже для неизвестного Action
            bool dispatch = true;
            switch (fni->Action) {
                case FILE_ACTION_ADDED:
                    ev.action = EventAction::Created; ev.name = name; break;
                case FILE_ACTION_MODIFIED:
                    ev.action = EventAction::Modified; ev.name = name; break;
                case FILE_ACTION_REMOVED:
                    ev.action = EventAction::Deleted; ev.name = name; break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    pendingOld.push_back(name); dispatch = false; break; // pair: old name awaits new
                case FILE_ACTION_RENAMED_NEW_NAME:
                    ev.action = EventAction::Renamed;
                    ev.name = name;
                    if (!pendingOld.empty()) {
                        ev.oldName = pendingOld.front();
                        pendingOld.pop_front();
                    }
                    break;
                default:
                    dispatch = false;  // неизвестное действие — не отдаём неинициализированное событие
                    break;
            }

            if (dispatch) {
                if (!running_) break;
                cb_(ev);
            }

            if (fni->NextEntryOffset == 0) break;
            if (fni->NextEntryOffset > (size_t)(end - p)) break;
            p += fni->NextEntryOffset;
        }
    }
    CloseHandle(h);
}

#elif defined(__APPLE__)
// ============================================================
//                        macOS (FSEvents)
// ============================================================

static std::string toUtf8(const std::wstring& w) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.to_bytes(w);
    } catch (...) {
        return std::string(w.begin(), w.end());
    }
}

static std::wstring fromUtf8(const std::string& s) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
}

void Watcher::fsEventsCallback(ConstFSEventStreamRef /*stream*/,
                               void* info,
                               size_t numEvents,
                               void* eventPaths,
                               const FSEventStreamEventFlags eventFlags[],
                               const FSEventStreamEventId /*eventIds*/[]) {
    auto* self  = static_cast<Watcher*>(info);
    auto* paths = static_cast<CFArrayRef>(eventPaths);

    for (size_t i = 0; i < numEvents; ++i) {
        const FSEventStreamEventFlags flags = eventFlags[i];
        // Без флагов файла — служебное событие самого корня наблюдения,
        // пропускаем (FSEvents присылает и такие).
        if (!(flags & (kFSEventStreamEventFlagItemCreated |
                       kFSEventStreamEventFlagItemModified |
                       kFSEventStreamEventFlagItemRenamed  |
                       kFSEventStreamEventFlagItemRemoved)))
            continue;

        CFStringRef cf = static_cast<CFStringRef>(
            CFArrayGetValueAtIndex(paths, (CFIndex)i));
        CFIndex maxLen =
            CFStringGetMaximumSizeOfFileSystemRepresentation(cf);
        std::string utf8((size_t)maxLen + 1, '\0');
        if (!CFStringGetFileSystemRepresentation(cf, utf8.data(), utf8.size()))
            continue;
        // буфер больше реальной длины: обрезаем хвост из NUL-байтов,
        // иначе к имени файла прилипают '\0' и все сравнения расширений ломаются
        utf8.resize(std::strlen(utf8.c_str()));

        fs::path full(fromUtf8(utf8));

        FileEvent ev;
        ev.dir  = full.parent_path().wstring();
        ev.name = full.filename().wstring();

        if      (flags & kFSEventStreamEventFlagItemRemoved) ev.action = EventAction::Deleted;
        else if (flags & kFSEventStreamEventFlagItemRenamed) ev.action = EventAction::Renamed;
        else if (flags & kFSEventStreamEventFlagItemCreated) ev.action = EventAction::Created;
        else                                                ev.action = EventAction::Modified;

        if (fs::is_directory(full)) {
            // FSEvents наблюдает рекурсивно сам — отдельные watch'и не нужны
            continue;
        }

        if (!self->running_) return;
        self->cb_(ev);
    }
}

void Watcher::loop(int /*index*/) {
    // Гарантированно помечаем выход из потока, даже при раннем return.
    struct DoneGuard { std::atomic<bool>& f; ~DoneGuard() { f.store(true); } } doneGuard{loopDone_};
    if (dirs_.empty()) return;

    CFMutableArrayRef paths = CFArrayCreateMutable(
        kCFAllocatorDefault, (CFIndex)dirs_.size(), &kCFTypeArrayCallBacks);
    if (!paths) {
        ae::warn(L"FSEvents: failed to create the folder list");
        return;
    }
    for (const auto& d : dirs_) {
        CFStringRef cf = CFStringCreateWithFileSystemRepresentation(
            kCFAllocatorDefault, toUtf8(d).c_str());
        if (cf) { CFArrayAppendValue(paths, cf); CFRelease(cf); }
    }

    FSEventStreamContext ctx{};
    ctx.info = this;

    FSEventStreamRef stream = FSEventStreamCreate(
        kCFAllocatorDefault, &Watcher::fsEventsCallback, &ctx, paths,
        kFSEventStreamEventIdSinceNow,
        /*latency*/ 0.05,   // почти живое время, без дожидания "окна" (0.3 с)
        kFSEventStreamCreateFlagFileEvents | // даёт флаги ItemCreated/Modified/…
        kFSEventStreamCreateFlagNoDefer |    // события сразу, а не на следующем после "долгого" события
        kFSEventStreamCreateFlagWatchRoot |  // рекурсивно + ловить переименование корня
        kFSEventStreamCreateFlagUseCFTypes); // eventPaths — CFArray, а не char**

    if (!stream) {
        ae::warn(L"FSEvents: FSEventStreamCreate failed");
        CFRelease(paths);
        return;
    }

    // Запускаем свой run loop: FSEvents доставляет события только в поток,
    // в котором stream запланирован. Всё в одном потоке — безопасно.
    CFRunLoopRef rl = CFRunLoopGetCurrent();
    {
        std::lock_guard<std::mutex> lock(supMu_);
        runloop_ = rl;
        stream_  = stream;
    }
    FSEventStreamScheduleWithRunLoop(stream, rl, kCFRunLoopDefaultMode);

    if (!FSEventStreamStart(stream)) {
        ae::warn(L"FSEvents: cannot watch");
        FSEventStreamRelease(stream);
        std::lock_guard<std::mutex> lock(supMu_);
        runloop_ = nullptr;  stream_ = nullptr;
        CFRelease(paths);
        return;
    }

    for (const auto& d : dirs_)
        ae::info(L"watching (FSEvents): " + d);

    if (!running_) {
        // stop() успел раньше, чем мы вошли в цикл
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        {
            std::lock_guard<std::mutex> lock(supMu_);
            runloop_ = nullptr;  stream_ = nullptr;
        }
        CFRelease(paths);
        return;
    }

    CFRunLoopRun();  // блокируемся, пока stop() не сделает CFRunLoopStop

    FSEventStreamStop(stream);
    FSEventStreamInvalidate(stream);
    FSEventStreamRelease(stream);
    {
        std::lock_guard<std::mutex> lock(supMu_);
        runloop_ = nullptr;  stream_ = nullptr;
    }
    CFRelease(paths);
}

#else  // ====================== POSIX (Linux) ======================

// Имена из inotify — байты UTF-8. Приводим к настоящим wchar, как main.cpp::toW(),
// иначе путь события не совпадёт с путём из настроек/карантина.
static std::wstring utf8ToWide(const std::string& s) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
}

// Рекурсивно добавить watch на папку и все её подпапки
static void addRecursive(int fd, std::map<int, std::wstring>& wd2dir,
                         const std::wstring& dir) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(fs::path(dir), ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::error_code isDirEc;
        if (!it->is_directory(isDirEc) || isDirEc) {
            // следим только за папками; файловый watch не нужен
            it.disable_recursion_pending();
            continue;
        }
        std::string np = it->path().string();
        int wd = inotify_add_watch(fd, np.c_str(),
                                   IN_CREATE | IN_MODIFY |
                                   IN_MOVED_FROM | IN_MOVED_TO |
                                   IN_DELETE | IN_CLOSE_WRITE);
        if (wd >= 0) wd2dir[wd] = it->path().wstring();
        else ae::warn(L"cannot watch subfolder " + it->path().wstring() +
                      L" (inotify_add_watch failed; check max_user_watches)");
    }
    // и сам корень тоже
    int wd = inotify_add_watch(fd, fs::path(dir).string().c_str(),
                               IN_CREATE | IN_MODIFY |
                               IN_MOVED_FROM | IN_MOVED_TO |
                               IN_DELETE | IN_CLOSE_WRITE);
    if (wd >= 0) wd2dir[wd] = dir;
    else ae::warn(L"cannot watch " + fs::path(dir).wstring() +
                  L" (inotify_add_watch failed; check max_user_watches)");
}

void Watcher::loop(int index) {
    (void)index;
    int fd = inotify_init1(0 /* blocking mode */);
    if (fd < 0) {
        ae::warn(L"inotify_init failed");
        return;
    }

    std::map<int, std::wstring> wd2dir;
    for (const auto& d : dirs_) {
        addRecursive(fd, wd2dir, d);
        ae::info(L"watching: " + d);
    }

    // подставки под событие MOVED_FROM -> MOVED_TO (как RENAMED_OLD/NEW)
    // cookie -> (событие, время прихода): пары может не быть (файл уехал
    // за пределы наблюдаемого дерева), тогда по таймауту отдаём Deleted.
    struct PendingMove {
        FileEvent ev;
        std::chrono::steady_clock::time_point at;
    };
    std::map<unsigned, PendingMove> pending;

    constexpr size_t BUF = 64 * 1024;
    std::vector<char> buf(BUF);

    auto emit = [&](const FileEvent& ev) {
        if (running_) cb_(ev);
    };

    auto flushStaleMoves = [&]() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = pending.begin(); it != pending.end();) {
            if (now - it->second.at < std::chrono::seconds(2)) { ++it; continue; }
            FileEvent old = it->second.ev;
            old.action = EventAction::Deleted;   // файл ушёл из наблюдаемого дерева
            old.name = old.oldName;
            old.oldName.clear();
            emit(old);
            it = pending.erase(it);
        }
    };

    int wakeFd = -1;
    {
        std::lock_guard<std::mutex> lock(wakeMu_);
        wakeFd = wakeFd_[0];
    }

    while (running_) {
        struct pollfd fds[2];
        fds[0].fd = fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = wakeFd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int pr = poll(fds, wakeFd >= 0 ? 2 : 1, 500);
        if (pr < 0) {
            if (errno == EINTR) continue;
            ae::warn(L"inotify poll failed");
            break;
        }
        if (wakeFd >= 0 && (fds[1].revents & POLLIN)) break;   // stop()
        if (pr == 0) { flushStaleMoves(); continue; }          // таймаут: чистим зависшие MOVED_FROM
        if (!(fds[0].revents & POLLIN)) continue;

        ssize_t len = read(fd, buf.data(), BUF);
        if (len < 0) {
            if (errno == EINTR) continue;
            ae::warn(L"inotify read failed");
            break;
        }
        if (len == 0) continue;

        char* p = buf.data();
        char* end = p + len;
        while (p + sizeof(inotify_event) <= end) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            if (ev->len > (size_t)(end - p) - sizeof(inotify_event)) break;
            std::string name(ev->len ? ev->name : "");
            auto itDir = wd2dir.find(ev->wd);
            std::wstring dir = (itDir != wd2dir.end()) ? itDir->second : L"";
            std::wstring wname = utf8ToWide(name);

            FileEvent fe;
            fe.dir = dir;
            fe.name = wname;

            if (ev->mask & IN_IGNORED) {
                // watch снялся (папка удалена) — пробуем вернуть, если папка жива
                wd2dir.erase(ev->wd);
                std::error_code ec;
                if (!dir.empty() && fs::exists(fs::path(dir), ec))
                    addRecursive(fd, wd2dir, dir);
            }
            else if (ev->mask & IN_MOVED_FROM) {
                fe.action = EventAction::Renamed;
                fe.oldName = wname;          // старое имя известно, новое пришлёт MOVED_TO
                pending[ev->cookie] = PendingMove{fe, std::chrono::steady_clock::now()};
            }
            else if (ev->mask & IN_MOVED_TO) {
                auto pit = pending.find(ev->cookie);
                fe.action = EventAction::Renamed;
                bool movedDir = false;
                std::error_code ec;
                fs::path newPath = fs::path(dir) / fs::path(wname);
                const bool isDir = fs::is_directory(newPath, ec) && !ec;
                if (pit != pending.end()) {
                    fe.oldName = pit->second.ev.oldName;
                    if (isDir && !fe.oldName.empty()) {
                        // дескрипторы inotify остаются теми же — меняем только путь в карте
                        std::wstring oldFull = (fs::path(pit->second.ev.dir) / fs::path(fe.oldName)).wstring();
                        std::wstring newFull = newPath.wstring();
                        for (auto& kv : wd2dir) {
                            if (kv.second == oldFull) kv.second = newFull;
                            else if (kv.second.rfind(oldFull + L"/", 0) == 0)
                                kv.second = newFull + kv.second.substr(oldFull.size());
                        }
                        movedDir = true;
                    }
                    pending.erase(pit);
                }
                emit(fe);
                // если переименовали в папку — начинаем и её наблюдать
                if (isDir && !movedDir)
                    addRecursive(fd, wd2dir, newPath.wstring());
            }
            else if (ev->mask & IN_CREATE) {
                // новая папка => наблюдаем и её тоже (рекурсивно)
                std::error_code ec;
                if (fs::is_directory(fs::path(dir) / fs::path(wname), ec)) {
                    addRecursive(fd, wd2dir, dir + L"/" + wname);
                }
                fe.action = EventAction::Created;
                emit(fe);
            }
            else if (ev->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
                fe.action = EventAction::Modified;
                emit(fe);
            }
            else if (ev->mask & IN_DELETE) {
                fe.action = EventAction::Deleted;
                emit(fe);
            }

            p += sizeof(inotify_event) + ev->len;
        }
        flushStaleMoves();
    }
    close(fd);
}

#endif  // _WIN32

} // namespace ae
