# Aegis

**Aegis** is a cross-platform guardian for your files. It watches your folders (Desktop, Documents, Downloads by default, or any folders you choose), detects ransomware-like behavior, and moves dangerous files into quarantine before they can be encrypted or destroyed.

**Version:** v6.0 — global update: tray icon + MAXIMUM SECURITY, secret zoo, safe config sharing, Doctor, and the mod bot  
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
- Persistent settings in `~/.aegis/config.txt` (with backward-compatible loading)
- **Profiles/presets:** Balanced, Strict protection, Performance/ultra-light, Documents
- Configure watched folders and per-folder sensitivity
- **Export/import settings** (copy config file)
- **Language setting (EN/RU foundation)** for UI evolution
- **UI theme tweaks:** `default` / `white` / `black` (configurable in Settings or via mods)
- **Mods/Tweaks Manager (v5):** interactive UI + CLI to list, inspect, enable/disable, verify, apply, and publish mods
- **Automatic mod verification:** `aegis mod verify` and strict `aegis mod publish` checks; CI workflow auto-verifies mod submissions/changes
- **Auto-update check on start** (quiet menu notice) + manual `aegis update` with SHA256 integrity verification when release checksums are available
- **Hands-off background protection:** `aegis autostart` / `aegis service` helpers (user-level, best effort) run `aegis watch --daemon` automatically at login (HKCU Run / XDG autostart / LaunchAgent with KeepAlive on macOS)

### Low footprint
- Small sampling windows (8 KiB entropy, 16 KiB content), bounded bookkeeping, periodic working-set trimming (Windows), ultra-light mode option

## Install from a terminal

One command, no manual asset hunting. The installer downloads the newest release
that actually has a published binary, verifies `SHA256SUMS.txt` when available,
and installs `aegis` to `~/.local/bin` (Linux/macOS) or
`%LOCALAPPDATA%\Aegis` (Windows). If a release has no binary yet, it falls back
to building from source (C++17 compiler and git required).

**Linux / macOS**
```bash
curl -fsSL https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.sh | sh
```

**Windows (PowerShell)**
```powershell
irm https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.ps1 | iex
```

The installer adds its folder to your PATH (Windows: current user PATH;
Linux/macOS: a symlink in `/usr/local/bin` when writable), so just run:

```bash
aegis help
```

Override the destination with `AEGIS_INSTALL_DIR`. If `aegis` is still not
found in the current terminal, either open a new terminal or run the binary by
its full path: `~/.local/bin/aegis help` (Linux/macOS),
`%LOCALAPPDATA%\Aegis\aegis.exe help` (Windows).

