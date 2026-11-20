// mods.cpp — Aegis mods/tweaks platform

#include "mods.h"

#include "logger.h"
#include "version.h"
#include "config.h"
#include "updater.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <cctype>
#include <cstring>
#include <fstream>
#include <locale>
#include <codecvt>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ae {

static std::wstring homeDirLocal() {
#ifdef _WIN32
    if (const wchar_t* e = _wgetenv(L"USERPROFILE")) return e;
    if (const wchar_t* e = _wgetenv(L"HOME")) return e;
    return L"";
#else
    const char* h = std::getenv("HOME");
    return h ? std::wstring(h, h + std::strlen(h)) : L"";
#endif
}

static std::wstring aegisDirLocal() {
    std::wstring home = homeDirLocal();
    if (home.empty()) home = L".";
#ifdef _WIN32
    return (fs::path(home) / L".aegis").wstring();
#else
    return (fs::path(home) / ".aegis").wstring();
#endif
}

std::wstring modsDirPath() {
    return (fs::path(aegisDirLocal()) / L"mods").wstring();
}

std::wstring modsStatePath() {
    return (fs::path(aegisDirLocal()) / L"mods-state.txt").wstring();
}

static std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
#ifdef _WIN32
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
#else
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.to_bytes(w);
    } catch (...) {
        return std::string(w.begin(), w.end());
    }
#endif
}

static std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return {};
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
#else
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
#endif
}

static std::wstring toWLoose(const std::string& s) { return utf8ToW(s); }

static std::wstring trimWLocal(std::wstring s) {
    auto issp = [](wchar_t c){ return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'; };
    size_t a=0,b=s.size();
    while (a<b && issp(s[a])) ++a;
    while (b>a && issp(s[b-1])) --b;
    return s.substr(a,b-a);
}

static std::wstring lowerWLocal(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

std::wstring modKey(const ModManifest& m) {
    if (!m.id.empty()) return lowerWLocal(m.id);
    fs::path p(m.file);
    return lowerWLocal(p.stem().wstring());
}

bool isModEnabled(const Settings& settings, const ModManifest& m) {
    const std::wstring k = modKey(m);
    for (const auto& d : settings.disabledMods) {
        if (lowerWLocal(d) == k) return false;
    }
    return true;
}

void setModEnabled(Settings& settings, const ModManifest& m, bool enabled) {
    const std::wstring k = modKey(m);
    auto eq = [&](const std::wstring& x){ return lowerWLocal(x) == k; };
    settings.disabledMods.erase(std::remove_if(settings.disabledMods.begin(), settings.disabledMods.end(), eq), settings.disabledMods.end());
    if (!enabled) settings.disabledMods.push_back(k);
}

ModManifest loadModManifest(const fs::path& p, const Settings& settings) {
    ModManifest m;
    m.file = p.filename().wstring();
    m.path = p.wstring();
    m.name = p.stem().wstring();
    m.status = L"unknown";
    m.type = L"tweak";

    std::ifstream f(p, std::ios::binary);
    if (!f) return m;
    std::string line;
    bool firstLine = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (firstLine) {
            firstLine = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);   // BOM в манифесте не должен терять первое поле
        }
        std::string t = line;
        // trim
        while (!t.empty() && (t.front()==' '||t.front()=='\t')) t.erase(t.begin());
        if (t.empty() || t[0]=='#' || t[0]==';') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = t.substr(0, eq);
        std::string v = t.substr(eq+1);
        while (!k.empty() && (k.back()==' '||k.back()=='\t')) k.pop_back();
        while (!v.empty() && (v.front()==' '||v.front()=='\t')) v.erase(v.begin());
        auto wl = lowerWLocal(toWLoose(k));
        auto wv = trimWLocal(toWLoose(v));
        if (wl == L"name") m.name = wv;
        else if (wl == L"id") m.id = wv;
        else if (wl == L"author") m.author = wv;
        else if (wl == L"version") m.version = wv;
        else if (wl == L"type") m.type = wv;
        else if (wl == L"status") m.status = wv;
        else if (wl == L"description") m.description = wv;
        else if (wl == L"applies") m.applies = wv;
        else if (wl == L"min_version" || wl == L"minversion") m.minVersion = wv;
        else if (wl == L"homepage" || wl == L"url") m.homepage = wv;
    }
    m.enabled = isModEnabled(settings, m);
    m.valid = !m.name.empty() && !m.id.empty();
    return m;
}

