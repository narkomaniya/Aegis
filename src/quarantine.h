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

    // Полная запись карантина: чего, откуда и почему забрали
    struct Record {
        std::wstring folder;   // путь к папке-записи
        std::wstring file;     // имя забранного файла
        std::wstring from;     // где файл лежал до карантина
        std::wstring reason;   // что в нём нашли подозрительного
        std::wstring time;     // когда забрали
        std::wstring mode;     // move / copy (emergency)
    };

    std::vector<Record> records() const;         // читает meta.txt каждой записи
    bool restore(const Record& rec);             // вернуть файл на прежнее место

private:
    std::wstring root_;
};

} // namespace ae
