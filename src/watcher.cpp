// watcher.cpp — Windows-реализация наблюдателя (только Win32, без зависимостей)
#include "watcher.h"
#include "logger.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    for (int i = 0; i < (int)dirs_.size(); ++i)
        threads_.emplace_back(&Watcher::loop, this, i);
}

void Watcher::stop() {
    running_ = false;
    // Потоки уходят из блокирующего ReadDirectoryChangesW только вместе
    // с процессом, поэтому detach — иначе деструктор std::thread вызовет
    // std::terminate в момент завершения main().
    for (auto& t : threads_) {
        if (t.joinable()) t.detach();
    }
    threads_.clear();
}

void Watcher::loop(int index) {
    const std::wstring dir = dirs_[index];

#ifdef _WIN32
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
#else
    (void)index;
    ae::warn(L"платформа пока не поддерживается (в планах Linux/macOS)");
#endif
}

} // namespace ae
