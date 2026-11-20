# Aegis — установка из терминала (Windows).
#
#   irm https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.ps1 | iex
#
# Скачивает aegis.exe из последнего релиза, в котором он опубликован,
# проверяет SHA256 (если есть SHA256SUMS.txt) и кладёт exe в
# %LOCALAPPDATA%\Aegis\aegis.exe (можно переопределить через $env:AEGIS_INSTALL_DIR).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = 'narkomaniya/Aegis'
$asset = 'aegis.exe'
$dest = if ($env:AEGIS_INSTALL_DIR) { $env:AEGIS_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'Aegis' }
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("aegis-install-" + [guid]::NewGuid().ToString('N'))
$staged = $null
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

try {
    $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases?per_page=30" -Headers @{ 'User-Agent' = 'aegis-installer' }
    $tag = $null
    foreach ($rel in $releases) {
        if ($rel.assets | Where-Object { $_.name -eq $asset }) { $tag = $rel.tag_name; break }
    }

    if ($tag) {
        $url = "https://github.com/$repo/releases/download/$tag/$asset"
        Write-Host "Aegis: downloading $asset from $tag ..."
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile (Join-Path $tmp $asset) -Headers @{ 'User-Agent' = 'aegis-installer' }

        # v6.0: SHA256 is MANDATORY for release downloads (same policy as `aegis update`).
        # GitHub serves this file as application/octet-stream, so Invoke-WebRequest
        # returns Byte[] in Windows PowerShell — always save it and read it as text.
        $sumsUrl = "https://github.com/$repo/releases/download/$tag/SHA256SUMS.txt"
        $sumsPath = Join-Path $tmp 'SHA256SUMS.txt'
        Invoke-WebRequest -UseBasicParsing -Uri $sumsUrl -OutFile $sumsPath -Headers @{ 'User-Agent' = 'aegis-installer' }
        $sumsMap = @{}
        foreach ($sumLine in [IO.File]::ReadAllLines($sumsPath)) {
            if ($sumLine -match '^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$') {
                $sumsMap[[IO.Path]::GetFileName($Matches[2].Trim())] = $Matches[1].ToLowerInvariant()
            }
        }
        if (-not $sumsMap.ContainsKey($asset)) { throw "SHA256SUMS.txt has no entry for $asset" }
        $expected = $sumsMap[$asset]
        $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $tmp $asset)).Hash.ToLowerInvariant()
        if ($expected -ne $actual) { throw "SHA256 mismatch: expected $expected, got $actual" }
        Write-Host 'Aegis: SHA256 verified.'

        # Optional v6.0: install bot-approved mods published as release assets.
        # Every downloaded asset is verified against the same checksum file.
        # Aegis reads mods from ~/.aegis/mods, NOT from the binary install dir.
        $modHome = if ($env:USERPROFILE) { $env:USERPROFILE } else { [Environment]::GetFolderPath('UserProfile') }
        $aegisHome = Join-Path $modHome '.aegis'
        $modsDest = Join-Path $aegisHome 'mods'
        $receiptsDest = Join-Path $aegisHome 'mods-approved'
        foreach ($modAsset in ($rel.assets | Where-Object { $_.name -like '*.aegismod' -or $_.name -like '*.bot.txt' })) {
            try {
                if (-not $sumsMap.ContainsKey($modAsset.name)) { throw 'no checksum in SHA256SUMS.txt' }
                Invoke-WebRequest -UseBasicParsing -Uri $modAsset.browser_download_url -OutFile (Join-Path $tmp $modAsset.name) -Headers @{ 'User-Agent' = 'aegis-installer' }
                $modHash = (Get-FileHash -Algorithm SHA256 (Join-Path $tmp $modAsset.name)).Hash.ToLowerInvariant()
                if ($modHash -ne $sumsMap[$modAsset.name]) { throw "SHA256 mismatch for $($modAsset.name)" }
                # *.bot.txt are bot receipts (in ~/.aegis/mods-approved), not mods.
                $modTargetDir = if ($modAsset.name -like '*.bot.txt') { $receiptsDest } else { $modsDest }
                New-Item -ItemType Directory -Path $modTargetDir -Force | Out-Null
                Copy-Item -Force (Join-Path $tmp $modAsset.name) (Join-Path $modTargetDir $modAsset.name)
                Write-Host "Aegis: installed mod asset $($modAsset.name)"
            } catch {
                Write-Host "Aegis: optional mod asset $($modAsset.name) skipped: $($_.Exception.Message)"
            }
        }
    } else {
        Write-Host 'Aegis: no published binary yet, building from source ...'
        $src = Join-Path $tmp 'src'
        & git clone --depth 1 "https://github.com/$repo.git" $src
        if ($LASTEXITCODE -ne 0) { throw 'git clone failed' }
        Push-Location $src
        try {
            & g++ -std=c++17 -O2 -Wall src\*.cpp -o (Join-Path $tmp $asset) -lshell32 -lole32 -luuid
            if ($LASTEXITCODE -ne 0) { throw 'g++ build failed (MinGW required)' }
        } finally { Pop-Location }
    }

    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $target = Join-Path $dest 'aegis.exe'
    # Stage next to the target, then rename: the running copy is never truncated.
    $staged = Join-Path $dest 'aegis.exe.new'
    Copy-Item -Force (Join-Path $tmp $asset) $staged

    # Файл, скачанный из интернета, помечается Mark-of-the-Web: без снятия
    # метки Windows/SmartScreen может отказаться его запускать.
    Unblock-File -Path $staged -ErrorAction SilentlyContinue
    Move-Item -Force -LiteralPath $staged -Destination $target
    $staged = $null

    # Добавляем папку в PATH пользователя, чтобы команда `aegis` работала
    # в любом новом терминале (в текущем сеансе — сразу тоже).
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not $userPath) { $userPath = '' }
    $parts = @($userPath -split ';' | Where-Object { $_ -ne '' })
    if (-not ($parts | Where-Object { $_.TrimEnd('\') -ieq $dest.TrimEnd('\') })) {
        $parts += $dest
        [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
        Write-Host "Aegis: added $dest to your user PATH."
    }
    if (-not ($env:PATH -split ';' | Where-Object { $_.TrimEnd('\') -ieq $dest.TrimEnd('\') })) {
        $env:PATH = "$env:PATH;$dest"
    }

    Write-Host "Aegis installed: $target"
    Write-Host 'Run: aegis help'
    Write-Host "(in this terminal: `"$target`" help)"
} finally {
    if ($staged) { Remove-Item -LiteralPath $staged -Force -ErrorAction SilentlyContinue }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
