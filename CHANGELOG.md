# Changelog

## v4.2 — Mods/Tweaks foundation (in development)

### Added
- **Mods/tweaks folder:** added `mods/` in the repo and runtime mods directory support at `~/.aegis/mods`.
- **Author tool (CLI):** `aegis mod list|path|create <name>|apply <name>` to scaffold and manage tweak manifests (in development).
- **UI theme tweak (in development):** new `ui_theme` setting with `default`, `white`, and `black` accent themes for logger/status tags; configurable in Settings and via tweak manifests.
- **Example tweaks:** included `mods/theme-white-black.aegismod` (white) and `mods/theme-white-black-black.aegismod` (black), marked **in development**.

### Changed
- **Settings UI/paths:** added UI theme/tweak and Mods/tweaks entries, and mods path display.
- **Status/CLI:** `aegis status` and help now include mod/theme information.

### Notes
- The mod/tweak system is an early foundation. Manifest format and author tooling are available now; broader runtime mod loading/hooks are still **in development**.

## v4.1 — Automatic background protection (hands-off)

### Added
- **True background autostart:** enabling autostart now launches `aegis watch --daemon` automatically at login/startup, so protection runs without opening the interactive menu.
- **macOS LaunchAgent keep-alive:** autostart agent now includes `KeepAlive` to help keep the watcher running.
- **Windows hidden daemon mode:** `aegis watch --daemon` hides the console window for quiet background protection.

### Changed
- **Autostart entries updated across platforms:** Windows HKCU Run, Linux XDG autostart `.desktop`, and macOS LaunchAgent now start in daemon mode.
- **Daemon watch flow:** daemon mode skips interactive banner/loop and runs as a non-interactive watcher process (logging to `~/.aegis/aegis.log`).

### Notes
- To use the new hands-off behavior, toggle autostart off/on once (`aegis autostart off` then `aegis autostart on`) so the saved startup entry is refreshed.

## v4.0 — Biggest update (major stability, detection, UX and performance overhaul)

### Added
- **Risk/correlation engine improvements:** stronger multi-signal escalation (canary, rename bursts, entropy, ransom notes/content, suspicious/double extensions) with faster emergency response on attack patterns.
- **Integrity checks:** canary integrity verification and diagnostics improvements; setup checks for paths, permissions, backups, and rules.
- **Backup & recovery workflow:** optional backup-before-quarantine and quick rollback helpers for recent quarantine activity (mass-attack recovery aid).
- **Profiles/presets:** Balanced, Strict, Performance/Ultra-light, Documents.
- **Per-folder sensitivity** and global sensitivity tuning.
- **Exclusions/whitelist:** paths and extensions in settings plus optional `~/.aegis/rules.txt` user rules.
- **Ultra-light mode v2:** reduced heavy checks/IO for low-resource systems while keeping key protections active.
- **Interactive menu upgrades:** watch screen with live counters and recent alerts panel/buffer, quarantine management (restore/delete/clear/restore recent/open), history search/filter, diagnostics, report export.
- **Settings management:** export/import settings, auto-update check notices, autostart controls.
- **Localization foundation:** language setting and EN/RU-ready UI structure (expanded in follow-ups).
- **CLI & automation:** `status`, `watch --daemon`, and `export` (txt/json) commands for scripting/service-friendly use.
- **CI/CD release pipeline:** multi-platform build/smoke tests and tag-based GitHub Releases with binaries and SHA256 checksums.

### Changed
- **Large refactor of UI flow:** clearer zapret-style menus, better status/alerts visibility, improved quarantine/history workflows.
- **Config system expanded:** richer settings format with backward-compatible loading for older configs.
- **Quarantine/reporting:** improved record handling, search, and export.

### Fixed
- **Stability and correctness:** safer path/config handling, improved console output synchronization, better handling of edge cases in watch/quarantine/restore flows.
- **False positives/performance balance:** additional ignores, tunable thresholds, reduced unnecessary scans/IO in relevant modes.

### Security
- **Hardening measures:** canary tamper response, safer quarantine operations (with backups), integrity/setup diagnostics, and more conservative handling in ultra-light mode.

> v4.0 is the biggest Aegis update so far, focused on making protection stronger, lighter, and much easier to control.
