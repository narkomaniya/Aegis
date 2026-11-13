# Aegis

**Aegis** is a cross-platform guardian for your files. It watches your folders (Desktop, Documents, Downloads by default, or any folders you choose), detects ransomware-like behavior, and moves dangerous files into quarantine before they can be encrypted or destroyed.

**Version:** v3.2 (ready for everyday use)  
**Author:** by narkomaniya

```
        ________________________________
        ||                            ||
        ||                            ||
        ||     A   E   G   I   S      ||
        ||============================||
        ||s h i e l d   o f   Z e u s ||
        ||                            ||
        ||____________________________||
          \____________________________/
            \________________________/
              \____________________/
                \________________/
                  \____________/
                    \________/
                      \____/
                        \/
```

## What Aegis does

Aegis combines real-time filesystem monitoring with behavior-based heuristics to catch ransomware-like activity early, quarantine threats safely, and let you review/restore files.

## Features

### Monitoring
- Native watchers: Windows (`ReadDirectoryChangesW`), Linux (`inotify`), macOS (`FSEvents`)
- Default protected folders (Desktop, Documents, Downloads) with OS-aware resolution (Known Folders/XDG, OneDrive-aware), plus custom folders
- Interactive zapret-style menu UI (numbered menus, submenus, clear screens)

### Detection & protection
- **Dangerous extensions** (common ransomware families/markers) and **double-extension** detection (e.g. `file.docx.locked`)
- **High-entropy** detection for new/modified files (sampled from first 8 KiB; compressed/media/container formats excluded)
- **Mass activity detection:** file-change rate spikes and rename bursts → emergency mode
- **Canary honey-files** in watched folders (tamper alert, auto-replant on delete)
- **Ransom-note detection:** filename patterns and content phrase scan (minimal YARA-style, first 16 KiB of small text files)
- **Hit escalation:** multiple strong detections in a short window escalate protection faster
- **Process attribution** (best effort): records who touched/quarantined files (Windows Restart Manager, `/proc/<pid>/fd` on Linux)
- **Exclusions / whitelist:** exclude trusted paths (prefix) and extensions
- **Per-folder sensitivity** and global sensitivity profiles: `low`, `medium`, `high`
- **Ultra-light mode:** reduces heavy checks/IO for very low CPU and memory usage
- **User rules file:** optional `~/.aegis/rules.txt` to add extra `exclude_path=` / `exclude_ext=` entries

### Quarantine, backup & recovery
- Threats moved (or copied in emergency mode) to `~/.aegis/quarantine` with `meta.txt` (origin, time, reason, mode, attribution)
- **Backup before quarantine:** optional extra backup copy in `~/.aegis/backups` before moving suspicious files
- No silent permanent deletion: review and restore files
- Quarantine actions: restore by number, **restore all**, **restore recent** (last 30 min / mass-attack rollback helper), delete record, clear all, open folder
- History/stats with search/filter and **export report** (`~/.aegis/aegis-report.txt`)

### UI & diagnostics
- Live watch screen: events/alerts/quarantined/emergency counters + recent alerts preview
- **Recent alerts buffer** view/clear (stable place to review alerts)
- **Diagnostics / check setup:** folders, quarantine/config/log paths, canary presence, write test, rules/backups status
- Open quarantine/log locations from menus

### Settings & convenience
- Persistent settings in `~/.aegis/config.txt`
- **Profiles/presets:** Balanced, Strict protection, Performance/ultra-light, Documents
- Configure watched folders and per-folder sensitivity
- **Export/import settings** (copy config file)
- **Auto-update check on start** (quiet menu notice) + manual `aegis update`
- **Autostart:** `aegis autostart on|off` (HKCU Run / XDG autostart / LaunchAgent)

### Low footprint
- Small sampling windows (8 KiB entropy, 16 KiB content), bounded bookkeeping, periodic working-set trimming (Windows), ultra-light mode option

## Build

Requires a C++17 compiler.

**Windows**
```bat
build.bat
```
or
```bat
g++ -std=c++17 -O2 -Wall src\*.cpp -o build\aegis.exe -lshell32 -lole32 -luuid
```

**Linux/macOS**
```bash
./build.sh
```
(CI builds produce `aegis-linux`, `aegis-macos`, `aegis.exe` artifacts.)

## Run

```text
aegis                         # interactive menu (recommended)
aegis watch [dir1 dir2 ...]   # watch folders (saved/custom or specified)
aegis quarantine              # show quarantine contents
aegis restore                 # list quarantine records
aegis restore <number>        # restore a quarantined file
aegis update [-y]             # self-update from GitHub Releases
aegis autostart on|off        # enable/disable autostart
aegis help                    # help
```

### Interactive menu
- **Watch / protect folders:** start protection, configure folders, per-folder sensitivity
- **Quarantine:** view/restore/restore recent/delete/clear/open
- **History / stats / search:** browse, filter, open locations
- **Settings:** sensitivity, ultra-light, backups, folders, exclusions, auto-update, profiles, export/import, paths
- **Diagnostics / Recent alerts / Export report / Update / Autostart / Help**

While watching (CLI mode): `status`, `quarantine`, `restore [n]`, `help`, `q`.

## Paths

- Config: `~/.aegis/config.txt`
- User rules (optional): `~/.aegis/rules.txt`
- Log: `~/.aegis/aegis.log`
- Quarantine: `~/.aegis/quarantine/`
- Backups: `~/.aegis/backups/`
- Report export: `~/.aegis/aegis-report.txt`

## User rules file (`rules.txt`)

Optional simple lines (merged with settings exclusions at watch start):
```text
exclude_path=C:\path\to\ignore
exclude_ext=.log
```

## False positives

Detection is heuristic/behavioral. To reduce false positives:
- lower sensitivity globally or per folder
- enable ultra-light mode where appropriate
- add exclusions (paths/extensions) or `rules.txt` entries
- review quarantine and restore safe files (backups may also exist if enabled)

## CI & releases

GitHub Actions builds and smoke-tests on Windows, Linux, and macOS. On version tags (`v*`), CI can publish a release with platform binaries and `SHA256SUMS.txt`.

## About

Aegis was created as a personal **cybersecurity** project by a **13-year-old developer** to learn systems programming in C++ and build real protection against ransomware-like threats.
