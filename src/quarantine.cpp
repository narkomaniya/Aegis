// quarantine.cpp
#include "quarantine.h"
#include "logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <locale>
#include <codecvt>
#endif

namespace fs = std::filesystem;

namespace ae {

// UTF-8 (как в meta.txt) -> широкая строка
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
#else
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
#endif
}

static std::string trimBytes(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring stamp() {    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    wchar_t buf[64] = {};
    wcsftime(buf, 32, L"%Y%m%d_%H%M%S", &lt);
    return buf;
}

bool Quarantine::moveIn(const std::wstring& full, const std::wstring& reason, bool copyOnly, const std::wstring& backupRoot) {
    std::error_code ec;
    fs::path src(full);
    if (!fs::exists(src, ec) || ec) return false;

    // Optional backup copy before taking the file (best effort, does not block quarantine)
    if (!backupRoot.empty() && !copyOnly) {
        std::error_code bec;
        fs::path bdir = fs::path(backupRoot) / (stamp() + L"_backup");
        for (int i = 1; fs::exists(bdir, bec) && i < 1000; ++i) { bec.clear(); bdir = fs::path(backupRoot) / (stamp() + L"_backup_" + std::to_wstring(i)); }
        bec.clear();
        fs::create_directories(bdir, bec);
        if (!bec) {
            fs::copy_file(src, bdir / src.filename(), fs::copy_options::overwrite_existing, bec);
        }
    }

    // Карантинная запись: папка вида <root>\20261107_142233_<имя>.
    // Папку "занимаем" атомарно (create_directory), поэтому два потока/процесса
    // не могут писать в одну и ту же запись и затирать друг друга.
    ec.clear();
    fs::create_directories(fs::path(root_), ec);
    if (ec) return false;

    fs::path folder;
    for (int i = 0; i < 1000; ++i) {
        ec.clear();
        fs::path cand = fs::path(root_) /
                        (stamp() + L"_" + (i ? std::to_wstring(i) + L"_" : L"") +
                         src.filename().wstring());
        if (fs::create_directory(cand, ec)) { folder = cand; break; }
        if (ec) return false;       // настоящая ошибка (права, длина пути) — не крутимся дальше
    }
    if (folder.empty()) return false;

    // meta.txt пишем ДО переноса файла: если процесс упадёт между шагами,
    // останется запись без файла (её видно и можно почистить), а не файл без
    // записи, который невозможно восстановить на место.
    fs::path metaTmp = folder / L"meta.txt.tmp";
    {
        std::ofstream meta(metaTmp, std::ios::binary | std::ios::trunc);
        if (!meta) { std::error_code c; fs::remove_all(folder, c); return false; }
        auto toUtf8 = [](const std::wstring& w) {
            if (w.empty()) return std::string{};
#ifdef _WIN32
            int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                        nullptr, 0, nullptr, nullptr);
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                s.data(), n, nullptr, nullptr);
            return s;
#else
            return std::string(w.begin(), w.end());
#endif
        };
        meta << "Aegis quarantine record\n";
        meta << "reason  : " << toUtf8(reason) << "\n";
        meta << "from    : " << toUtf8(full) << "\n";
        meta << "taken at: " << toUtf8(stamp()) << "\n";
        meta << "mode    : " << (copyOnly ? "copy (emergency)" : "move") << "\n";
        meta.close();
        if (!meta) { std::error_code c; fs::remove_all(folder, c); return false; }
    }
    ec.clear();
    fs::rename(metaTmp, folder / L"meta.txt", ec);
    if (ec) { std::error_code c; fs::remove_all(folder, c); return false; }

    // Файл кладём в подпапку data/, иначе файл с именем "meta.txt" затирал бы
    // собственную мета-запись (и терялся безвозвратно).
    fs::path dataDir = folder / L"data";
    ec.clear();
    fs::create_directories(dataDir, ec);
    if (ec) { std::error_code c; fs::remove_all(folder, c); return false; }

    fs::path dst = dataDir / src.filename();
    bool placed = false;

    if (copyOnly) {
        ec.clear();
        fs::copy_file(src, dst, fs::copy_options::none, ec);
        placed = !ec;
    } else {
        ec.clear();
        fs::rename(src, dst, ec);  // быстрый путь: та же файловая система
        if (ec) {
            // резерв: скопировать и убрать исходник
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::none, ec);
            if (!ec) {
                std::error_code ec2;
                fs::remove(src, ec2);
                if (ec2) {
                    // файл остался на месте — не врём, что он в карантине:
                    // чистим частичную запись и сообщаем о неудаче.
                    ae::warn(L"quarantine: could not remove source, file is still in place: " +
                             src.wstring());
                    ec.clear();
                    fs::remove_all(folder, ec);
                    return false;
                }
                placed = true;
            }
        } else {
            placed = true;
        }
    }

    if (!placed) {
        std::error_code c;
        fs::remove_all(folder, c);
        return false;
    }
    return true;
}