std::vector<ModManifest> scanMods(const Settings& settings) {
    std::vector<ModManifest> out;
    std::error_code ec;
    fs::path md = fs::path(modsDirPath());
    fs::create_directories(md, ec);
    if (!fs::exists(md, ec)) return out;
    for (auto& e : fs::directory_iterator(md, ec)) {
        if (ec) break;
        std::error_code efile;
        if (!e.is_regular_file(efile) || efile) continue;
        const std::wstring fname = lowerWLocal(e.path().filename().wstring());
        // Bot receipts are verification metadata, not mods — never list or
        // validate them as manifests (otherwise Doctor reports false alarms).
        if (fname.size() >= 8 && fname.compare(fname.size() - 8, 8, L".bot.txt") == 0) continue;
        auto ext = lowerWLocal(e.path().extension().wstring());
        if (ext == L".aegismod" || ext == L".mod" || ext == L".txt") {
            out.push_back(loadModManifest(e.path(), settings));
        }
    }
    std::sort(out.begin(), out.end(), [](const ModManifest& a, const ModManifest& b){ return lowerWLocal(a.name) < lowerWLocal(b.name); });
    return out;
}

static bool isSafeId(const std::wstring& id) {
    if (id.empty() || id.size() > 64) return false;
    for (wchar_t c : id) {
        bool ok = (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_'||c==L'.';
        if (!ok) return false;
    }
    return true;
}

static bool versionLooksOk(const std::wstring& v) {
    if (v.empty()) return false;
    for (wchar_t c : v) {
        if (!((c>=L'0'&&c<=L'9')||c==L'.'||c==L'-'||c==L'_'||c==L'a'||c==L'z'||c==L'A'||c==L'Z')) return false;
    }
    return v.size() <= 32;
}

ModVerifyResult verifyMod(const ModManifest& m, bool strictPublish) {
    ModVerifyResult r;
    std::error_code ec;
    if (m.path.empty() || !fs::exists(fs::path(m.path), ec)) {
        r.ok = false; r.errors.push_back(L"Mod file not found: " + m.path); return r;
    }
    auto fsz = fs::file_size(fs::path(m.path), ec);
    if (!ec && fsz > 256 * 1024) {
        r.warnings.push_back(L"Manifest is large (>256KB); keep mods lightweight");
        if (strictPublish) { r.ok = false; r.errors.push_back(L"Publish check failed: manifest too large"); }
    }
    if (m.name.empty()) { r.ok = false; r.errors.push_back(L"Missing required field: name"); }
    if (m.id.empty()) { r.ok = false; r.errors.push_back(L"Missing required field: id"); }
    else if (!isSafeId(m.id)) { r.ok = false; r.errors.push_back(L"Invalid id (allowed: letters, digits, . _ -; max 64 chars)"); }
    if (m.version.empty()) { r.ok = false; r.errors.push_back(L"Missing required field: version"); }
    else if (!versionLooksOk(m.version)) { r.ok = false; r.errors.push_back(L"Invalid version format"); }
    if (m.type.empty()) r.warnings.push_back(L"Missing field: type (recommended)");
    if (m.description.empty()) r.warnings.push_back(L"Missing field: description (recommended)");
    if (m.author.empty()) r.warnings.push_back(L"Missing field: author (recommended)");
    if (m.status.empty()) r.info.push_back(L"Status not set (will be treated as unknown)");
    else {
        auto st = lowerWLocal(m.status);
        if (!(st==L"draft"||st==L"in-development"||st==L"stable"||st==L"deprecated"||st==L"verified")) {
            r.warnings.push_back(L"Unusual status value: " + m.status);
        }
    }
    // Supported actions in this build
    if (!m.applies.empty()) {
        auto a = lowerWLocal(m.applies);
        bool supported = (a==L"ui_theme:white"||a==L"ui_theme:black"||a==L"ui_theme:default");
        if (!supported) {
            r.warnings.push_back(L"applies action is not natively supported by this build: " + m.applies);
            r.info.push_back(L"Supported now: ui_theme:white | ui_theme:black | ui_theme:default");
            if (strictPublish) { r.ok = false; r.errors.push_back(L"Publish check failed: unsupported applies action for current runtime"); }
        }
    } else {
        r.info.push_back(L"No applies action declared (manifest-only mod/tweak scaffold)");
    }
    // Basic safety checks (manifest-level)
    std::ifstream f(fs::path(m.path), std::ios::binary);
    if (f) {
        std::string line;
        int lines = 0;
        while (std::getline(f, line)) {
            if (++lines > 2000) { r.warnings.push_back(L"Manifest has many lines; consider simplifying"); break; }
            std::string low = line;
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (low.find("<script") != std::string::npos || low.find("powershell -enc") != std::string::npos || low.find("curl | sh") != std::string::npos) {
                r.warnings.push_back(L"Potentially dangerous content pattern detected in manifest text");
                if (strictPublish) { r.ok = false; r.errors.push_back(L"Publish check failed: suspicious content pattern"); }
            }
        }
    }
    if (strictPublish) {
        if (m.description.size() < 10) r.warnings.push_back(L"Publish recommendation: provide a clearer description");
        if (m.author.empty()) r.warnings.push_back(L"Publish recommendation: set author");
    }
    if (!r.errors.empty()) r.ok = false;
    return r;
}

ModVerifyResult verifyAllMods(const Settings& settings, bool strictPublish) {
    ModVerifyResult all;
    auto mods = scanMods(settings);
    if (mods.empty()) {
        all.info.push_back(L"No mods found in " + modsDirPath());
        return all;
    }
    for (auto& m : mods) {
        auto r = verifyMod(m, strictPublish);
        if (!r.ok) all.ok = false;
        for (auto& e : r.errors) all.errors.push_back(L"[" + m.file + L"] " + e);
        for (auto& w : r.warnings) all.warnings.push_back(L"[" + m.file + L"] " + w);
        // keep info concise
        for (auto& i : r.info) if (all.info.size() < 20) all.info.push_back(L"[" + m.file + L"] " + i);
    }
    return all;
}

bool applyMod(const ModManifest& m, Settings& settings, std::wstring& message) {
    auto vr = verifyMod(m, false);
    if (!vr.ok) {
        message = L"Mod failed verification; fix issues before applying.";
        return false;
    }
    if (!isModEnabled(settings, m)) {
        message = L"Mod is disabled. Enable it first.";
        return false;
    }
    auto a = lowerWLocal(m.applies);
    if (a == L"ui_theme:white" || a == L"ui_theme:black" || a == L"ui_theme:default") {
        settings.uiTheme = a.substr(std::wstring(L"ui_theme:").size());
        saveSettings(settings);
        Logger::instance().setTheme(settings.uiTheme);
        message = L"Applied tweak: " + m.name + L" (ui theme = " + settings.uiTheme + L")";
        return true;
    }
    message = L"No supported runtime apply action for this mod in current build (manifest recorded/verified).";
    return false;
}

static std::wstring nowStamp() {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    wchar_t buf[32];
    wcsftime(buf, 32, L"%Y%m%d-%H%M%S", &tm);
    return buf;
}

bool publishMod(const ModManifest& m, bool strict, std::wstring& reportPath, std::wstring& summary) {
    auto vr = verifyMod(m, strict);

    // id/version приходят из манифеста (потенциально враждебного): не даём им
    // выйти за пределы mods-publish через ../ или абсолютный путь.
    auto fileSafe = [](std::wstring s) {
        for (auto& c : s) {
            if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|' || c < 32)
                c = L'_';
        }
        if (s.empty()) s = L"mod";
        return s;
    };

    std::error_code ec;
    fs::path pubDir = fs::path(aegisDirLocal()) / L"mods-publish";
    fs::create_directories(pubDir, ec);
    if (ec) { summary = L"Could not create publish directory."; return false; }
    fs::path report = pubDir / (fileSafe(modKey(m)) + L"-publish-" + nowStamp() + L".txt");
    reportPath = report.wstring();

    std::ofstream out(report, std::ios::binary | std::ios::trunc);
    if (!out) { summary = L"Could not write publish report."; return false; }

    out << "Aegis Mod Publish Verification Report\n";
    out << "App version: " << wToUtf8(ae::kVersion) << "\n";
    out << "Mod: " << wToUtf8(m.name) << " (" << wToUtf8(m.file) << ")\n";
    out << "Strict mode: " << (strict ? "yes" : "no") << "\n";
    out << "Result: " << (vr.ok ? "PASS" : "FAIL") << "\n\n";
    if (!vr.errors.empty()) {
        out << "Errors:\n";
        for (auto& e : vr.errors) out << "- " << wToUtf8(e) << "\n";
        out << "\n";
    }
    if (!vr.warnings.empty()) {
        out << "Warnings:\n";
        for (auto& w : vr.warnings) out << "- " << wToUtf8(w) << "\n";
        out << "\n";
    }
    if (!vr.info.empty()) {
        out << "Info:\n";
        for (auto& i : vr.info) out << "- " << wToUtf8(i) << "\n";
        out << "\n";
    }
    out << "Automated checks performed: manifest fields, id/version format, file size, supported applies action, basic safety pattern scan.\n";
    out << "If PASS, this mod package/manifest is ready for submission/publishing review pipeline.\n";
    out.flush();
    if (!out) { summary = L"Could not write publish report."; return false; }

    // Артефакт публикуем только после успешной проверки.
    std::wstring artifactNote;
    if (vr.ok) {
        fs::path artifact = pubDir / (fileSafe(modKey(m)) + L"-" +
                                      fileSafe(m.version.empty() ? L"0.0.0" : m.version) +
                                      L".aegismod");
        ec.clear();
        fs::copy_file(fs::path(m.path), artifact, fs::copy_options::overwrite_existing, ec);
        if (!ec) artifactNote = L" | Artifact: " + artifact.wstring();
    }

    summary = (vr.ok ? L"Publish verification PASSED" : L"Publish verification FAILED") +
              std::wstring(L". Report: ") + reportPath + artifactNote;
    return vr.ok;
}

// ---------------------------------------------------------------------------
// v6.0 MOD BOT: checks the *content* of a mod (including on_content code),
// then produces a signed-off artifact + approval receipt for release.
// ---------------------------------------------------------------------------

std::wstring approvedModsDir() {
    return (fs::path(aegisDirLocal()) / L"mods-approved").wstring();
}

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::wstring fromAscii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

int compareVersions(const std::wstring& a, const std::wstring& b) {
    auto parts = [](const std::wstring& v) {
        std::vector<int> out;
        std::wstring cur;
        for (wchar_t c : v) {
            if (c >= L'0' && c <= L'9') cur.push_back(c);
            else if (!cur.empty()) { out.push_back(std::stoi(cur)); cur.clear(); }
        }
        if (!cur.empty()) out.push_back(std::stoi(cur));
        return out;
    };
    auto pa = parts(a), pb = parts(b);
    const size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa[i] : 0;
        const int y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool looksBase64Blob(const std::string& s) {
    if (s.size() < 120) return false;
    size_t valid = 0;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') ++valid;
    }
    return valid * 100 / s.size() >= 90;
}

} // namespace

