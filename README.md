# Aegis

**Aegis** is a cross-platform guardian for your files. It watches your folders (Desktop, Documents, Downloads by default, or any folders you choose), detects ransomware-like behavior, and moves dangerous files into quarantine before they can be encrypted or destroyed.

**Version:** v3.1 (ready for everyday use)  
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

Aegis combines real-time filesystem monitoring with behavior-based heuristics (not just signatures) to catch ransomware-like activity early, quarantine threats safely, and let you restore files if needed.

## Features

### Cross-platform monitoring
- Native watchers: Windows (`ReadDirectoryChangesW`), Linux (`inotify`), macOS (`FSEvents`)
- One background watcher thread per platform implementation
- Watches default user folders (Desktop, Documents, Downloads) with proper OS-aware resolution (including OneDrive/known folders/XDG), or custom folders

### Detection & protection
- **Dangerous extensions** (common ransomware families/markers), including double-extension patterns like `document.docx.locked`
- **High-entropy content** in new/modified files (sampled from first 8 KiB; compressed/media/container formats excluded to reduce false positives)
- **Mass file-change rate** detection → emergency mode (new files copied to quarantine for a window)
- **Mass rename bursts** detection (typical encryption behavior)
- **Canary honey-files** (`.aegis-canary.txt`) planted in watched folders; any tamper triggers alert and canaries are auto-replanted if removed
- **Ransom-note detection** by filename patterns and by content phrases (minimal YARA-style scan of first 16 KiB in small text files)
- **Detection escalation:** multiple strong detections in a short time trigger faster emergency response
- **Process attribution** (best effort): records which process holds/touched a quarantined file (Windows Restart Manager, `/proc/<pid>/fd` on Linux)
- **Exclusions / whitelist:** exclude trusted paths (prefix) and extensions to reduce false positives
- **Per-folder sensitivity:** assign sensitivity per watched folder (or use global)
- **Sensitivity profiles:** `low` (fewer false positives), `medium` (balanced), `high` (stricter/faster)
- **Ultra-light mode:** reduces heavy checks/IO for very low CPU and memory usage on weaker systems

### Quarantine, history & recovery
- Threats are moved (or copied in emergency mode) into `~/.aegis/quarantine`
- Each quarantined item includes `meta.txt` (origin path, time, reason, mode, attribution when available)
- Nothing is silently destroyed: files can be reviewed and restored
- Interactive quarantine management: restore by number, **restore all**, delete record, clear all, open quarantine folder
- History/stats view with search/filter and report export

### UI & usability
- Interactive zapret-style menu UI: numbered choices, clear screens, submenus, easy navigation
- Live watch screen with counters (events/alerts/quarantined/emergency) and recent alerts preview
- Recent alerts buffer view in menu (helps track alerts without losing context)
- Diagnostics / setup check (folders, quarantine/log paths, canary presence)
- Open quarantine/log locations from menus
- Centered terminal output and large banner

### Updates, autostart & persistence
- **Self-update:** `aegis update` checks GitHub Releases and replaces the binary in-place (with rollback; cleans `*.old` on startup)
- **Auto-update check notice:** optional check on interactive start with a quiet menu notification when a newer release is available
- **Autostart:** `aegis autostart on|off` (HKCU Run on Windows, XDG autostart on Linux, LaunchAgent on macOS; no admin rights required)
- **Persistent settings:** watched folders, per-folder sensitivity, global sensitivity, exclusions, ultra-light mode, auto-update preference saved in `~/.aegis/config.txt`

### Logging & footprint
- Activity log: `~/.aegis/aegis.log`
- Low footprint design: small sampling windows (entropy/content), bounded bookkeeping, periodic working-set trimming on Windows, optional ultra-light mode

## How it works

1. **Watcher** receives OS filesystem events (create/modify/delete/rename).
2. **Engine** evaluates events with heuristics: suspicious extensions/renames, high entropy, ransom-note names/content, canary tampering, rename/rate bursts, and rapid sequences of strong detections.
3. **Quarantine** safely takes custody of suspicious files with metadata for review/restore.
4. **UI/CLI** provides monitoring, status, quarantine management, settings, diagnostics, and updates.

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
(or build with CMake)

## Run

```text
aegis                         # interactive menu (recommended)
aegis watch [dir1 dir2 ...]   # watch folders (saved/custom or defaults)
aegis quarantine              # show quarantine contents
aegis restore                 # list quarantine records
aegis restore <number>        # restore a quarantined file
aegis update [-y]             # self-update from GitHub Releases
aegis autostart on|off        # enable/disable autostart
aegis help                    # help
```

### Interactive menu highlights
- **Watch / protect folders:** start protection, configure watched folders, per-folder sensitivity
- **Quarantine:** view, restore, restore all, delete, clear, open folder
- **History / stats / search:** browse and filter records, open locations
- **Settings:** global sensitivity, ultra-light mode, exclusions (paths/extensions), auto-update check, paths
- **Diagnostics:** setup checks and canary status
- **Recent alerts buffer / Export report / Update / Autostart / Help**

While watching (CLI mode): `status`, `quarantine`, `restore [n]`, `help`, `q`.

## Paths

- Config: `~/.aegis/config.txt`
- Log: `~/.aegis/aegis.log`
- Quarantine: `~/.aegis/quarantine/`
- Report export: `~/.aegis/aegis-report.txt`

(`~` = user profile/home directory)

## Notes on false positives

Ransomware detection is heuristic/behavioral. If you see false positives:
- lower sensitivity globally or for a specific folder
- enable ultra-light mode on low-power systems/workloads as needed
- add exclusions for trusted paths or extensions
- review quarantine records and restore safe files

Aegis quarantines files (custody), it does not permanently delete them.

## Download

Prebuilt binaries are available on the [Releases](https://github.com/narkomaniya/Aegis/releases) page.

## About

Aegis was created as a personal **cybersecurity** project by a **13-year-old developer** to learn systems programming in C++ and build real protection against ransomware-like threats.
