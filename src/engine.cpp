// engine.cpp
#include "engine.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ae {

// Расширения-признаки шифровальщиков
static const wchar_t* kSuspiciousExt[] = {
    L".locked", L".locky",  L".enc",   L".encrypted", L".crypt",  L".crypto",
    L".cryptolocker",       L".cerber",              L".wnry",   L".wcry",
    L".onion",  L".cryp1",  L".osiris", L".zepto",     L".odcodc", L".paymessage",
    L".kraken", L".gravity", L".tevac", L".r4a",       L".lock93", L".party",
};

// Форматы, где высокая энтропия это норма (сжатие/контейнер) — не подозреваем их
static const wchar_t* kCompressedExt[] = {
    L".zip", L".rar", L".7z", L".gz",  L".xz",  L".bz2", L".cab",
    L".jpg", L".jpeg", L".png", L".gif", L".webp", L".mp3", L".mp4",
    L".avi", L".mkv", L".mov", L".exe", L".dll", L".msi", L".ico", L".pdf",
    L".docx", L".xlsx", L".pptx", L".docm", L".xlsm", L".pptm",
};

static std::wstring lower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::towlower);
    return r;
}

static std::wstring extOf(const std::wstring& file) {
    size_t dot = file.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    return lower(file.substr(dot));
}

static bool inList(const std::wstring& ext, const wchar_t** list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (ext == list[i]) return true;
    return false;
}

bool Engine::insideQuarantine(const std::wstring& full) const {
    std::wstring f = lower(full);
    std::wstring q = lower(q_.root());
    if (!q.empty() && q.back() != L'\\') q += L'\\';
    return f.compare(0, q.size(), q) == 0;
}

double Engine::sampleEntropy(const std::wstring& full) const {
    std::error_code ec;
    uintmax_t size = fs::file_size(fs::path(full), ec);
    if (ec || size < 4096) return -1.0;  // мелкие файлы не оцениваем

    std::ifstream f(fs::path(full), std::ios::binary);
    if (!f) return -1.0;

    constexpr size_t N = 8192;
    unsigned char buf[N];
    f.read(reinterpret_cast<char*>(buf), N);
    std::streamsize got = f.gcount();
    if (got < 4096) return -1.0;

    int freq[256] = {};
    for (std::streamsize i = 0; i < got; ++i) freq[buf[i]]++;

    double H = 0.0;
    for (int c : freq) {
        if (c == 0) continue;
        double p = (double)c / (double)got;
        H -= p * std::log2(p);
    }
    return H;  // максимум 8.0
}

bool Engine::emergencyActive() const {
    std::lock_guard<std::mutex> lock(m_);
    return emergency_;
}

void Engine::noteRate() {
    auto now = std::chrono::steady_clock::now();
    rate_.push_back(now);
    while (!rate_.empty() && now - rate_.front() > std::chrono::seconds(1))
        rate_.pop_front();
}

void Engine::checkRate() {
    if (emergency_) {
        if (std::chrono::steady_clock::now() > emergencyUntil_)
            emergency_ = false;
        return;
    }
    if (rate_.size() > 150) {  // >150 изменений за секунду — не человек так работает
        emergency_ = true;
        emergencyUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        ++alerts_;
        ae::alert(L"АВАРИЙНЫЙ РЕЖИМ: массовое изменение файлов "
                  L"(>150/сек) — похоже на шифровальщик!");
        ae::warn(L"новые файлы 30 секунд будут копироваться в карантин");
    }
}

void Engine::take(const FileEvent& e, const std::wstring& full,
                  const std::wstring& reason, bool copyOnly) {
    bool done = q_.moveIn(full, reason, copyOnly);
    if (done) {
        ++quarantined_;
        if (copyOnly) {
            ae::warn(L"скопирован в карантин: " + full + L"  (" + reason + L")");
        } else {
            ae::alert(L"УГРОЗА забрана в карантин: " + full + L"  (" + reason + L")");
        }
    } else {
        ae::warn(L"не удалось убрать в карантин: " + full);
    }
    (void)e;
}

void Engine::onEvent(const FileEvent& e) {
    if (e.action == EventAction::Deleted) return;   // удалять нам нечего, пропускаем

    std::wstring full = e.dir + L"\\" + e.name;
    if (insideQuarantine(full)) return;             // карантин сам себя не забирает

    std::error_code ec;
    if (fs::is_directory(fs::path(full), ec) || ec) return;

    {
        std::lock_guard<std::mutex> lock(m_);
        noteRate();
        checkRate();
    }

    const std::wstring ext     = extOf(e.name);
    const std::wstring oldExt  = extOf(e.oldName);
    const bool susExt          = inList(ext, kSuspiciousExt,
                                        sizeof(kSuspiciousExt) / sizeof(wchar_t*));

    // 1) Переименовали нормальный файл в опасное расширение
    if (e.action == EventAction::Renamed &&
        !inList(oldExt, kSuspiciousExt, sizeof(kSuspiciousExt) / sizeof(wchar_t*)) &&
        !oldExt.empty() && susExt) {
        take(e, full, L"опасное переименование: " + oldExt + L" -> " + ext, false);
        return;
    }

    // 2) Опасное расширение у нового/изменённого файла
    if (susExt) {
        take(e, full, L"опасное расширение " + ext, false);
        return;
    }

    // 3) Высокая энтропия у несжатых файлов.
    //    Created: порог мягче (файл только что появился).
    //    Modified: порог строже — иначе поймаем обычный редактор.
    //    (событие Created приходит ДО записи данных, поэтому важен и Modified)
    bool compressed = inList(ext, kCompressedExt,
                             sizeof(kCompressedExt) / sizeof(wchar_t*));
    if (!compressed &&
        (e.action == EventAction::Created || e.action == EventAction::Modified)) {
        double threshold = (e.action == EventAction::Created) ? 7.5 : 7.8;
        double H = sampleEntropy(full);
        if (H >= threshold) {
            wchar_t buf[16];
            swprintf(buf, 16, L"%.2f", H);
            take(e, full, L"высокая энтропия (" + std::wstring(buf) +
                 L" из 8.0) — похоже на шифрование", false);
            return;
        }
    }

    // 4) Аварийный режим: все созданные файлы копируем
    if (emergencyActive() && e.action == EventAction::Created) {
        take(e, full, L"аварийный режим: массовое изменение файлов", true);
        return;
    }
}

} // namespace ae
