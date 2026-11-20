// configs.cpp — безопасный обмен конфигурациями Aegis v6.0.
#include "configs.h"

#include "config.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ae {
namespace {

const size_t kMaxConfigBytes = 256 * 1024;

std::wstring trimW(std::wstring s) {
    const wchar_t* ws = L" \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::wstring::npos) return L"";
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::wstring lowerW(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = wchar_t(c + 32);
        else if (c >= 0x0410 && c <= 0x042F) c = wchar_t(c + 32);
    }
    return s;
}

std::string narrow(const std::wstring& w) {
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

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeAllAtomic(const fs::path& p, const std::string& data, std::wstring& error) {
    const fs::path tmp = p.wstring() + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { error = L"cannot write temporary file"; return false; }
        f.write(data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) { error = L"failed to write temporary file"; return false; }
    }
    std::error_code ec;
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    if (ec) { error = L"failed to replace config: " + std::wstring(ec.message().begin(), ec.message().end()); return false; }
    return true;
}

bool downloadTo(const std::wstring& url, const fs::path& out) {
#ifdef _WIN32
    // URLDownloadToFileW is loaded dynamically — CI links only shell32/ole32/uuid.
    typedef int (WINAPI *DownloadFn)(void*, const wchar_t*, const wchar_t*, unsigned long, void*);
    HMODULE h = LoadLibraryW(L"urlmon.dll");
    if (!h) return false;
    auto fn = reinterpret_cast<DownloadFn>(GetProcAddress(h, "URLDownloadToFileW"));
    if (!fn) { FreeLibrary(h); return false; }
    const int hr = fn(nullptr, url.c_str(), out.wstring().c_str(), 0, nullptr);
    FreeLibrary(h);
    return hr == 0;
#else
    const std::string u = narrow(url);
    const std::string o = out.string();
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execlp("curl", "curl", "-fsSL", "--max-time", "30", "-o", o.c_str(), u.c_str(), (char*)nullptr);
        execlp("wget", "wget", "-q", "-T", "30", "-O", o.c_str(), u.c_str(), (char*)nullptr);
        _exit(1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
#endif
}

bool isKnownKey(const std::string& k) {
    static const std::set<std::string> keys = {
        "sensitivity", "ultralight", "autoupdate", "backup_before_quarantine",
        "language", "ui_theme", "mod_disabled", "notify_alerts",
        "exclude_path", "exclude_ext", "last_update_available",
        "last_update_version", "watch"
    };
    return keys.count(k) != 0;
}

bool validValue(const std::string& key, const std::string& v) {
    auto flag = [](const std::string& x) { return x == "0" || x == "1" || x == "true" || x == "false"; };
    if (key == "sensitivity") return v == "0" || v == "1" || v == "2";
    if (key == "ultralight" || key == "autoupdate" || key == "backup_before_quarantine" ||
        key == "notify_alerts" || key == "last_update_available") return flag(v);
    if (key == "language") return v == "en" || v == "ru";
    if (key == "ui_theme") return v == "default" || v == "white" || v == "black";
    if (key == "mod_disabled" || key == "exclude_ext") return v.size() <= 128;
    if (key == "exclude_path" || key == "watch" || key == "last_update_version")
        return v.size() <= 2048;
    return false;
}

std::string sanitizeName(const std::wstring& raw) {
    std::wstring s;
    for (wchar_t c : raw) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_') s.push_back(c);
        else if (!s.empty() && s.back() != L'_') s.push_back(L'_');
        if (s.size() >= 48) break;
    }
    while (!s.empty() && s.back() == L'_') s.pop_back();
    return narrow(s.empty() ? L"preset" : s);
}

std::string stamp() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char b[32] = {};
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &lt);
    return b;
}

