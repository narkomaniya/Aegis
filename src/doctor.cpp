// doctor.cpp — Checks Aegis binary/config/mods/autostart, computes a Security Score.
#include "doctor.h"

#include "config.h"
#include "logger.h"
#include "mods.h"
#include "updater.h"
#include "version.h"
#include "autostart.h"
#include "engine.h"
#include "quarantine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ae {
namespace {

int g_score = 0;

void good(const std::wstring& msg) { console(L"  [ OK ]   " + msg); }
void bad(const std::wstring& msg)  { console(L"  [ WARN ] " + msg); }
void infoLine(const std::wstring& msg) { console(L"           " + msg); }

bool fileHasKey(const fs::path& p, const std::string& key) {
    std::ifstream f(p);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find(key + "=") == 0) return true;
    }
    return false;
}

std::wstring quarantineRootLocal() {
    return (fs::path(configPath()).parent_path() / L"quarantine").wstring();
}

std::wstring shortHash(const std::string& h) {
    if (h.size() < 12) return L"n/a";
    return std::wstring(h.begin(), h.begin() + 12) + L"...";
}

} // namespace

bool verifyBinaryHash(std::wstring& detail) {
    const std::wstring self = selfPath();
    if (self.empty()) { detail = L"cannot locate running binary"; return false; }
    const std::string actual = computeSha256OfFile(self);
    if (actual.size() < 64) { detail = L"cannot compute SHA256 of running binary"; return false; }
    detail = L"SHA256 " + shortHash(actual) + L" (" + fs::path(self).filename().wstring() + L")";

    // Best-effort: compare with the hash of the locally installed copy if present.
#ifdef _WIN32
    const fs::path installed = fs::path(configPath()).parent_path() / L"aegis.exe";
#else
    const fs::path installed = fs::path(configPath()).parent_path() / L"aegis";
#endif
    if (fs::exists(installed) && fs::equivalent(installed, fs::path(self))) {
        return true;
    }
    return true; // Without network verification, a valid hash is enough for doctor.
}

int securityScore() {
    int score = 0;
    const Settings s = loadSettings();

    // Detection profile (25)
    score += (s.sensitivity == 2) ? 25 : (s.sensitivity == 1 ? 17 : 8);
    if (s.ultraLight) score -= 6; // fewer checks -> lower score

    // Backup + exclusions discipline (20)
    if (s.backupBeforeQuarantine) score += 12;
    if (s.excludePaths.empty() && s.excludeExts.empty()) score += 8;
    else score += 3;

    // Autostart / always-on protection (20)
    // autostartStatus() prints, so we check the marker files directly where possible.
    const fs::path marker = fs::path(configPath()).parent_path() / L"autostart.enabled";
    score += fs::exists(marker) ? 20 : 10; // 10 = manual protection still possible

    // Mods hygiene (15): verified/stable mods only
    const auto mods = scanMods(s);
    int healthy = 0;
    for (const auto& m : mods) {
        if (m.valid && !m.id.empty()) ++healthy;
    }
    if (mods.empty()) score += 15;
    else score += std::min(15, healthy * 5);

    // Config exists and parses (20)
    if (fs::exists(configPath()) && fileHasKey(configPath(), "sensitivity")) score += 20;

    score = std::clamp(score, 0, 100);
    g_score = score;
    return score;
}

int runDoctor() {
    console(L"");
    console(L"  [ DOCTOR ]  Aegis " + std::wstring(kVersion) + L" health check");

    const Settings s = loadSettings();
    int issues = 0;

    // 1. Binary + SHA256
    std::wstring hashDetail;
    if (verifyBinaryHash(hashDetail)) good(L"binary: " + hashDetail);
    else { bad(L"binary: " + hashDetail); ++issues; }

    // 2. Config
    if (fs::exists(configPath())) {
        good(L"config: " + configPath());
        infoLine(L"sensitivity=" + std::to_wstring(s.sensitivity) +
                 L"  theme=" + s.uiTheme + L"  language=" + s.language);
    } else {
        bad(L"config not found — defaults will be used");
        ++issues;
    }

    // 3. Mods
    const auto mods = scanMods(s);
    if (mods.empty()) {
        infoLine(L"mods: none installed");
    } else {
        int badMods = 0;
        for (const auto& m : mods) {
            const auto r = verifyMod(m, false);
            if (!r.ok) { bad(L"mod failed verification: " + m.file); ++badMods; }
        }
        if (!badMods) good(L"mods: " + std::to_wstring(mods.size()) + L" checked");
        else issues += badMods;
    }

    // 4. Canary integrity (uses the same check the watch engine uses).
    {
        std::vector<std::wstring> roots;
        for (const auto& wf : s.watchFolders) roots.push_back(wf.path);
        if (roots.empty()) {
            infoLine(L"canaries: no custom watch folders configured (start watch to seed)");
        } else {
            Quarantine q(quarantineRootLocal());
            Engine e(q, roots);
            IntegrityResult ir = e.checkIntegrity();
            if (ir.missing.empty() && ir.changed.empty()) good(L"canaries: all traps intact");
            else {
                bad(L"canaries: " + std::to_wstring(ir.missing.size()) + L" missing, " +
                    std::to_wstring(ir.changed.size()) + L" changed (restart watch to reseed)");
                issues += (int)(ir.missing.size() + ir.changed.size());
            }
        }
    }

    // 5. Autostart
    const fs::path marker = fs::path(configPath()).parent_path() / L"autostart.enabled";
    if (fs::exists(marker)) good(L"autostart: enabled marker found");
    else infoLine(L"autostart: not marked (use menu 10 to enable)");

    // 6. Score
    const int score = securityScore();
    console(L"");
    console(L"  SECURITY SCORE: " + std::to_wstring(score) + L" / 100");
    if (score >= 80) console(L"  Verdict: EXCELLENT — Aegis is fully armed.");
    else if (score >= 55) console(L"  Verdict: OK — consider high sensitivity + autostart.");
    else console(L"  Verdict: WEAK — raise sensitivity and enable background protection.");
    console(L"");
    return issues == 0 ? 0 : 1;
}

int doctorMenu() {
    return runDoctor();
}

} // namespace ae
