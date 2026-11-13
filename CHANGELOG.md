# Changelog

## v5.0 — Mod Platform, Automatic Mod Verification, and Quality Update

### Added
- **Mods/Tweaks Manager UI:** new interactive interface in the main menu and settings (`Mods / Tweaks Manager`) to browse installed mods, view details, enable/disable, apply, verify, publish, and open the mods folder.
- **Full mod CLI workflow:** `aegis mod list|path|ui|info|verify|enable|disable|create|apply|publish|open` for authors and users.
- **Automatic mod verification:** manifest validation with required-field checks, id/version format checks, file-size/safety-pattern checks, and supported-apply checks.
  - `aegis mod verify <mod>` / `--all` / `--strict` for local automatic checks
  - `aegis mod publish <mod>` runs **strict automatic verification**, generates a publish report and artifact in `~/.aegis/mods-publish/` (submission-ready flow)
- **CI auto-verification for published/submitted mods:** new GitHub Actions workflow `.github/workflows/mod-verify.yml` that builds Aegis and runs strict automatic mod verification on mod-related changes/PRs.
- **Mod state management:** enable/disable mods persisted in config (`mod_disabled=...`), richer manifest fields (`author`, `description`, `min_version`, `homepage`, etc.), and improved mod discovery.
- **Self-update integrity verification:** updater now downloads `SHA256SUMS.txt` and verifies the downloaded release asset hash before install (aborts on mismatch / missing computed hash when checksum entry is present).
- **Background/service helper commands:** `aegis service status|install|uninstall|start|stop` (best-effort, user-level) built on autostart + daemon background protection across platforms.
- **Quality/UI improvements:** updated menus/help/status for mod platform and background service controls; clearer background protection messaging.

### Included mods/tweaks
- `mods/theme-white-black.aegismod` — UI theme tweak (white)
- `mods/theme-white-black-black.aegismod` — UI theme tweak (black)
- Both can be verified/applied via the Mod Manager or `aegis mod verify/apply ...`

### Notes
- Mod runtime actions are currently focused on safe, supported tweaks (e.g. UI theme) with strong manifest verification and publish checks. The platform/API is designed for expansion (more hooks and mod capabilities can be added on top of this foundation).
- `service` commands provide cross-platform best-effort user-level background management (autostart + daemon). Native system service integration (Windows Service/systemd unit) can be layered on further where elevated privileges/environments are available.

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
