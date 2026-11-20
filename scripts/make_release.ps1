# make_release.ps1 - builds and publishes an Aegis release (v6.0+).
# Mods are included ONLY after the mod bot produced approval receipts.
param(
    [string]$Tag = 'v6.0',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repoRoot 'build\release-assets'
$exe = Join-Path $repoRoot 'build\aegis.exe'
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) { $gh = 'C:\Program Files\GitHub CLI\gh.exe' }

if (-not $SkipBuild) {
    Write-Host "Aegis: building $exe ..."
    Push-Location $repoRoot
    try {
        & g++ -std=c++17 -O2 -Wall src\*.cpp -o $exe -lshell32 -lole32 -luuid
        if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    } finally { Pop-Location }
}

Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -Force $exe (Join-Path $out 'aegis.exe')

# Bot-approved mods (receipt + artifact) are the only mods allowed into a release.
$approved = Join-Path $env:USERPROFILE '.aegis\mods-approved'
if (Test-Path $approved) {
    Get-ChildItem -Path $approved -File | Where-Object { $_.Extension -in '.aegismod', '.txt' } | ForEach-Object {
        Copy-Item -Force $_.FullName (Join-Path $out $_.Name)
        Write-Host "Aegis: including approved mod asset $($_.Name)"
    }
}

Push-Location $out
try {
    $names = Get-ChildItem -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Select-Object -ExpandProperty Name
    $lines = foreach ($n in $names) {
        $h = (Get-FileHash -Algorithm SHA256 (Join-Path $out $n)).Hash.ToLowerInvariant()
        "$h  $n"
    }
    $lines | Set-Content -Path (Join-Path $out 'SHA256SUMS.txt') -Encoding ASCII
} finally { Pop-Location }

$notes = @"
Aegis $Tag — global update: system tray + MAXIMUM SECURITY mode, secret zoo,
safe config presets (import by link with diff/backup), Doctor with Security Score,
and the mod bot with deep ``on_content`` code checks and signed release artifacts.

Install (Windows):
    irm https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.ps1 | iex

Install (Linux/macOS):
    curl -fsSL https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.sh | sh

Every release download is verified against SHA256SUMS.txt.
"@
$notesPath = Join-Path $env:TEMP 'aegis-release-notes.md'
Set-Content -Path $notesPath -Value $notes -Encoding UTF8

$assets = Get-ChildItem -File $out | Select-Object -ExpandProperty FullName
$existing = & $gh release view $Tag --repo narkomaniya/Aegis 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "Aegis: uploading assets to existing release $Tag ..."
    & $gh release upload $Tag $assets --clobber --repo narkomaniya/Aegis
} else {
    Write-Host "Aegis: creating release $Tag ..."
    & $gh release create $Tag $assets --title "Aegis $Tag — Maximum Security" --notes-file $notesPath --repo narkomaniya/Aegis
}
if ($LASTEXITCODE -ne 0) { throw "gh release failed for $Tag" }
Write-Host "Aegis release $Tag is ready: $out"
