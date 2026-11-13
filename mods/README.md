# Aegis Mods / Tweaks (in development)

This folder is for Aegis tweaks/mods and author experiments.

> Status: **in development**. The mod/tweak system is an early foundation (manifest format + author tooling + simple UI theme tweaks). Runtime mod loading/execution is intentionally limited for now.

## What works now
- `mods/` folder support (repo and `~/.aegis/mods` at runtime)
- Author tool (CLI): `aegis mod list|path|create <name>|apply <name>`
- Simple UI theme tweaks via `applies=ui_theme:white` or `applies=ui_theme:black` (in development)

## Manifest format (draft, `.aegismod`)
Example fields:
```ini
name=My Tweak
id=my-tweak
author=you
version=0.1.0
type=tweak
status=in-development
description=...
applies=ui_theme:white
```

## Included example tweak
- `theme-white-black.aegismod` — UI theme/color tweak (white/black), **in development**. Apply via:
  - `aegis mod apply theme-white-black` (or use Settings → UI theme/tweak)

## For authors
Create a scaffold:
```bash
aegis mod create my-tweak
```
Then edit the generated file in your `~/.aegis/mods` folder and share it. More hook points and a fuller tweak API are planned.
