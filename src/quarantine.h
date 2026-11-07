#pragma once
// quarantine.h — карантин Aegis.
// Каждый забранный файл кладётся в отдельную папку внутри корня карантина:
//   <root>\<время>_<имя файла>\ сам файл + meta.txt (описание: откуда, когда, почему).
#include <string>
#include <vector>

namespace ae {

class Quarantine {
public:
    explicit Quarantine(std::wstring root) : root_(std::move(root)) {}

    const std::wstring& root() const { return root_; }

    // copyOnly = true  -> файл КОПИРУЕТСЯ в карантин (аварийный режим, файл остаётся)
    // copyOnly = false -> файл ПЕРЕМЕЩАЕТСЯ в карантин (с диска исчезает)
    bool moveIn(const std::wstring& full, const std::wstring& reason, bool copyOnly = false);

    std::vector<std::wstring> list() const;  // имена записей карантина
    unsigned long long count() const;

private:
    std::wstring root_;
};

} // namespace ae
