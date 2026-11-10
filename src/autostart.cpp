// autostart.cpp — `aegis autostart on|off`.
//
// Windows: ключ HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//          (Current User — админские права не нужны).
// Linux:   ~/.config/autostart/aegis.desktop (XDG autostart).
// macOS:   ~/Library/LaunchAgents/com.aegis.guardian.plist (LaunchAgent).
#include "autostart.h"

#include "logger.h"
#include "updater.h"   // ae::selfPath()

#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <fstream>
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
        RegDeleteValueW(hKey, kRegValue);   // нет значения — тоже успех
        RegCloseKey(hKey);
        ok(L"autostart disabled");
        return 0;
    }

    const std::wstring quoted = L"\"" + self + L"\"";
    const BYTE* data = reinterpret_cast<const BYTE*>(quoted.c_str());
    DWORD bytes = (DWORD)((quoted.size() + 1) * sizeof(wchar_t));
    LSTATUS rc = RegSetValueExW(hKey, kRegValue, 0, REG_SZ, data, bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) { warn(L"cannot write HKCU Run key"); return 1; }

    ok(L"autostart enabled: " + self);
    return 0;
}

#else // Linux / macOS

static std::wstring homeDirRaw() {
    const char* h = std::getenv("HOME");
    return h ? std::wstring(h, h + std::strlen(h)) : L"";
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
        fs::remove(plist, ec);              // нет файла — тоже успех
        ok(L"autostart disabled (remove the agent with launchctl bootout if it is running)");
        return 0;
    }

    std::error_code ec;
    fs::create_directories(plist.parent_path(), ec);

    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
        " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>Label</key>\n  <string>com.aegis.guardian</string>\n"
        "  <key>ProgramArguments</key>\n  <array>\n"
        "    <string>";

    std::string narrow = fs::path(self).string();   // широкий путь -> родной narrow (UTF-8 на POSIX)
    body += narrow;
    body +=
        "</string>\n  </array>\n"
        "  <key>RunAtLoad</key>\n  <true/>\n"
        "</dict>\n</plist>\n";

    std::ofstream f(plist, std::ios::binary | std::ios::trunc);
    if (!f) { warn(L"cannot write LaunchAgent plist"); return 1; }
    f << body;
    f.close();

    ok(L"autostart enabled: " + plist.wstring());
    return 0;
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
        std::error_code ec;
        fs::remove(desktop, ec);            // нет файла — тоже успех
        ok(L"autostart disabled");
        return 0;
    }

    std::error_code ec;
    fs::create_directories(desktop.parent_path(), ec);

    std::string body =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Aegis\n"
        "Comment=Anti-ransomware file guardian\n"
        "Exec=";
    std::string narrow = fs::path(self).string();
    body += "\"" + narrow + "\"";                   // кавычки на случай пробелов в пути
    body +=
        "\nX-GNOME-Autostart-enabled=true\n"
        "Terminal=false\n";

    std::ofstream f(desktop, std::ios::binary | std::ios::trunc);
    if (!f) { warn(L"cannot write autostart .desktop"); return 1; }
    f << body;
    f.close();

    ok(L"autostart enabled: " + desktop.wstring());
    return 0;
}

#endif // __APPLE__
#endif // _WIN32

} // namespace ae