// Возвращает нормализованный текст и список отличий от текущего конфига.
bool validateConfig(const std::string& raw, std::string& clean,
                    std::vector<std::wstring>& diff, std::wstring& error) {
    if (raw.size() > kMaxConfigBytes) { error = L"config is too large (limit 256 KiB)"; return false; }
    size_t pos = 0;
    std::string text = raw;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) pos = 3;

    std::vector<std::string> lines;
    std::set<std::pair<std::string, std::string>> seen;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') { lines.push_back(line); continue; }
        for (unsigned char c : line) {
            if (c == 0 || c == '\r' || c < 0x20) { error = L"config contains control characters"; return false; }
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) { error = L"invalid line (expected key=value): " + std::wstring(line.begin(), line.end()); return false; }
        std::string key = line.substr(0, eq);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        std::string value = line.substr(eq + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (!isKnownKey(key)) { error = L"unknown/unsupported key: " + std::wstring(key.begin(), key.end()); return false; }
        if (!validValue(key, value)) { error = L"unsafe value for " + std::wstring(key.begin(), key.end()); return false; }
        lines.push_back(key + "=" + value);
        seen.emplace(key, value);
    }

    std::string current;
    if (readAll(configPath(), current)) {
        std::set<std::pair<std::string, std::string>> old;
        std::istringstream ss(current);
        std::string line;
        while (std::getline(ss, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            while (!key.empty() && key.back() == '\r') key.pop_back();
            while (!value.empty() && value.back() == '\r') value.pop_back();
            old.emplace(key, value);
        }
        for (const auto& item : seen) {
            if (!old.count(item))
                diff.push_back(L"+ " + std::wstring(item.first.begin(), item.first.end()) + L"=" +
                               std::wstring(item.second.begin(), item.second.end()));
        }
        for (const auto& item : old) {
            std::string k = item.first;
            bool found = false;
            for (const auto& n : seen) if (n.first == k) { found = true; break; }
            if (!found && isKnownKey(k))
                diff.push_back(L"- " + std::wstring(k.begin(), k.end()) + L" (removed)");
        }
    }

    clean.clear();
    for (const auto& l : lines) clean += l + "\n";
    return true;
}

bool confirm(const std::wstring& question) {
    console(L"");
    console(question);
    console(L"  Apply? [y/N]: ");
    std::wstring answer;
    if (!std::getline(std::wcin, answer)) return false;
    answer = lowerW(trimW(answer));
    return answer == L"y" || answer == L"yes" || answer == L"да";
}

} // namespace

std::wstring configsDir() {
    return (fs::path(configPath()).parent_path() / L"configs").wstring();
}

