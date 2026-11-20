// tray.cpp — трей-иконка и уведомления Aegis (см. tray.h).
#include "tray.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ae {
namespace {

void copyWide(wchar_t* dst, size_t capacity, const std::wstring& src) {
    if (!dst || capacity == 0) return;
    const size_t n = src.size() < capacity - 1 ? src.size() : capacity - 1;
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = L'\0';
}

#ifndef _WIN32
std::string toUtf8(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) {
        if (c < 0x80) out.push_back(char(c));
        else if (c < 0x800) {
            out.push_back(char(0xC0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else {
            out.push_back(char(0xE0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::string statusPath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.aegis/tray.status";
}

void writeStatus(bool running, bool max) {
    std::ofstream f(statusPath(), std::ios::trunc);
    if (!f) return;
    f << "aegis: " << (running ? "active" : "stopped")
      << (max ? " [MAX SECURITY]" : "") << "\n";
}

// Без shell: аргументы передаются напрямую в execvp, поэтому кавычки
// и `$` в тексте уведомления не могут стать командой.
void spawnNotify(const std::string& title, const std::string& message) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
#ifdef __APPLE__
        std::string esc = message;
        std::string ttl = title;
        for (std::string* s : {&esc, &ttl}) {
            std::string r;
            for (char c : *s) {
                if (c == '\\' || c == '"') r.push_back('\\');
                r.push_back(c);
            }
            *s = r;
        }
        const std::string script = "display notification \"" + esc +
                                   "\" with title \"" + ttl + "\"";
        execlp("osascript", "osascript", "-e", script.c_str(), (char*)nullptr);
#else
        execlp("notify-send", "notify-send", "-u", "critical", "-t", "7000",
               title.c_str(), message.c_str(), (char*)nullptr);
#endif
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, WNOHANG);
}
#endif

} // namespace

#ifdef _WIN32
#define AEGIS_TRAY_MSG (WM_APP + 1)
#define AEGIS_TRAY_STATUS 1
#define AEGIS_TRAY_QUARANTINE 2
#define AEGIS_TRAY_MAX 3
#define AEGIS_TRAY_STOP 4

struct Tray::Impl {
    HWND hwnd = nullptr;
    HICON icon = nullptr;
    NOTIFYICONDATAW nid{};
    TrayCallbacks cb;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> max{false};
};

static LRESULT CALLBACK trayWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Tray::Impl* self = reinterpret_cast<Tray::Impl*>(
        GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case AEGIS_TRAY_MSG:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
            POINT p{};
            GetCursorPos(&p);
            SetForegroundWindow(h);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, AEGIS_TRAY_STATUS, L"Status");
            AppendMenuW(menu, MF_STRING, AEGIS_TRAY_QUARANTINE, L"Quarantine");
            AppendMenuW(menu, MF_STRING | (self && self->max.load() ? MF_CHECKED : 0),
                        AEGIS_TRAY_MAX, L"Max security");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, AEGIS_TRAY_STOP, L"Stop protection");
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
            DestroyMenu(menu);
            PostMessageW(h, WM_NULL, 0, 0);
        } else if (lp == WM_LBUTTONDBLCLK && self && self->cb.status) {
            self->cb.status();
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (self) {
            if (id == AEGIS_TRAY_STATUS && self->cb.status) self->cb.status();
            else if (id == AEGIS_TRAY_QUARANTINE && self->cb.quarantine) self->cb.quarantine();
            else if (id == AEGIS_TRAY_MAX && self->cb.toggleMax) self->cb.toggleMax();
            else if (id == AEGIS_TRAY_STOP && self->cb.stop) self->cb.stop();
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, msg, wp, lp);
    }
}

static void trayThread(Tray::Impl* self) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = trayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AegisTrayWindowClass";
    RegisterClassExW(&wc); // ERROR_CLASS_ALREADY_EXISTS is harmless here

