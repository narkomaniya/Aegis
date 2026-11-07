// quarantine.cpp
#include "quarantine.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ae {

static std::wstring stamp() {
    auto now = std::chrono::system_clock::now();
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

bool Quarantine::moveIn(const std::wstring& full, const std::wstring& reason, bool copyOnly) {
    std::error_code ec;
    fs::path src(full);
    if (!fs::exists(src, ec) || ec) return false;

    // Карантинная запись: папка вида <root>\20261107_142233_<имя>
    fs::path folder = fs::path(root_) /
                      (stamp() + L"_" + src.filename().wstring());
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

} // namespace ae