ModBotResult botReviewMod(const ModManifest& m) {
    ModBotResult r;
    auto vr = verifyMod(m, true);
    if (!vr.ok) {
        r.reason = L"strict verification failed";
        for (const auto& e : vr.errors) r.findings.push_back(L"ERROR: " + e);
        return r;
    }

    // Bot only signs off mature mods.
    const std::wstring st = lowerWLocal(m.status);
    if (st != L"stable" && st != L"verified") {
        r.reason = L"bot publishes only status=stable or status=verified mods";
        r.findings.push_back(L"set status=stable (or verified) in the manifest");
        return r;
    }
    if (!m.minVersion.empty() && compareVersions(m.minVersion, kVersion) > 0) {
        r.reason = L"mod requires a newer Aegis (" + m.minVersion + L")";
        return r;
    }

    std::ifstream f(fs::path(m.path), std::ios::binary);
    if (!f) { r.reason = L"cannot read mod file"; return r; }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.size() > 2 * 1024 * 1024) {
        r.reason = L"mod content exceeds 2 MiB bot limit";
        return r;
    }

    // High-signal dangerous constructs. Only *code* lines are checked:
    // comment lines (# / ;) are reported as info so example snippets don't
    // silently hide a real payload further down.
    static const char* kDanger[] = {
        "<script", "javascript:", "powershell -enc", "powershell -e ",
        "frombase64string", "invoke-expression", "iex(", "downloadstring",
        "curl | sh", "wget | sh", "curl http", "wget http",
        "rm -rf /", "format c:", "del /f /s /q", "schtasks /create",
        "reg add hkey_current_user\\software\\microsoft\\windows\\currentversion\\run",
        "os.system", "subprocess.", "eval(", "exec(", "system(",
        "createprocess", "virtualalloc", "writeprocessmemory", "shellexecute",
        "wscript.shell", "rundll32", "certutil -decode", "bitsadmin",
        "/dev/tcp", "nc -e", "base64 -d", "bash -c", "sh -c",
        "on_content", "payload=", "script=", "code="
    };

    bool blocked = false;
    size_t lineNo = 0;
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        ++lineNo;
        if (line.size() > 16384) { r.reason = L"line " + std::to_wstring(lineNo) + L" is too long"; return r; }
        const std::string low = lowerAscii(line);
        const bool comment = !low.empty() && (low[0] == '#' || low[0] == ';');
        if (looksBase64Blob(line)) {
            r.findings.push_back(L"embedded base64-like blob at line " + std::to_wstring(lineNo));
            if (!comment) blocked = true;
            continue;
        }
        for (const char* pat : kDanger) {
            if (low.find(pat) == std::string::npos) continue;
            const std::wstring msg = L"pattern '" + fromAscii(pat) + L"' at line " + std::to_wstring(lineNo);
            if (comment) r.findings.push_back(L"INFO: " + msg);
            else { r.findings.push_back(L"ERROR: " + msg); blocked = true; }
        }
        // External fetch in code is never allowed; homepage/comment fields are fine.
        if (!comment && (low.find("https://") != std::string::npos ||
                         low.find("http://") != std::string::npos) &&
            low.find("homepage=") == std::string::npos &&
            low.find("url=") == std::string::npos) {
            r.findings.push_back(L"ERROR: external URL in code at line " + std::to_wstring(lineNo));
            blocked = true;
        }
    }

    if (blocked) {
        r.reason = L"bot found unsafe code content — mod rejected";
        return r;
    }

    r.approved = true;
    r.reason = L"bot checks passed (manifest + on_content code scan)";
    if (r.findings.empty()) r.findings.push_back(L"no dangerous patterns, no embedded payloads");
    return r;
}