    self->hwnd = CreateWindowExW(0, L"AegisTrayWindowClass", L"Aegis",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!self->hwnd) { self->running.store(false); return; }
    SetWindowLongPtrW(self->hwnd, GWLP_USERDATA, (LONG_PTR)self);

    // MinGW defines IDI_* as MAKEINTRESOURCE (char*); LoadIconW wants LPCWSTR.
    self->icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_SHIELD));
    if (!self->icon)
        self->icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_INFORMATION));

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = self->hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = AEGIS_TRAY_MSG;
    nid.hIcon = self->icon;
    copyWide(nid.szTip, 128, L"Aegis — guardian active");
    self->nid = nid;
    Shell_NotifyIconW(NIM_ADD, &self->nid);
    self->running.store(true);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shell_NotifyIconW(NIM_DELETE, &self->nid);
    self->running.store(false);
}
#else
struct Tray::Impl {
    TrayCallbacks cb;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> max{false};
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<std::wstring, std::wstring>> queue;
    bool stopRequested = false;
};

static void trayThread(Tray::Impl* self) {
    writeStatus(true, false);
    for (;;) {
        std::vector<std::pair<std::wstring, std::wstring>> batch;
        {
            std::unique_lock<std::mutex> lock(self->m);
            self->cv.wait_for(lock, std::chrono::seconds(2),
                              [self] { return self->stopRequested || !self->queue.empty(); });
            while (!self->queue.empty()) {
                batch.push_back(std::move(self->queue.front()));
                self->queue.pop_front();
            }
            if (self->stopRequested) break;
        }
        writeStatus(true, self->max.load());
        for (const auto& item : batch)
            spawnNotify(toUtf8(item.first), toUtf8(item.second));
    }
    writeStatus(false, self->max.load());
    self->running.store(false);
}
#endif

Tray::Tray() : impl_(new Impl()) {}

Tray::~Tray() { stop(); }

bool Tray::start(const TrayCallbacks& cb) {
    if (impl_->running.load()) return true;
    impl_->cb = cb;
#ifdef _WIN32
    impl_->thread = std::thread(trayThread, impl_.get());
    for (int i = 0; i < 50 && !impl_->running.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
#else
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->stopRequested = false;
    }
    impl_->running.store(true);
    impl_->thread = std::thread(trayThread, impl_.get());
#endif
    return impl_->running.load();
}

void Tray::notify(const std::wstring& title, const std::wstring& message) {
    if (!impl_->running.load()) return;
#ifdef _WIN32
    if (!impl_->hwnd) return;
    NOTIFYICONDATAW n = impl_->nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_WARNING;
    copyWide(n.szInfoTitle, 64, title);
    copyWide(n.szInfo, 256, message);
    Shell_NotifyIconW(NIM_MODIFY, &n);
#else
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->queue.emplace_back(title, message);
    }
    impl_->cv.notify_one();
#endif
}

void Tray::setMaxSecurity(bool on) {
    impl_->max.store(on);
    if (!impl_->running.load()) return;
#ifdef _WIN32
    if (!impl_->hwnd) return;
    NOTIFYICONDATAW n = impl_->nid;
    n.uFlags = NIF_TIP;
    copyWide(n.szTip, 128, on ? L"Aegis — MAX SECURITY" : L"Aegis — guardian active");
    Shell_NotifyIconW(NIM_MODIFY, &n);
#else
    writeStatus(true, on);
#endif
}

void Tray::stop() {
    if (!impl_->running.load()) return;
#ifdef _WIN32
    Shell_NotifyIconW(NIM_DELETE, &impl_->nid);
    if (impl_->hwnd) PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
#else
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->stopRequested = true;
    }
    impl_->cv.notify_one();
#endif
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->running.store(false);
#ifdef _WIN32
    impl_->hwnd = nullptr;
#endif
}

bool Tray::running() const {
    return impl_->running.load();
}

} // namespace ae
