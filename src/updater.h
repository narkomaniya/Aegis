#pragma once
// updater.h — `aegis update`: сверяем свою версию с последним релизом GitHub
// и при наличии обновления сами заменяем свой бинарь.
#include <string>

namespace ae {

// Формат ответа: 0 = актуально, 1 = успешно обновились, 2 = не удалось
int runUpdate(bool assumeYes);

// Returns newer version tag (e.g. v3.2) if available, else empty. Best-effort, no prompt/install.
std::wstring checkLatestVersion();

// удалить остаток *.old от прошлого самообновления (best effort)
void cleanupOldBinary();

// путь к собственному бинарю (используется апдейтером и автозапуском)
std::wstring selfPath();

// v6.0 doctor: SHA256 helpers for binary integrity checks.
std::string computeSha256OfFile(const std::wstring& pathW);
std::string parseExpectedSha256(const std::string& sums, const std::wstring& assetW);

} // namespace ae
