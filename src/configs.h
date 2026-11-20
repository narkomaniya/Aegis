#pragma once
// configs.h — v6.0 «Configs»: обмен конфигурациями по ссылке.
// Импорт никогда не пишет сразу в config.txt: сначала скачивание во временный
// файл, whitelist-валидация, diff, бэкап текущего конфига и только потом замена.
#include <string>
#include <vector>

namespace ae {

struct ConfigImportResult {
    bool ok = false;
    std::wstring message;
    std::vector<std::wstring> diff;
};

std::wstring configsDir();
std::vector<std::wstring> listPresets();

bool savePreset(const std::wstring& name, std::wstring& error);
bool applyPreset(const std::wstring& name, std::wstring& error);

// `link` — https/http URL или локальный путь к файлу конфига.
ConfigImportResult importConfigLink(const std::wstring& link);

// Публикует локальный preset через `gh gist create` (нужен scope gist).
bool publishPreset(const std::wstring& name, std::wstring& url, std::wstring& error);

void configsMenu();

} // namespace ae
