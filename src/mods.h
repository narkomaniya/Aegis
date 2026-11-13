#pragma once
// mods.h — Aegis mods/tweaks platform (v5 foundation, quality-focused)

#include <filesystem>
#include <string>
#include <vector>

#include "config.h"

namespace ae {

struct ModManifest {
    std::wstring file;          // filename
    std::wstring path;          // full path
    std::wstring name;
    std::wstring id;
    std::wstring author;
    std::wstring version;
    std::wstring type;          // tweak|theme|rules|integration...
    std::wstring status;        // draft|in-development|stable|deprecated
    std::wstring description;
    std::wstring applies;       // supported action, e.g. ui_theme:white
    std::wstring minVersion;    // optional
    std::wstring homepage;
    bool enabled = true;        // runtime enable state (from settings)
    bool valid = false;         // manifest parsed/has required fields
};

struct ModVerifyResult {
    bool ok = true;
    std::vector<std::wstring> errors;
    std::vector<std::wstring> warnings;
    std::vector<std::wstring> info;
};

std::wstring modsDirPath();
std::wstring modsStatePath(); // optional metadata file in ~/.aegis

std::vector<ModManifest> scanMods(const Settings& settings);
ModManifest loadModManifest(const std::filesystem::path& p, const Settings& settings);

ModVerifyResult verifyMod(const ModManifest& m, bool strictPublish = false);
ModVerifyResult verifyAllMods(const Settings& settings, bool strictPublish = false);

bool isModEnabled(const Settings& settings, const ModManifest& m);
void setModEnabled(Settings& settings, const ModManifest& m, bool enabled);

bool applyMod(const ModManifest& m, Settings& settings, std::wstring& message);
bool publishMod(const ModManifest& m, bool strict, std::wstring& reportPath, std::wstring& summary);

std::wstring modKey(const ModManifest& m);
std::wstring createModScaffold(const std::wstring& name, std::wstring& outPath, std::wstring& err);

} // namespace ae
