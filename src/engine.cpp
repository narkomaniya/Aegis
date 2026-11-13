// engine.cpp
#include "engine.h"
#include "logger.h"
#include "attribution.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ae {

// Расширения-признаки шифровальщиков.
// .onion здесь НЕ нужен: это домен в записках, а не расширение зашифрованного
// файла — ловится контент-сканом и даёт ложные срабатывания как расширение.
static const wchar_t* kSuspiciousExt[] = {
    L".locked", L".locky",  L".enc",   L".encrypted", L".crypt",  L".crypto",
    L".cryptolocker",       L".cerber",              L".wnry",   L".wcry",
    L".cryp1",  L".osiris", L".zepto",     L".odcodc", L".paymessage",
    L".kraken", L".gravity", L".tevac", L".r4a",       L".lock93", L".party",
    L".lockbit", L".blackcat", L".alphv", L".conti",  L".phobos", L".ryuk",
    L".akira",   L".play",     L".hive",  L".revil",  L".sodinokibi",
    L".maze",    L".egregor",  L".darkside", L".avoslocker", L".babuk",
    L".clop",    L".stop",     L".djvu",  L".medusalocker",
    L".rhysida", L".blacksuit", L".inc", L".8base", L".bianlian", L".nokoyawa",
    L".royal",   L".cactus",    L".ransomhub", L".fargo", L".akira_readme",
};

// Форматы, где высокая энтропия это норма (сжатие/контейнер/медиа/БД) —
// не подозреваем их, иначе получим массу ложных срабатываний.
static const wchar_t* kCompressedExt[] = {
    L".zip", L".rar", L".7z",  L".gz",  L".xz",  L".bz2", L".cab",
    L".tar", L".zst", L".lz4", L".lz",  L".br",
    L".jpg", L".jpeg", L".png", L".gif", L".webp", L".heic", L".heif",
    L".avif", L".bmp", L".tif", L".tiff", L".ico", L".svgz",
    L".mp3", L".mp4",  L".avi", L".mkv", L".mov",  L".webm", L".ogg",
    L".flac", L".aac", L".m4a", L".m4v", L".wmv",  L".wma",  L".opus",
    L".exe", L".dll",  L".msi", L".sys", L".so",   L".dylib",
    L".pdf", L".docx", L".xlsx", L".pptx", L".docm", L".xlsm", L".pptm",
    L".odt", L".ods",  L".odp",
    L".jar", L".apk",  L".ipa", L".dmg", L".iso",  L".pkg",  L".deb", L".rpm",
    L".sqlite", L".db", L".sqlite3",
    L".woff", L".woff2", L".ttf", L".otf", L".eot",
    L".class", L".pyc", L".wasm",
};

static std::wstring lower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::towlower);
    return r;
}

// ---- Canary-ловушка: тихий файл-детектор в каждой наблюдаемой папке ----
// Шифровальщики трогают ВСЕ файлы; любое касание ловушки = точное знание,
// что в папке хозяйничает не человек (и не сам Aegis).
static const wchar_t* kCanaryName = L".aegis-canary.txt";
static const char* kCanaryText =
    "Aegis canary file.\n"
    "This file is monitored by Aegis (file guardian). If this file is\n"
    "changed, moved or deleted, something touched this folder without\n"
    "permission and Aegis already reacted.\n"
    "Do not delete.\n";

// ---- Записки выкупа: имена файлов, которые создают шифровальщики ----
static const wchar_t* kRansomNoteTokens[] = {
    L"how_to_decrypt", L"how_to_recover", L"how_to_fix",  L"readme_for_decrypt",
    L"recover_files",  L"decrypt_files",  L"help_decrypt", L"help_restore",
    L"your_files",     L"allyourfiles",   L"restore_files", L"_what_is_this",
    L"pay_to_unlock",  L"ransom",         L"readme_decrypt",
    L"howtorecover",   L"decryption_instructions", L"decrypt_instruction",
    L"recovery_instructions", L"filesencrypted",
};

