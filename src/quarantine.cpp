// quarantine.cpp
#include "quarantine.h"

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

    // Карантинная запись: папка вида <root>\20261107_142233_<имя>
    // Имена могут совпасть (два файла в одну секунду) — добавляем _1, _2, ...
    fs::path folder = fs::path(root_) /
                      (stamp() + L"_" + src.filename().wstring());
    for (int i = 1; fs::exists(folder, ec) && !ec && i < 1000; ++i) {
        ec.clear();
        folder = fs::path(root_) /
                 (stamp() + L"_" + std::to_wstring(i) + L"_" + src.filename().wstring());
    }
    ec.clear();
    fs::create_directories(folder, ec);
    if (ec) return false;

    fs::path dst = folder / src.filename();
    bool placed = false;

    if (copyOnly) {
        ec.clear();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        placed = !ec;
    } else {
        ec.clear();
        fs::rename(src, dst, ec);  // быстрый путь: та же файловая система
        if (ec) {
            // резерв: скопировать и убрать исходник
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::error_code ec2;
                fs::remove(src, ec2);
                placed = true;
            }
        } else {
            placed = true;
        }
    }
    if (!placed) return false;

    // meta.txt — вся история: что забрали и почему
    std::ofstream meta(folder / L"meta.txt", std::ios::binary);
    if (meta) {
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

        // сам забранный файл: любой элемент записи, кроме meta.txt
        std::error_code ecIn;
        for (const auto& f : fs::directory_iterator(e.path(), ecIn)) {
            if (ecIn) break;
            if (f.path().filename() == L"meta.txt") continue;
            r.file = f.path().filename().wstring();
            break;
        }

        std::ifstream meta(e.path() / L"meta.txt", std::ios::binary);
        if (meta) {
            std::string line;
            while (std::getline(meta, line)) {
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string key = trimBytes(line.substr(0, colon));
                std::string val = trimBytes(line.substr(colon + 1));
                if      (key == "reason")  r.reason = utf8ToWide(val);
                else if (key == "from")    r.from   = utf8ToWide(val);
                else if (key == "taken at") r.time  = utf8ToWide(val);
                else if (key == "mode")    r.mode   = utf8ToWide(val);
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
    fs::path src = folder / fs::path(rec.file);
    if (!fs::exists(src, ec) || ec) return false;

    fs::path dest = fs::path(rec.from);
    if (dest.empty()) return false;

    // путь мог "сдохнуть" с момента карантина (файлы уехали в OneDrive и т.п.) —
    // пересоздаём родителя, иначе rename падает с Access denied
    {
        std::error_code ecP;
        if (!dest.parent_path().empty())
            fs::create_directories(dest.parent_path(), ecP);
        if (ecP) return false;
    }

    if (fs::exists(dest, ec)) {
        // оригинал уже занят — не затираем живой файл, кладём рядом с меткой
        std::error_code ecE;
        fs::path stem = dest.stem();
        fs::path ext  = dest.extension();
        dest = dest.parent_path() /
               (stem.wstring() + L" (restored)" + ext.wstring());
        (void)ecE;
    }

    ec.clear();
    fs::rename(src, dest, ec);   // быстрый путь: та же файловая система
    if (ec) {
        // резерв: скопировать и убрать исходник
        ec.clear();
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
        std::error_code ec2;
        fs::remove(src, ec2);
    }

    // запись отслужила
    std::error_code ec3;
    fs::remove_all(folder, ec3);
    return true;
}

} // namespace ae
