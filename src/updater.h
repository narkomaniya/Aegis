#pragma once
// updater.h — `aegis update`: сверяем свою версию с последним релизом GitHub
// и при наличии обновления сами заменяем свой бинарь.
#include <string>

namespace ae {

// Формат ответа: 0 = актуально, 1 = успешно обновились, 2 = не удалось
int runUpdate(bool assumeYes);

// удалить остаток *.old от прошлого самообновления (best effort)
void cleanupOldBinary();

// путь к собственному бинарю (используется апдейтером и автозапуском)
std::wstring selfPath();

} // namespace ae
