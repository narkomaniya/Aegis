# test_aegis.ps1 — smoke test for Aegis:
#   1) report.docx.locked  -> SUSPICIOUS EXTENSION -> move to quarantine
#   2) secret.bin          -> random bytes         -> quarantine by entropy
#   3) homework.txt        -> normal file          -> must stay in place
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\testdata'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
# v6.0: only clean generated probe files — never delete tracked fixtures.
foreach ($f in @('report.docx.locked', 'secret.bin', 'homework.txt', 'note.txt', 'tray-probe.locked')) {
    Remove-Item -LiteralPath (Join-Path $dir $f) -Force -ErrorAction SilentlyContinue
}
Set-Content -Path "$dir\note.txt" -Value 'just a note'
$exe = Join-Path $PSScriptRoot '..\build\aegis.exe'
if (-not (Test-Path $exe)) { throw "Build Aegis first: $exe" }

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

# loopback ping can finish in well under a second, so the `q` used to arrive
# before the job had even created the malicious files. Sleep a real 10 seconds.
cmd /c "(powershell -NoProfile -Command Start-Sleep -Seconds 10 & echo quarantine & echo q) | `"$exe`" watch `"$dir`""

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
