// autostart.cpp — `aegis autostart on|off`.
//
// Windows: ключ HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//          (Current User — админские права не нужны).
// Linux:   ~/.config/autostart/aegis.desktop (XDG autostart).
// macOS:   ~/Library/LaunchAgents/com.aegis.guardian.plist (LaunchAgent).
#include "autostart.h"

#include "logger.h"
#include "updater.h"   // ae::selfPath()

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
namespace ae {

#ifdef _WIN32

static const wchar_t* kRegPath  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRegValue = L"Aegis";

int runAutostart(const std::wstring& mode) {
    if (mode != L"on" && mode != L"off") {
        warn(L"usage: aegis autostart on|off");
        return 1;
    }
    std::wstring self = selfPath();
    if (self.empty()) { warn(L"cannot determine own path"); return 1; }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        warn(L"cannot open HKCU Run key");
        return 1;
    }

    if (mode == L"off") {
        LSTATUS rc = RegDeleteValueW(hKey, kRegValue);   // нет значения — тоже успех
        RegCloseKey(hKey);
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
            warn(L"cannot remove HKCU Run value");
            return 1;
        }
        ok(L"autostart/background protection disabled");
        return 0;
    }

    // Start protection automatically in background (no interactive console needed)
    const std::wstring cmd = L"\"" + self + L"\" watch --daemon";
    const BYTE* data = reinterpret_cast<const BYTE*>(cmd.c_str());
    DWORD bytes = (DWORD)((cmd.size() + 1) * sizeof(wchar_t));
    LSTATUS rc = RegSetValueExW(hKey, kRegValue, 0, REG_SZ, data, bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) { warn(L"cannot write HKCU Run key"); return 1; }

    ok(L"autostart/background protection enabled (daemon at login): " + self);
    return 0;
}

int autostartStatus() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        console(L"Autostart: unknown (cannot read HKCU Run key)");
        return 0;
    }
    wchar_t buf[2048] = {};
    DWORD type = 0, size = sizeof(buf);
    LSTATUS rc = RegQueryValueExW(hKey, kRegValue, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    std::wstring cmdLine;
    if (rc == ERROR_SUCCESS && type == REG_SZ) {
        cmdLine.assign(buf, size / sizeof(wchar_t));
        while (!cmdLine.empty() && cmdLine.back() == L'\0') cmdLine.pop_back();
    } else if (rc == ERROR_MORE_DATA) {
        // значение длиннее буфера: забираем нужный размер и читаем ещё раз
        std::wstring big(size / sizeof(wchar_t), L'\0');
        DWORD size2 = size;
        if (RegQueryValueExW(hKey, kRegValue, nullptr, &type,
                             reinterpret_cast<LPBYTE>(big.data()), &size2) == ERROR_SUCCESS &&
            type == REG_SZ) {
            big.resize(size2 / sizeof(wchar_t));
            while (!big.empty() && big.back() == L'\0') big.pop_back();
            cmdLine = big;
        }
    }
    RegCloseKey(hKey);
    if (!cmdLine.empty()) {
        console(L"Autostart: ENABLED");
        console(L"  Command: " + cmdLine);
    } else {
        console(L"Autostart: DISABLED");
    }
    console(L"Mode: user-level background protection via autostart + `aegis watch --daemon`");
    return 0;
}

int runServiceCommand(const std::wstring& action) {
    if (action == L"status") return autostartStatus();
    if (action == L"install" || action == L"enable") return runAutostart(L"on");
    if (action == L"uninstall" || action == L"disable") return runAutostart(L"off");
    if (action == L"start") {
        console(L"Starting background protection now (best effort): launching `aegis watch --daemon`...");
        std::wstring self = selfPath();
        if (self.empty()) { warn(L"cannot determine own path"); return 1; }
        std::wstring cmd = L"\"" + self + L"\" watch --daemon";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = cmd;
        if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            ok(L"Background daemon launch requested.");
            return 0;
        }
        warn(L"Failed to start background daemon process.");
        return 1;
    }
    if (action == L"stop") {
        console(L"Stop is best-effort for daemon mode. If running interactively press q/Ctrl+C; for background processes stop via Task Manager or reboot/login cycle.");
        console(L"Tip: `aegis autostart off` disables future auto-start.");
        return 0;
    }
    warn(L"usage: aegis service status|install|uninstall|start|stop");
    return 1;
}

