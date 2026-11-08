# 🛡️ Aegis

**Aegis** is a guardian for your files. It watches your folders (Desktop, Documents,
Downloads), detects ransomware-like behavior the way real antivirus engines do,
and takes dangerous files into quarantine before they can be encrypted or corrupted.

```
┌─────────────┐    OS events    ┌──────────┐   verdict   ┌────────────┐
│   Watcher   │ ──────────────► │  Engine  │ ──────────► │ Quarantine │
│ (monitoring)│                 │ (rules)  │             │  (custody) │
└─────────────┘                 └──────────┘             └────────────┘
```

## How it works (v0.2)

- **Watcher** — uses the OS file-change API to receive events: file created,
  modified, deleted, renamed. `ReadDirectoryChangesW` on Windows,
  `inotify` on Linux, `FSEvents` on macOS (one background thread per platform).
- **Engine** — evaluates every event with a set of heuristics:
  1. **Dangerous extension** (`.locked`, `.enc`, `.wnry`, `.cerber`, ...) →
     **high threat** → the file is moved into quarantine (it disappears
     from disk, but the program keeps it safe).
  2. **High entropy** (above 7.5 of 8.0) in a newly created or modified file —
     a classic sign of encryption. First 8 KiB of the file are sampled.
     Already-compressed formats (zip, jpg, mp4, exe...) are excluded —
     high entropy is normal there.
  3. **Mass modification rate**: when more than 150 files change within a second,
     Aegis triggers an emergency mode for 30 seconds — every new file is
     copied into quarantine.
- **Quarantine** — each taken file lives in its own folder with a `meta.txt`
  (original path, time, and the reason it was taken). Nothing is destroyed:
  "delete" here means "remove from the victim folder, keep in custody".

## Build

Requires a C++17 compiler. On Windows you can simply run:

```
build.bat
```

Or manually:

```
g++ -std=c++17 -O2 -Wall src\*.cpp -o build\aegis.exe
```

## Run

```
build\aegis.exe help          # help
build\aegis.exe watch         # watch your real Desktop, Documents, Downloads
                              # (Known Folders API: OneDrive-aware)
build\aegis.exe watch D:\dir  # watch a specific folder
build\aegis.exe quarantine    # show quarantine records
build\aegis.exe restore       # quarantine records with numbers
build\aegis.exe restore 2     # take file #2 back out of quarantine
```

Interactive commands while watching: `status`, `quarantine`, `help`, `q` (exit).
Logs are written to `%USERPROFILE%\.aegis\aegis.log`.

## Download

Ready-to-run binaries are published on the
[Releases page](https://github.com/narkomaniya/Aegis/releases) —
no compiler needed.

## Roadmap

- [x] Windows: monitoring, heuristics, quarantine, CLI (release binaries)
- [x] `aegis restore <n>` — take a file back out of quarantine (Windows + Linux + macOS)
- [x] Linux: the same loop on top of `inotify` (compiled and smoke-tested in CI)
- [x] macOS: the same loop on top of `FSEvents` (beta)
- [ ] Better detection: process attribution (who touched the file), YARA rules
- [ ] Self-update + auto-start with the system
- [ ] Signed / notarized macOS builds

## About the project

Aegis is a personal educational project in systems programming with C++:
OS-level APIs, multithreading, filesystem work, and the fundamentals of
information security.
