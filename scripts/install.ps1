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

        try {
            $sumsUrl = "https://github.com/$repo/releases/download/$tag/SHA256SUMS.txt"
            $sums = (Invoke-WebRequest -UseBasicParsing -Uri $sumsUrl -Headers @{ 'User-Agent' = 'aegis-installer' }).Content
            $line = $sums -split "`n" | Where-Object { $_ -match "\s$([regex]::Escape($asset))\s*$" } | Select-Object -First 1
            if ($line) {
                $expected = ($line -split '\s+')[0].ToLowerInvariant()
                $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $tmp $asset)).Hash.ToLowerInvariant()
                if ($expected -ne $actual) { throw "SHA256 mismatch: expected $expected, got $actual" }
            }
        } catch {
            # Отсутствие контрольных сумм не фатально: проверяем, только если они есть.
            if ($_.Exception.Message -like '*SHA256 mismatch*') { throw }
            Write-Host 'Aegis: SHA256SUMS.txt unavailable, skipping checksum.'
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
    Copy-Item -Force (Join-Path $tmp $asset) $target
    Write-Host "Aegis installed: $target"
    Write-Host "Run: `"$target`" help"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