Prefer doing it yourself? Download a binary directly from
[Releases](https://github.com/narkomaniya/Aegis/releases):
`aegis-linux`, `aegis-macos`, `aegis.exe`.

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
aegis watch --daemon [...]    # non-interactive watch mode (service-friendly)
aegis watch --tray [...]      # tray icon + MAXIMUM SECURITY while active
aegis status                     # quick health/status summary
aegis quarantine                 # show quarantine contents
aegis restore [number]           # list or restore quarantined files
aegis export [path] [--json]     # export quarantine/report (txt or json)
aegis mod [list|path|ui|info|verify|enable|disable|create|apply|publish|bot|open]
                                 # mods/tweaks manager, author tools, mod bot
aegis doctor                     # integrity checks + Security Score
aegis configs                    # save/apply/import presets by link (diff + backup)
aegis update [-y]                # self-update from GitHub Releases (SHA256 verified when checksums available)
aegis autostart on|off           # enable/disable automatic background protection at login
aegis service status|install|uninstall|start|stop
                                 # background/service helper (best effort)
aegis help                       # help
```

### Interactive menu
- **Watch / protect folders:** start protection, configure folders, per-folder sensitivity
- **Quarantine:** view/restore/restore recent/delete/clear/open
- **History / stats / search:** browse, filter, open locations
- **Settings:** sensitivity, ultra-light, backups, folders, exclusions, auto-update, profiles, export/import, paths
- **Configs:** save presets, apply with diff/backup, import by URL or local path, publish via `gh gist`
- **Doctor:** binary SHA256, config/mods/autostart/canary checks, Security Score
- **Diagnostics / Recent alerts / Export report / Update / Autostart / Help**

While watching: `status`, `quarantine`, `restore [n]`, `help`, `q`, and the secret animal commands.

### Tray + MAXIMUM SECURITY (v6.0)

Start `aegis watch --tray` (or Watch menu → **4**). While the tray icon is alive,
Aegis automatically enables MAXIMUM SECURITY: rate/rename and `noteHit` limits are
divided by 3, entropy thresholds drop to 6.8/7.1, and emergency mode lasts 60 s.
All `WARN`/`ALERT` messages are mirrored to the tray (Windows balloon / POSIX
`notify-send` or `osascript`). This is intentionally stricter than normal mode —
a few extra false positives are the price of staying safer.

### Configs (v6.0)

`aegis configs` (main menu **11**) stores presets in `~/.aegis/configs/`. Importing
a link never writes directly to `config.txt`: the file is downloaded to a temp
location, limited to 256 KiB, checked against a key whitelist, shown as a diff,
backed up, and only then applied atomically. Publishing uses `gh gist create --public`.

## Mod bot (v6.0)

`aegis mod bot <mod>|--all [--upload]` performs a deep `on_content` code scan in
addition to strict manifest checks. Rejected patterns include dynamic execution,
embedded PowerShell/curl pipes, base64 payloads, external URLs in code and
`on_content=`/`payload=`/`script=` fields. Approved mods are copied to
`~/.aegis/mods-approved/` together with a `.bot.txt` receipt containing SHA256
and the check log. Only bot-approved artifacts are included in releases/installers.

## Paths

- Config: `~/.aegis/config.txt`
- User rules (optional): `~/.aegis/rules.txt`
- Log: `~/.aegis/aegis.log`
- Quarantine: `~/.aegis/quarantine/`
- Backups: `~/.aegis/backups/`
- Report export: `~/.aegis/aegis-report.txt`
- Config presets: `~/.aegis/configs/`
- Approved mods: `~/.aegis/mods-approved/`

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

GitHub Actions builds and smoke-tests on Windows, Linux, and macOS. On version tags (`v*`), CI publishes a GitHub Release with platform binaries (`aegis.exe`, `aegis-linux`, `aegis-macos`) and `SHA256SUMS.txt`.

## Mods / Tweaks (v5 platform)

- **Locations:** repo `mods/` and runtime `~/.aegis/mods`
- **Interface:** interactive **Mods/Tweaks Manager** in app + CLI (`aegis mod ui|list|info|verify|enable|disable|create|apply|publish|open`)
- **Author workflow:** scaffold with `aegis mod create <name>`, edit manifest, run `aegis mod verify <name>` and `aegis mod publish <name>`
- **Automatic verification (on publish):** strict checks validate manifest fields/format, supported apply actions, size and basic safety patterns, and generate a publish report/artifact in `~/.aegis/mods-publish/`
- **Automatic verification (CI):** `.github/workflows/mod-verify.yml` runs strict mod verification for mod-related changes/PRs
- **Examples included:** `mods/theme-white-black.aegismod` (white) and `mods/theme-white-black-black.aegismod` (black)

## A little secret

Even a shield gets lonely. If you know the three short sounds people make to call a stray cat, try typing exactly that into Aegis — in the terminal or in the menu. It is not listed in `aegis help`, of course. In v6.0 the secret grew up: `dog`, `fox`, `owl`, `penguin`, `dragon` and `zoo` (`зоопарк`) bring the whole hidden menagerie to life, each with its own 10-second in-place animation.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for full release notes (v6.0 and earlier).

## About

Aegis was created as a personal **cybersecurity** project by a **13-year-old developer** to learn systems programming in C++ and build real protection against ransomware-like threats.
