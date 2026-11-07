// watcher.cpp — реализация наблюдателя:
//   Windows : ReadDirectoryChangesW (один поток на папку)
//   Linux   : inotify (один поток на все папки, рекурсивный проход)
//   macOS   : TODO (FSEvents), пока предупреждение
#include "watcher.h"
#include "logger.h"

#include <filesystem>
#include <map>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#endif

namespace fs = std::filesystem;

namespace ae {

bool Watcher::add(const std::wstring& dir) {
    std::error_code ec;
    fs::path p(dir);
    if (!fs::exists(p, ec) || ec) return false;
    dirs_.push_back(fs::absolute(p, ec).wstring());
    return !ec;
}

void Watcher::start() {
    running_ = true;
#ifdef _WIN32
    for (int i = 0; i < (int)dirs_.size(); ++i)
        threads_.emplace_back(&Watcher::loop, this, i);
#else
    // Linux/macOS: единый поток observing все добавленные папки
    if (!dirs_.empty())
        threads_.emplace_back(&Watcher::loop, this, 0);
#endif
}

void Watcher::stop() {
    running_ = false;
    // Потоки уходят из блокирующего системного вызова только вместе
    // с процессом, поэтому detach — иначе деструктор std::thread вызовет
    // std::terminate в момент завершения main().
    for (auto& t : threads_) {
        if (t.joinable()) t.detach();
    }
    threads_.clear();
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
        FILE_FLAG_BACKUP_SEMANTICS,   // открываем папку как дескриптор
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        ae::warn(L"не удалось наблюдать: " + dir + L" (код " +
                 std::to_wstring(GetLastError()) + L")");
        return;
    }

    ae::info(L"наблюдаю: " + dir);

    constexpr DWORD BUF = 64 * 1024;
    std::vector<BYTE> buf(BUF);
    DWORD bytes = 0;
    std::wstring pendingOld;

    while (running_) {
        if (!ReadDirectoryChangesW(
                h, buf.data(), BUF,
                TRUE,  // рекурсивно, включая подпапки
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                    FILE_NOTIFY_CHANGE_SIZE,
                &bytes, nullptr, nullptr))
            break;

        BYTE* p = buf.data();
        for (;;) {
            auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            std::wstring name(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

            FileEvent ev;
            ev.dir = dir;
            switch (fni->Action) {
                case FILE_ACTION_ADDED:
                    ev.action = EventAction::Created; ev.name = name; break;
                case FILE_ACTION_MODIFIED:
                    ev.action = EventAction::Modified; ev.name = name; break;
                case FILE_ACTION_REMOVED:
                    ev.action = EventAction::Deleted; ev.name = name; break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    pendingOld = name; break;  // пара: старое имя ждёт новое
                case FILE_ACTION_RENAMED_NEW_NAME:
                    ev.action = EventAction::Renamed;
                    ev.name = name;
                    ev.oldName = pendingOld;
                    pendingOld.clear();
                    break;
                default:
                    break;  // неизвестное действие — пропускаем
            }

            if (fni->Action != FILE_ACTION_RENAMED_OLD_NAME)
                cb_(ev);

            if (fni->NextEntryOffset == 0) break;
            p += fni->NextEntryOffset;
        }
    }
    CloseHandle(h);
}

#else  // ====================== POSIX (Linux/macOS) ======================

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
    }
    // и сам корень тоже
    int wd = inotify_add_watch(fd, fs::path(dir).string().c_str(),
                               IN_CREATE | IN_MODIFY |
                               IN_MOVED_FROM | IN_MOVED_TO |
                               IN_DELETE | IN_CLOSE_WRITE);
    if (wd >= 0) wd2dir[wd] = dir;
}

void Watcher::loop(int index) {
    (void)index;
    int fd = inotify_init1(0 /* блокирующий режим */);
    if (fd < 0) {
        ae::warn(L"inotify_init не удался");
        return;
    }

    std::map<int, std::wstring> wd2dir;
    for (const auto& d : dirs_) {
        addRecursive(fd, wd2dir, d);
        ae::info(L"наблюдаю: " + d);
    }

    // подставки под событие MOVED_FROM -> MOVED_TO (как RENAMED_OLD/NEW)
    std::map<unsigned, FileEvent> pending;  // cookie -> событие

    constexpr size_t BUF = 64 * 1024;
    std::vector<char> buf(BUF);

    auto emit = [&](FileEvent ev) {
        if (running_) cb_(ev);
    };

    while (running_) {
        ssize_t len = read(fd, buf.data(), BUF);
        if (len <= 0) break;

        char* p = buf.data();
        char* end = p + len;
        while (p < end) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            std::string name(ev->name ? ev->name : "");
            auto itDir = wd2dir.find(ev->wd);
            std::wstring dir = (itDir != wd2dir.end()) ? itDir->second : L"";
            std::wstring wname(name.begin(), name.end());

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
                pending[ev->cookie] = fe;    // ждём пару
            }
            else if (ev->mask & IN_MOVED_TO) {
                auto pit = pending.find(ev->cookie);
                fe.action = EventAction::Renamed;
                if (pit != pending.end()) {
                    fe.oldName = pit->second.oldName;
                    pending.erase(pit);
                }
                emit(fe);
                // если переименовали в папку — начинаем и её наблюдать
                std::error_code ec;
                if (fs::is_directory(fs::path(dir) / fs::path(wname), ec))
                    addRecursive(fd, wd2dir, dir + L"/" + wname);
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
    }
    close(fd);
}

#endif  // _WIN32

} // namespace ae