ModBotResult botPublishMod(const ModManifest& m) {
    ModBotResult r = botReviewMod(m);
    if (!r.approved) return r;

    std::error_code ec;
    const fs::path dir(approvedModsDir());
    fs::create_directories(dir, ec);
    if (ec) { r.approved = false; r.reason = L"cannot create approved mods folder"; return r; }

    auto safeName = [](std::wstring s) {
        for (wchar_t& c : s) {
            if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
        }
        return s;
    };
    const std::wstring base = safeName(m.id.empty() ? m.name : m.id) + L"-" +
                              safeName(m.version.empty() ? L"0.0.0" : m.version);
    const fs::path artifact = dir / (base + L".aegismod");
    fs::copy_file(fs::path(m.path), artifact, fs::copy_options::overwrite_existing, ec);
    if (ec) { r.approved = false; r.reason = L"cannot copy mod artifact"; return r; }

    const std::string hash = computeSha256OfFile(artifact.wstring());
    const fs::path receipt = dir / (base + L".bot.txt");
    std::ofstream out(receipt, std::ios::binary | std::ios::trunc);
    if (!out) { r.approved = false; r.reason = L"cannot write bot receipt"; return r; }
    out << "Aegis Mod Bot Approval\n";
    out << "App version: " << wToUtf8(kVersion) << "\n";
    out << "Mod: " << wToUtf8(m.name) << " (id=" << wToUtf8(m.id) << ", v" << wToUtf8(m.version) << ")\n";
    out << "SHA256: " << hash << "\n";
    out << "Checks: strict manifest verification, status/min_version, deep on_content code scan\n";
    out << "Result: APPROVED\n\nFindings:\n";
    for (const auto& f : r.findings) out << "- " << wToUtf8(f) << "\n";
    out.flush();
    if (!out) { r.approved = false; r.reason = L"cannot finish bot receipt"; return r; }

    r.artifact = artifact.wstring();
    r.receipt = receipt.wstring();
    return r;
}