static bool isRansomNoteName(const std::wstring& name) {
    const std::wstring low = lower(name);
    // записки обычно .txt/.html/.htm, иногда без расширения
    size_t dot = low.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        const std::wstring ext = low.substr(dot);
        static const wchar_t* kNoteExts[] = { L".txt", L".html", L".htm" };
        bool okExt = false;
        for (const wchar_t* e : kNoteExts) if (ext == e) { okExt = true; break; }
        if (!okExt) return false;
    }
    for (const wchar_t* tok : kRansomNoteTokens)
        if (low.find(tok) != std::wstring::npos) return true;
    return false;
}

// double extension like photo.jpg.lockbit -> very strong ransomware indicator
static bool hasDoubleExtRansom(const std::wstring& name, const std::wstring& ext) {
    if (ext.empty()) return false;
    std::wstring low = lower(name);
    if (low.size() <= ext.size() + 2) return false;
    size_t prevDot = low.rfind(L'.', low.size() - ext.size() - 1);
    if (prevDot == std::wstring::npos) return false;
    std::wstring prevExt = low.substr(prevDot, low.size() - ext.size() - prevDot);
    static const wchar_t* kCommon[] = {
        L".doc",L".docx",L".xls",L".xlsx",L".ppt",L".pptx",L".pdf",
        L".jpg",L".jpeg",L".png",L".bmp",L".gif",
        L".txt",L".rtf",L".csv",L".sql",L".db",L".mdb",L".accdb",
        L".zip",L".rar",L".7z",L".mp3",L".mp4",L".avi",L".mkv",
        L".dwg",L".psd",L".ai",L".cdr",L".odt",L".ods"
    };
    for (const wchar_t* c : kCommon) if (prevExt == c) return true;
    return false;
}

// Минимум в духе YARA: типовые фразы выкупа ВНУТРИ записки.
// Полноценный движок правил не городим — фиксированного списка достаточно
// для тихой проверки первых 16 КиБ у новых текстовых файлов.
//
// ВАЖНО: фразы хранятся XOR-кодированными (ключ 0x55) и расшифровываются
// только в памяти при сканировании. Антивирусы ищут в бинаре строки вида
// "your files are encrypted" / "send bitcoin" — наши голые строки в тексте
// программы давали false positive: Defender съел первый билд как
// Ransom:Win32/Avaddon. Кодированные — сигнатуры в бинаре нет.
//
// Хранение ПЛОСКОЕ: все фразы одной последовательностью байт по UTF-8,
// разделитель 0x00 (как C-строки). ВАЖНО: не среды к типу [N][36] —
// без внутренних скобок C++ заполняет строки плоско и "съедает" границы!
static const char kRansomContentEnc[] = {
        '\x2c','\x3a','\x20','\x27','\x75','\x33','\x3c','\x39','\x30','\x26','\x75','\x34','\x27','\x30','\x75','\x30','\x3b','\x36','\x27','\x2c','\x25','\x21','\x30','\x31', 0,
        '\x2c','\x3a','\x20','\x27','\x75','\x33','\x3c','\x39','\x30','\x26','\x75','\x3d','\x34','\x23','\x30','\x75','\x37','\x30','\x30','\x3b','\x75','\x30','\x3b','\x36','\x27','\x2c','\x25','\x21','\x30','\x31', 0,
        '\x34','\x39','\x39','\x75','\x2c','\x3a','\x20','\x27','\x75','\x33','\x3c','\x39','\x30','\x26','\x75','\x3d','\x34','\x23','\x30','\x75','\x37','\x30','\x30','\x3b','\x75','\x30','\x3b','\x36','\x27','\x2c','\x25','\x21','\x30','\x31', 0,
        '\x22','\x30','\x75','\x22','\x3c','\x39','\x39','\x75','\x31','\x30','\x36','\x27','\x2c','\x25','\x21','\x75','\x2c','\x3a','\x20','\x27', 0,
        '\x27','\x30','\x34','\x31','\x38','\x30','\x75','\x33','\x3a','\x27','\x75','\x31','\x30','\x36','\x27','\x2c','\x25','\x21', 0,
        '\x27','\x30','\x26','\x21','\x3a','\x27','\x30','\x75','\x33','\x3c','\x39','\x30','\x26','\x75','\x33','\x3a','\x27','\x75','\x33','\x30','\x30', 0,
        '\x7b','\x3a','\x3b','\x3c','\x3a','\x3b','\x7a', 0,
        '\x21','\x3a','\x27','\x75','\x37','\x27','\x3a','\x22','\x26','\x30','\x27','\x75','\x21','\x3a','\x75','\x31','\x30','\x36','\x27','\x2c','\x25','\x21', 0,
        '\x37','\x3c','\x21','\x36','\x3a','\x3c','\x3b','\x75','\x22','\x34','\x39','\x39','\x30','\x21', 0,
        '\x26','\x30','\x3b','\x31','\x75','\x37','\x3c','\x21','\x36','\x3a','\x3c','\x3b', 0,
        '\x21','\x27','\x34','\x3b','\x26','\x33','\x30','\x27','\x75','\x21','\x3d','\x30','\x75','\x34','\x38','\x3a','\x20','\x3b','\x21', 0,
};