#else // Linux / macOS

static std::wstring homeDirRaw() {
    const char* h = std::getenv("HOME");
    return h ? std::wstring(h, h + std::strlen(h)) : L"";
}

// На POSIX широкие строки путей хранят сырые байты (см. updater.cpp):
// обратное превращение — просто копия байтов, без повторного UTF-8-кодирования.
static std::string rawNarrow(const std::wstring& w) {
    return std::string(w.begin(), w.end());
}

static std::string xmlEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

// Экранирование значения Exec= по спецификации Desktop Entry.
static std::string desktopEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"' || c == '`' || c == '$') o += '\\';
        o += c;
    }
    return o;
}

static bool writeFileChecked(const fs::path& p, const std::string& body) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(body.data(), (std::streamsize)body.size());
    f.flush();
    if (!f) { f.close(); return false; }
    f.close();
    return static_cast<bool>(f);
}

#ifdef __APPLE__

// macOS: LaunchAgent plist — сработает при входе пользователя.
static std::wstring agentPath() {
    return homeDirRaw() + L"/Library/LaunchAgents/com.aegis.guardian.plist";
}

int runAutostart(const std::wstring& mode) {
    if (mode != L"on" && mode != L"off") {
        warn(L"usage: aegis autostart on|off");
        return 1;
    }
    std::wstring self = selfPath();
    if (self.empty()) { warn(L"cannot determine own path"); return 1; }

    const fs::path plist = fs::path(agentPath());
    if (mode == L"off") {
        std::error_code ec;
        std::error_code existEc;
        bool existed = fs::exists(plist, existEc);
        if (existed) {
            fs::remove(plist, ec);              // нет файла — тоже успех
            if (ec) { warn(L"cannot remove LaunchAgent plist"); return 1; }
        }
        ok(L"autostart/background protection disabled (LaunchAgent plist removed)");
        return 0;
    }

    std::error_code ec;
    fs::create_directories(plist.parent_path(), ec);
    if (ec) { warn(L"cannot create LaunchAgents directory"); return 1; }

    std::string narrow = xmlEsc(rawNarrow(self));
    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
        " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>Label</key>\n  <string>com.aegis.guardian</string>\n"
        "  <key>ProgramArguments</key>\n  <array>\n"
        "    <string>" + narrow + "</string>\n"
        "    <string>watch</string>\n"
        "    <string>--daemon</string>\n"
        "  </array>\n"
        "  <key>RunAtLoad</key>\n  <true/>\n"
        "  <key>KeepAlive</key>\n  <true/>\n"
        "</dict>\n</plist>\n";

    if (!writeFileChecked(plist, body)) { warn(L"cannot write LaunchAgent plist"); return 1; }

    ok(L"autostart/background protection enabled (LaunchAgent): " + plist.wstring());
    return 0;
}

int autostartStatus() {
    std::error_code ec;
    auto p = fs::path(agentPath());
    if (fs::exists(p, ec)) {
        console(L"Autostart (LaunchAgent): ENABLED");
        console(L"  Plist: " + p.wstring());
    } else {
        console(L"Autostart (LaunchAgent): DISABLED");
    }
    console(L"Mode: user-level background protection via LaunchAgent + `aegis watch --daemon` (KeepAlive)");
    return 0;
}