std::vector<std::wstring> listPresets() {
    std::vector<std::wstring> out;
    std::error_code ec;
    fs::path dir(configsDir());
    if (!fs::exists(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file() && it->path().extension() == L".txt")
            out.push_back(it->path().stem().wstring());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool savePreset(const std::wstring& name, std::wstring& error) {
    const fs::path dir(configsDir());
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string current;
    if (!readAll(configPath(), current)) { error = L"cannot read current config"; return false; }
    const std::string file = sanitizeName(name);
    const fs::path target = dir / (std::wstring(file.begin(), file.end()) + L".txt");
    if (!writeAllAtomic(target, current, error)) return false;
    return true;
}

bool applyPreset(const std::wstring& name, std::wstring& error) {
    const std::string safe = sanitizeName(name);
    const fs::path file = fs::path(configsDir()) /
        (std::wstring(safe.begin(), safe.end()) + L".txt");
    std::string raw;
    if (!readAll(file, raw)) { error = L"preset not found: " + name; return false; }
    std::string clean;
    std::vector<std::wstring> diff;
    if (!validateConfig(raw, clean, diff, error)) return false;
    if (!diff.empty()) {
        for (const auto& d : diff) console(L"  " + d);
    }
    if (!confirm(L"  Applying preset '" + name + L"'. Current config will be backed up.")) return false;
    const fs::path backup = fs::path(configPath()).wstring() + L".backup." + std::wstring(stamp().begin(), stamp().end()) + L".txt";
    std::string current;
    if (readAll(configPath(), current)) {
        std::wstring ignored;
        writeAllAtomic(backup, current, ignored);
    }
    if (!writeAllAtomic(configPath(), clean, error)) return false;
    return true;
}

ConfigImportResult importConfigLink(const std::wstring& link) {
    ConfigImportResult r;
    const std::wstring t = lowerW(trimW(link));
    if (t.empty()) { r.message = L"empty link"; return r; }

    const fs::path tmp = fs::temp_directory_path() /
        (L"aegis-config-" + std::wstring(stamp().begin(), stamp().end()) + L"-" +
         std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".txt");

    bool downloaded = false;
    if (t.rfind(L"http://", 0) == 0 || t.rfind(L"https://", 0) == 0) {
        downloaded = downloadTo(trimW(link), tmp);
    } else {
        std::error_code ec;
        fs::copy_file(trimW(link), tmp, fs::copy_options::overwrite_existing, ec);
        downloaded = !ec;
    }
    if (!downloaded) { r.message = L"failed to download/read config from link"; return r; }

    std::string raw;
    if (!readAll(tmp, raw)) { r.message = L"downloaded file is unreadable"; return r; }
    std::string clean;
    std::wstring error;
    if (!validateConfig(raw, clean, r.diff, error)) { r.message = error; return r; }

    for (const auto& d : r.diff) console(L"  " + d);
    if (!confirm(L"  Importing config from: " + trimW(link))) { r.message = L"cancelled"; return r; }

    const fs::path backup = fs::path(configPath()).wstring() + L".backup." +
                            std::wstring(stamp().begin(), stamp().end()) + L".txt";
    std::string current;
    if (readAll(configPath(), current)) {
        std::wstring ignored;
        writeAllAtomic(backup, current, ignored);
    }
    if (!writeAllAtomic(configPath(), clean, error)) { r.message = error; return r; }
    std::error_code ec;
    fs::remove(tmp, ec);
    r.ok = true;
    r.message = L"config imported and applied (restart Aegis to reload settings)";
    return r;
}

bool publishPreset(const std::wstring& name, std::wstring& url, std::wstring& error) {
    const std::string file = sanitizeName(name);
    const std::wstring wideFile(file.begin(), file.end());
    const fs::path path = fs::path(configsDir()) / (wideFile + L".txt");
    if (!fs::exists(path)) { error = L"preset not found: " + name; return false; }
    const std::string cmd = "gh gist create \"" + narrow(path.wstring()) + "\" --public 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { error = L"cannot run gh (is GitHub CLI installed?)"; return false; }
    char buf[1024] = {};
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    pclose(pipe);
    const size_t p = output.find("https://gist.github.com/");
    if (p == std::string::npos) {
        error = L"gh gist failed: " + std::wstring(output.begin(), output.end());
        return false;
    }
    size_t e = output.find_first_of(" \t\r\n", p);
    if (e == std::string::npos) e = output.size();
    url = std::wstring(output.begin() + p, output.begin() + e);
    return true;
}

void configsMenu() {
    while (true) {
        console(L"");
        console(L"  [ CONFIGS ]  Safe sharing of Aegis presets");
        console(L"  1. List saved presets");
        console(L"  2. Save current config as preset");
        console(L"  3. Apply a preset (with diff + backup)");
        console(L"  4. Import config from link (URL or file)");
        console(L"  5. Publish preset as GitHub gist");
        console(L"  6. Show current config");
        console(L"  0. Back");
        console(L"  > ");
        std::wstring line;
        if (!std::getline(std::wcin, line)) return;
        line = trimW(line);
        if (line.empty() || line == L"0") return;

        if (line == L"1") {
            auto presets = listPresets();
            if (presets.empty()) console(L"  no presets yet");
            else for (const auto& p : presets) console(L"  - " + p);
        } else if (line == L"2") {
            console(L"  Preset name: ");
            std::wstring name;
            std::getline(std::wcin, name);
            std::wstring error;
            console(savePreset(name, error) ? L"  OK preset saved" : L"  ERROR " + error);
        } else if (line == L"3") {
            console(L"  Preset name: ");
            std::wstring name;
            std::getline(std::wcin, name);
            std::wstring error;
            console(applyPreset(name, error) ? L"  OK preset applied (restart Aegis)" : L"  ERROR " + error);
        } else if (line == L"4") {
            console(L"  Paste link: ");
            std::wstring link;
            std::getline(std::wcin, link);
            auto r = importConfigLink(link);
            console(r.ok ? L"  OK " + r.message : L"  ERROR " + r.message);
        } else if (line == L"5") {
            console(L"  Preset name: ");
            std::wstring name;
            std::getline(std::wcin, name);
            std::wstring url, error;
            if (publishPreset(name, url, error)) console(L"  OK " + url);
            else console(L"  ERROR " + error);
        } else if (line == L"6") {
            std::string raw;
            if (readAll(configPath(), raw)) {
                std::istringstream ss(raw);
                std::string l;
                while (std::getline(ss, l)) console(L"  " + std::wstring(l.begin(), l.end()));
            } else console(L"  cannot read config");
        }
    }
}

} // namespace ae
