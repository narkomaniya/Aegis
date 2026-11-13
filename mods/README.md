# Aegis Mods / Tweaks (v5)

This folder contains Aegis mod/tweak manifests and examples.

## v5 highlights
- **Mods/Tweaks Manager UI** in the app (interactive interface)
- **CLI author tools:** `create`, `info`, `verify`, `enable/disable`, `apply`, `publish`
- **Automatic verification:**
  - Local: `aegis mod verify <mod>` or `--all` (`--strict` for publish-level checks)
  - Publish: `aegis mod publish <mod>` runs strict automatic checks and writes a report/artifact to `~/.aegis/mods-publish/`
  - CI: `.github/workflows/mod-verify.yml` auto-verifies mod changes/PRs

## Runtime locations
- Repo examples: `mods/`
- Installed/user mods: `~/.aegis/mods`

## Manifest format (`.aegismod`, key=value)
Recommended/required fields for publish:
- `name` (required)
- `id` (required, safe chars: letters/digits/._-)
- `version` (required)
- `author`, `type`, `status`, `description` (recommended)
- `applies` (runtime action; current supported: `ui_theme:white|black|default`)
- `min_version`, `homepage` (optional)

## Included example tweaks
- `theme-white-black.aegismod` — UI theme tweak (white)
- `theme-white-black-black.aegismod` — UI theme tweak (black)

Apply:
```bash
aegis mod verify theme-white-black --strict
aegis mod apply theme-white-black
```

## Author/publish flow
```bash
aegis mod create my-tweak
# edit ~/.aegis/mods/my-tweak.aegismod
aegis mod verify my-tweak
aegis mod publish my-tweak   # automatic strict verification + report/artifact
```
When submitting/publishing a mod, include the manifest and publish report; CI mod verification should pass.