// Расшифровка одного токена (в памяти, на лету)
static std::string ransomToken(const char* enc) {
    std::string out;
    while (*enc) { out.push_back((char)((unsigned char)*enc ^ 0x55)); ++enc; }
    return out;
}

static std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// Сканирует содержимое на фразы выкупа. Для снижения ложных срабатываний
// требуется >= 2 разных совпадения (реальная записка почти всегда содержит
// несколько признаков: "encrypted" + контакт/кошелёк/.onion).
// Возвращает описание совпадений или пустую строку.
static std::string ransomContentHit(const std::wstring& full) {
    std::error_code ec;
    if (fs::file_size(fs::path(full), ec) == 0 || ec) return "";

    std::ifstream f(fs::path(full), std::ios::binary);
    if (!f) return "";

    constexpr size_t N = 16384;          // как у энтропии — только начало
    std::string buf;
    buf.resize(N);
    f.read(&buf[0], N);
    buf.resize((size_t)f.gcount());

    std::string low = lowerAscii(buf);
    std::string first;
    std::string second;
    int hits = 0;

    const char* enc = kRansomContentEnc;
    while (*enc) {
        std::string tok = ransomToken(enc);
        if (low.find(tok) != std::string::npos) {
            ++hits;
            if (first.empty()) first = tok;
            else if (second.empty() && tok != first) second = tok;
            // достаточно 2 разных сигналов для уверенного вердикта
            if (hits >= 2 && !second.empty()) break;
        }
        enc += tok.size() + 1;          // перепрыгнуть 0x00-разделитель
    }
    if (hits < 2) return "";
    if (!second.empty()) return first + ", " + second;
    return first; // (на случай дубля — маловероятно, но hits>=2)
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

// Временные/служебные файлы (редакторы, браузеры, ОС) — их активность
// не похожа на шифровальщика и даёт ложные срабатывания.
static bool isIgnoredName(const std::wstring& name) {
    std::wstring low = lower(name);
    if (low == kCanaryName) return false;   // канарейку никогда не игнорируем

    // Office lock/temp: "~$document.docx"
    if (low.rfind(L"~$", 0) == 0) return true;

    static const wchar_t* kExact[] = {
        L".ds_store", L"thumbs.db", L"desktop.ini",
        L"ntuser.dat", L"ntuser.dat.log1", L"ntuser.dat.log2",
    };
    for (const wchar_t* e : kExact) if (low == e) return true;

    const std::wstring ext = extOf(low);
    static const wchar_t* kExt[] = {
        L".tmp", L".temp", L".part", L".crdownload", L".partial",
        L".swp", L".swo", L".swx", L".bak", L".lnk",
        L".etl", L".opdownload", L".download",
        L".db-wal", L".db-shm", L".journal",
    };
    for (const wchar_t* e : kExt) if (ext == e) return true;

    // "~filename" временники некоторых редакторов
    if (!low.empty() && low[0] == L'~' && low.size() > 1) return true;

    return false;
}

bool Engine::insideQuarantine(const std::wstring& full) const {
    // переносимо: приводим оба пути к одному сепаратору ('/')
    auto norm = [](std::wstring s) {
        std::replace(s.begin(), s.end(), L'\\', L'/');
        return s;
    };
    std::wstring f = lower(norm(fs::path(full).lexically_normal().wstring()));
    std::wstring q = lower(norm(fs::path(q_.root()).lexically_normal().wstring()));
    if (q.empty()) return false;
    if (q.back() != L'/') q += L'/';
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

void Engine::noteRate(bool isRename) {
    auto now = std::chrono::steady_clock::now();
    rate_.push_back(now);
    while (!rate_.empty() && now - rate_.front() > std::chrono::seconds(1))
        rate_.pop_front();

    if (isRename) {
        renames_.push_back(now);
        while (!renames_.empty() && now - renames_.front() > std::chrono::seconds(1))
            renames_.pop_front();
    }
}

static void raiseEmergencyLocked(unsigned long long& alerts, bool& emergency,
                                 std::chrono::steady_clock::time_point& until,
                                 const std::wstring& why) {
    if (!emergency) {
        emergency = true;
        until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    }
    ++alerts;
    ae::alert(why);
}

void Engine::checkRate() {
    if (emergency_) {
        if (std::chrono::steady_clock::now() > emergencyUntil_)
            emergency_ = false;
        return;
    }
    size_t limit = 150;
    if (sensitivity_ <= 0) limit = 220;      // low: fewer false positives
    else if (sensitivity_ >= 2) limit = 90;  // high: catch faster
    if (rate_.size() > limit) {
        raiseEmergencyLocked(alerts_, emergency_, emergencyUntil_,
                             L"EMERGENCY: mass file changes — looks like a ransomware attack!");
        ae::warn(L"new files will be copied to quarantine for 30 seconds");
    }
}

void Engine::checkRenames() {
    if (emergency_) return;   // already in emergency — no duplicate alarm
    // Ransomware renames victims in bulk: file.txt -> file.txt.<their-ext>.
    size_t limit = 15;
    if (sensitivity_ <= 0) limit = 25;
    else if (sensitivity_ >= 2) limit = 8;
    if (renames_.size() > limit) {
        raiseEmergencyLocked(alerts_, emergency_, emergencyUntil_,
                             L"ANOMALY: mass rename burst — typical ransomware behavior (files renamed during encryption)");
        ae::warn(L"new files will be copied to quarantine for 30 seconds");
    }
}

static void noteHitLocked(std::deque<std::chrono::steady_clock::time_point>& hits,
                          unsigned long long& alerts, bool& emergency,
                          std::chrono::steady_clock::time_point& until,
                          int sensitivity) {
    auto now = std::chrono::steady_clock::now();
    hits.push_back(now);
    auto window = std::chrono::seconds(10);
    while (!hits.empty() && now - hits.front() > window) hits.pop_front();
    size_t limit = 4;
    if (sensitivity <= 0) limit = 6;
    else if (sensitivity >= 2) limit = 3;
    if (hits.size() >= limit) {
        raiseEmergencyLocked(alerts, emergency, until,
            L"EMERGENCY: multiple ransomware-like detections in a short time — attack in progress!");
        ae::warn(L"new files will be copied to quarantine for 30 seconds");
    }
}

// Windows: every ~1024 events we trim the working set of the process.
// A resident guardian may hang in memory for months — keep the RSS tiny;
// pages come back from the pagefile instantly when needed.
void Engine::trimWorkingSet() {
#ifdef _WIN32
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
}

void Engine::take(const FileEvent& e, const std::wstring& full,
                  const std::wstring& reason, bool copyOnly) {
    // "Who did this?" — platform attribution: on Windows/Linux we find the
    // processes that currently have the file open.
    std::wstring who = fileOwner(full);
    std::wstring reasonFull = reason;
    if (!who.empty()) reasonFull += L"  | touched by " + who;

    bool done = q_.moveIn(full, reasonFull, copyOnly);
    if (done) {
        ++quarantined_;
        if (copyOnly) {
            ae::warn(L"copied to quarantine: " + full + L"  (" + reasonFull + L")");
        } else {
            ae::alert(L"THREAT quarantined: " + full + L"  (" + reasonFull + L")");
        }
    } else {
        ae::warn(L"failed to quarantine: " + full);
    }
    (void)e;
}

void Engine::ignorePath(const std::wstring& full) {
    std::lock_guard<std::mutex> lock(m_);
    if (ignored_.size() >= 512) ignored_.clear();   // потолок памяти: пути restore недолговечны
    ignored_.insert(lower(full));
}

// Расставить canary-ловушки по наблюдаемым папкам (вызывается ДО запуска
// слежения, чтобы не поймать собственное создание ловушки).
void Engine::seedCanaries() {
    std::error_code ec;
    for (const auto& root : protectedRoots_) {
        fs::path dir(root);
        if (!fs::is_directory(dir, ec) || ec) continue;   // несуществующие тихо пропускаем

        fs::path canary = dir / fs::path(kCanaryName);
        if (fs::exists(canary, ec) && !ec) continue;     // ловушка уже на месте (не пересоздаём)

        std::ofstream f(canary, std::ios::binary | std::ios::trunc);
        if (!f) {
            ae::warn(L"canary could not be created: " + canary.wstring());
            continue;
        }
        f << kCanaryText;
        f.close();

#ifdef _WIN32
        // спрятать ловушку от глаз пользователя
        DWORD attrs = GetFileAttributesW(canary.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
            SetFileAttributesW(canary.c_str(),
                               attrs | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
#endif
        ae::info(L"canary deployed: " + canary.wstring());
    }
}

void Engine::onEvent(const FileEvent& e) {
    // 0) Удалили canary-ловушку? Это sabotage-сигнал отличный от прочих Deleted.
    if (e.action == EventAction::Deleted) {
        if (lower(e.name) == kCanaryName) {
            ae::alert(L"TAMPER ALERT: canary was DELETED: " +
                      (fs::path(e.dir) / e.name).wstring() +
                      L" — someone is cleaning the trap!");
            {
                std::lock_guard<std::mutex> lock(m_);
                if (!emergency_) {
                    emergency_ = true;
                    emergencyUntil_ = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(30);
                }
                ++alerts_;
            }
            // вернуть ловушку на место мгновенно
            fs::path canary = fs::path(e.dir) / fs::path(kCanaryName);
            ignorePath(canary.wstring());   // событие создания не ловим сами
            std::ofstream f(canary, std::ios::binary | std::ios::trunc);
            if (f) {
                f << kCanaryText;
#ifdef _WIN32
                DWORD attrs = GetFileAttributesW(canary.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES)
                    SetFileAttributesW(canary.c_str(),
                                       attrs | FILE_ATTRIBUTE_HIDDEN |
                                       FILE_ATTRIBUTE_SYSTEM);
#endif
            }
        }
        return;   // обычные удаления нам не интересны
    }

    // Переносимый путь: на Windows сепаратор '\', на macOS/Linux '/'
    std::wstring full = (fs::path(e.dir) / e.name).wstring();

    {
        std::lock_guard<std::mutex> lock(m_);
        if (ignored_.count(lower(full))) return;    // только вернули из карантина — не трогаем
    }

    if (insideQuarantine(full)) return;             // карантин сам себя не забирает

    std::error_code ec;
    if (fs::is_directory(fs::path(full), ec) || ec) return;

    // Временные/служебные файлы (браузер .part/.crdownload, Office ~$*, .tmp)
    // не анализируем — это снижает ложные срабатывания и шум в rate.
    if (isIgnoredName(e.name)) return;

    {
        std::lock_guard<std::mutex> lock(m_);
        ++events_;
        if (events_ % 1024 == 0) trimWorkingSet();   // маленький RSS на годы работы
        noteRate(e.action == EventAction::Renamed);
        checkRate();
        checkRenames();
    }

    const std::wstring ext     = extOf(e.name);
    const std::wstring oldExt  = extOf(e.oldName);
    const bool susExt          = inList(ext, kSuspiciousExt,
                                        sizeof(kSuspiciousExt) / sizeof(wchar_t*));

    // 0b) Canary-ловушку тронули: Created/Modified/Renamed у ловушки.
    // Renamed-ловушку карантиним по НОВОМУ имени (старого уже нет).
    if (lower(e.name) == kCanaryName) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!emergency_) {
                emergency_ = true;
                emergencyUntil_ = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(30);
            }
            ++alerts_;
            noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_);
        }
        ae::alert(L"TAMPER ALERT: canary touched: " + full +
                  L" — something is messing with this folder!");
        take(e, full, L"canary honey-file touched", false);
        return;
    }

    // 0c) Записка выкупа: типовое имя "how to decrypt ..." и т.п.
    if (isRansomNoteName(e.name)) {
        take(e, full, L"ransom note detected: " + e.name, false);
        { std::lock_guard<std::mutex> lock(m_); noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_); }
        return;
    }

    // 0c-2) Минимум в духе YARA: фразы выкупа внутри содержимого.
    //   Только текстовые расширения (записки всегда .txt/.html/.htm),
    //   только первые 16 КиБ — дёшево, бинари не трогаем.
    if (e.action == EventAction::Created || e.action == EventAction::Modified) {
        static const wchar_t* kNoteExtsSc[] =
            { L".txt", L".html", L".htm", L".md", L".ini", L".log" };
        if (inList(ext, kNoteExtsSc, sizeof(kNoteExtsSc) / sizeof(wchar_t*))) {
            // skip very large text files (ransom notes are small) to save IO/CPU
            std::error_code ecSz;
            auto sz = fs::file_size(fs::path(full), ecSz);
            if (!ecSz && sz <= 2 * 1024 * 1024) {
                const std::string hit = ransomContentHit(full);
                if (!hit.empty()) {
                    std::wstring hitw(hit.begin(), hit.end());
                    take(e, full, L"ransom note content: \"" + hitw + L"\"", false);
                    { std::lock_guard<std::mutex> lock(m_); noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_); }
                    return;
                }
            }
        }
    }

    // 1) Renamed a normal file into a dangerous extension
    if (e.action == EventAction::Renamed &&
        !inList(oldExt, kSuspiciousExt, sizeof(kSuspiciousExt) / sizeof(wchar_t*)) &&
        !oldExt.empty() && susExt) {
        take(e, full, L"dangerous rename: " + oldExt + L" -> " + ext +
                 (hasDoubleExtRansom(e.name, ext) ? L" (double extension)" : L""), false);
        { std::lock_guard<std::mutex> lock(m_); noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_); }
        return;
    }

    // 2) Dangerous extension on a new/modified file
    if (susExt) {
        take(e, full, L"dangerous extension " + ext +
                 (hasDoubleExtRansom(e.name, ext) ? L" (double extension)" : L""), false);
        { std::lock_guard<std::mutex> lock(m_); noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_); }
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
        // Thresholds depend on sensitivity (low = fewer FPs, high = more aggressive)
        double tCreated = 7.5, tModified = 7.85;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (sensitivity_ <= 0) { tCreated = 7.7; tModified = 7.92; }
            else if (sensitivity_ >= 2) { tCreated = 7.3; tModified = 7.7; }
        }
        double threshold = (e.action == EventAction::Created) ? tCreated : tModified;
        double H = sampleEntropy(full);
        if (H >= threshold) {
            wchar_t buf[16];
            swprintf(buf, 16, L"%.2f", H);
            take(e, full, L"high entropy (" + std::wstring(buf) +
                 L"/8.0) — looks like encryption", false);
            { std::lock_guard<std::mutex> lock(m_); noteHitLocked(hits_, alerts_, emergency_, emergencyUntil_, sensitivity_); }
            return;
        }
    }

    // 4) Аварийный режим: все созданные файлы копируем
    if (emergencyActive() && e.action == EventAction::Created) {
        take(e, full, L"emergency mode: mass file changes", true);
        return;
    }
}

} // namespace ae