std::vector<std::wstring> Quarantine::list() const {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(root_), ec)) {
        if (e.is_directory()) out.push_back(e.path().filename().wstring());
    }
    return out;
}

unsigned long long Quarantine::count() const {
    return list().size();
}

// Прочитать все записи карантина по их meta.txt
std::vector<Quarantine::Record> Quarantine::records() const {
    std::vector<Record> out;
    std::error_code ec;

    for (const auto& e : fs::directory_iterator(fs::path(root_), ec)) {
        if (ec || !e.is_directory()) continue;

        Record r;
        r.folder = e.path().wstring();

        // сам забранный файл: новый формат — <запись>/data/<файл>,
        // старый (до 5.1) — <запись>/<файл>. meta.txt пропускаем всегда.
        std::error_code ecIn;
        auto pickPayload = [&](const fs::path& dir) -> bool {
            std::error_code ecIt;
            for (const auto& f : fs::directory_iterator(dir, ecIt)) {
                if (ecIt) return false;
                if (!f.is_regular_file(ecIt)) continue;
                if (f.path().filename() == L"meta.txt") continue;
                r.file = f.path().filename().wstring();
                return true;
            }
            return false;
        };
        if (!pickPayload(e.path() / L"data")) {
            (void)ecIn;
            pickPayload(e.path());
        }

        std::ifstream meta(e.path() / L"meta.txt", std::ios::binary);
        if (meta) {
            std::string line;
            while (std::getline(meta, line)) {
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string key = trimBytes(line.substr(0, colon));
                std::string val = trimBytes(line.substr(colon + 1));
                // первое вхождение ключа выигрывает: повторный "from:" не должен
                // переопределять настоящее место, откуда файл забрали
                if      (key == "reason")   { if (r.reason.empty()) r.reason = utf8ToWide(val); }
                else if (key == "from")     { if (r.from.empty())   r.from   = utf8ToWide(val); }
                else if (key == "taken at") { if (r.time.empty())   r.time   = utf8ToWide(val); }
                else if (key == "mode")     { if (r.mode.empty())   r.mode   = utf8ToWide(val); }
            }
        }

        if (!r.file.empty()) out.push_back(r);
    }
    return out;
}

// Вернуть файл на прежнее место и убрать запись
bool Quarantine::restore(const Record& rec) {
    std::error_code ec;
    fs::path folder(rec.folder);

    // новый формат: payload в data/, старый — прямо в папке записи
    fs::path src = folder / L"data" / fs::path(rec.file);
    if (!fs::exists(src, ec) || ec) {
        ec.clear();
        src = folder / fs::path(rec.file);
        if (!fs::exists(src, ec) || ec) return false;
    }

    // "from" читается из meta.txt, которую может подменить любой, у кого есть
    // доступ к ~/.aegis: разрешаем только абсолютные пути без управляющих байтов.
    if (rec.from.empty() || rec.from.find_first_of(L"\r\n\t") != std::wstring::npos) return false;
    fs::path dest = fs::path(rec.from);
    if (!dest.is_absolute()) return false;

    // путь мог "сдохнуть" с момента карантина (файлы уехали в OneDrive и т.п.) —
    // пересоздаём родителя, иначе rename падает с Access denied
    {
        std::error_code ecP;
        if (!dest.parent_path().empty())
            fs::create_directories(dest.parent_path(), ecP);
        if (ecP) return false;
    }

    // Оригинал уже занят — не затираем живой файл, подбираем реально свободное имя.
    {
        std::error_code ecE;
        bool exists = fs::exists(dest, ecE);
        if (ecE) return false;   // не смогли проверить — лучше честно упасть
        if (exists) {
            const std::wstring parent = dest.parent_path().wstring();
            const std::wstring stem = dest.stem().wstring();
            const std::wstring ext  = dest.extension().wstring();
            bool found = false;
            for (int i = 1; i < 1000 && !found; ++i) {
                std::wstring name = stem + L" (restored" +
                                    (i > 1 ? L" " + std::to_wstring(i) : L"") + L")" + ext;
                fs::path cand = parent.empty() ? fs::path(name) : fs::path(parent) / name;
                std::error_code ecC;
                if (!fs::exists(cand, ecC)) {
                    if (ecC) return false;
                    dest = cand;
                    found = true;
                }
            }
            if (!found) return false;
        }
    }

    ec.clear();
    fs::rename(src, dest, ec);   // быстрый путь: та же файловая система
    if (ec) {
        // резерв: скопировать и убрать исходник (без overwrite — не затираем чужое)
        ec.clear();
        fs::copy_file(src, dest, fs::copy_options::none, ec);
        if (ec) return false;
        std::error_code ec2;
        fs::remove(src, ec2);
        if (ec2) return false;   // копия есть, но запись оставляем — не теряем данные
    }

    // запись отслужила
    std::error_code ec3;
    fs::remove_all(folder, ec3);
    return true;
}

} // namespace ae