int runServiceCommand(const std::wstring& action) {
    if (action == L"status") return autostartStatus();
    if (action == L"install" || action == L"enable") return runAutostart(L"on");
    if (action == L"uninstall" || action == L"disable") return runAutostart(L"off");
    if (action == L"start") {
        console(L"Best-effort start: load LaunchAgent with launchctl if available, or run `aegis watch --daemon` manually.");
        console(L"Try: launchctl bootstrap gui/$UID " + fs::path(agentPath()).wstring());
        return 0;
    }
    if (action == L"stop") {
        console(L"Best-effort stop: unload LaunchAgent via launchctl or stop daemon process.");
        console(L"Try: launchctl bootout gui/$UID " + fs::path(agentPath()).wstring());
        return 0;
    }
    warn(L"usage: aegis service status|install|uninstall|start|stop");
    return 1;
}

#else // Linux

// Linux: XDG autostart .desktop — сработает в GNOME/KDE/XFCE.
static std::wstring desktopPath() {
    return homeDirRaw() + L"/.config/autostart/aegis.desktop";
}

int runAutostart(const std::wstring& mode) {
    if (mode != L"on" && mode != L"off") {
        warn(L"usage: aegis autostart on|off");
        return 1;
    }
    std::wstring self = selfPath();
    if (self.empty()) { warn(L"cannot determine own path"); return 1; }

    const fs::path desktop = fs::path(desktopPath());
    if (mode == L"off") {
        std::error_code existEc;
        if (fs::exists(desktop, existEc)) {
            std::error_code ec;
            fs::remove(desktop, ec);            // нет файла — тоже успех
            if (ec) { warn(L"cannot remove autostart .desktop"); return 1; }
        }
        ok(L"autostart/background protection disabled");
        return 0;
    }

    std::error_code ec;
    fs::create_directories(desktop.parent_path(), ec);
    if (ec) { warn(L"cannot create autostart directory"); return 1; }

    std::string narrow = desktopEsc(rawNarrow(self));
    std::string body =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Aegis\n"
        "Comment=Anti-ransomware file guardian\n"
        "Exec=\"" + narrow + "\" watch --daemon\n"
        "X-GNOME-Autostart-enabled=true\n"
        "Terminal=false\n";

    if (!writeFileChecked(desktop, body)) { warn(L"cannot write autostart .desktop"); return 1; }

    ok(L"autostart/background protection enabled: " + desktop.wstring());
    return 0;
}

int autostartStatus() {
    std::error_code ec;
    auto p = fs::path(desktopPath());
    if (fs::exists(p, ec)) {
        console(L"Autostart (XDG): ENABLED");
        console(L"  Desktop file: " + p.wstring());
    } else {
        console(L"Autostart (XDG): DISABLED");
    }
    console(L"Mode: user-level background protection via XDG autostart + `aegis watch --daemon`");
    return 0;
}

int runServiceCommand(const std::wstring& action) {
    if (action == L"status") return autostartStatus();
    if (action == L"install" || action == L"enable") return runAutostart(L"on");
    if (action == L"uninstall" || action == L"disable") return runAutostart(L"off");
    if (action == L"start") {
        console(L"Best-effort start: run `aegis watch --daemon` now (or re-login if XDG autostart is enabled).");
        std::string self = rawNarrow(selfPath());
        if (self.empty()) { warn(L"cannot determine own path"); return 1; }
        pid_t pid = fork();
        if (pid < 0) { warn(L"cannot fork to start daemon"); return 1; }
        if (pid == 0) {
            setsid();
            execl(self.c_str(), self.c_str(), "watch", "--daemon",
                  static_cast<char*>(nullptr));
            _exit(127);
        }
        ok(L"Background daemon launch requested (best effort).");
        return 0;
    }
    if (action == L"stop") {
        console(L"Best-effort stop: stop the running daemon process (e.g. pkill -f 'aegis.*--daemon') or end it manually.");
        return 0;
    }
    warn(L"usage: aegis service status|install|uninstall|start|stop");
    return 1;
}

#endif // __APPLE__
#endif // _WIN32

} // namespace ae
