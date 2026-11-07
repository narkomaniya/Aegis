# test_aegis.ps1 — smoke test for Aegis:
#   1) report.docx.locked  -> SUSPICIOUS EXTENSION -> move to quarantine
#   2) secret.bin          -> random bytes         -> quarantine by entropy
#   3) homework.txt        -> normal file          -> must stay in place
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\testdata'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Set-Content -Path "$dir\note.txt" -Value 'just a note'

$job = Start-Job -ScriptBlock {
    param($d)
    Start-Sleep -Seconds 3
    Set-Content -Path "$d\report.docx.locked" -Value 'ATTACK'
    $bytes = New-Object byte[] 16384
    (New-Object Random 42).NextBytes($bytes)
    [IO.File]::WriteAllBytes("$d\secret.bin", $bytes)
    Start-Sleep -Milliseconds 800
    Set-Content -Path "$d\homework.txt" -Value 'homework'
} -ArgumentList $dir

cmd /c "(ping -n 10 127.0.0.1 >nul & echo quarantine & echo q) | build\aegis.exe watch `"$dir`""

Receive-Job -Wait $job | Out-Null
"=== testdata after test ==="
(Get-ChildItem $dir).Name
"=== quarantine records ==="
$q = Join-Path $env:USERPROFILE '.aegis\quarantine'
(Get-ChildItem $q -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending |
    Select-Object -First 6).Name
Get-ChildItem $q -Recurse -Filter meta.txt -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 2 | ForEach-Object {
        "--- meta: " + $_.Directory.Name
        Get-Content $_.FullName -Encoding UTF8
    }