bool uploadModArtifact(const std::wstring& tag, const std::wstring& artifact, std::wstring& error) {
    if (tag.empty() || artifact.empty()) { error = L"missing tag or artifact"; return false; }
#ifdef _WIN32
    std::wstring cmd = L"gh release upload \"" + tag + L"\" \"" + artifact + L"\" --clobber 2>&1";
    FILE* pipe = _wpopen(cmd.c_str(), L"r");
#else
    const std::string c = "gh release upload \"" + std::string(tag.begin(), tag.end()) +
                          "\" \"" + std::string(artifact.begin(), artifact.end()) + "\" --clobber 2>&1";
    FILE* pipe = popen(c.c_str(), "r");
#endif
    if (!pipe) { error = L"cannot run gh (is GitHub CLI installed?)"; return false; }
    char buf[512] = {};
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    if (output.find("Uploaded") == std::string::npos &&
        output.find("uploaded") == std::string::npos &&
        output.find("done") == std::string::npos) {
        error = L"gh release upload failed: " + std::wstring(output.begin(), output.end());
        return false;
    }
    return true;
}

std::wstring createModScaffold(const std::wstring& nameIn, std::wstring& outPath, std::wstring& err) {
    std::error_code ec;
    fs::create_directories(fs::path(modsDirPath()), ec);
    std::wstring name = trimWLocal(nameIn);
    std::wstring id = name;
    for (auto& c : id) {
        if (c==L' '||c==L'\\'||c==L'/') c=L'-';
        c = towlower(c);
    }
    // sanitize id
    std::wstring safe;
    for (auto c : id) {
        bool ok = (c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_'||c==L'.';
        if (ok) safe.push_back(c);
    }
    if (safe.empty()) { err = L"Invalid mod name/id"; return L""; }
    fs::path out = fs::path(modsDirPath()) / (safe + L".aegismod");
    ec.clear();
    if (fs::exists(out, ec) && !ec) { err = L"Mod already exists: " + out.wstring(); return L""; }

    // Имя пишется в манифест как одна строка: перевод строки сломал бы формат.
    std::wstring display = name.empty() ? safe : name;
    for (auto& c : display) if (c == L'\r' || c == L'\n') c = L' ';

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) { err = L"Could not create mod file"; return L""; }
    f << "name=" << wToUtf8(display) << "\n";
    f << "id=" << wToUtf8(safe) << "\n";
    f << "author=\n";
    f << "version=0.1.0\n";
    f << "type=tweak\n";
    f << "status=draft\n";
    f << "description=\n";
    f << "applies=\n";
    f << "min_version=\n";
    f << "homepage=\n";
    f << "\n# Aegis mod/tweak manifest (v5 scaffold)\n";
    f << "# Required for publish: name,id,version (+ recommended author,type,description,status)\n";
    f << "# Supported runtime applies (current build): ui_theme:white | ui_theme:black | ui_theme:default\n";
    f << "# Use: aegis mod verify " << wToUtf8(safe) << "   and   aegis mod publish " << wToUtf8(safe) << "\n";
    f.flush();
    if (!f) {
        f.close();
        std::error_code e2;
        fs::remove(out, e2);
        err = L"Could not write mod file";
        return L"";
    }
    outPath = out.wstring();
    return safe;
}

} // namespace ae
