#!/bin/sh
# Aegis — установка из терминала (Linux / macOS).
#
#   curl -fsSL https://raw.githubusercontent.com/narkomaniya/Aegis/main/scripts/install.sh | sh
#
# Скачивает готовый бинарник из последнего релиза, в котором он опубликован,
# проверяет SHA256 (если релиз выложил SHA256SUMS.txt) и кладёт его в
# ~/.local/bin/aegis (или в $AEGIS_INSTALL_DIR). Если бинарников нет —
# собирает из исходников (нужен C++17 компилятор).
set -eu

REPO="narkomaniya/Aegis"
API="https://api.github.com/repos/$REPO"
DL="https://github.com/$REPO/releases/download"
DEST="${AEGIS_INSTALL_DIR:-$HOME/.local/bin}"

case "$(uname -s)" in
    Linux)  asset="aegis-linux";  compiler="g++" ;;
    Darwin) asset="aegis-macos";  compiler="clang++" ;;
    *) echo "Aegis: unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac

for tool in curl; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Aegis: need '$tool'" >&2; exit 1; }
done

tmp=$(mktemp -d 2>/dev/null || mktemp -d -t aegis)
trap 'rm -rf "$tmp"' EXIT INT TERM
tag=""

# Берём самый свежий релиз, у которого реально лежит наш бинарник.
# (Случается, что релиз есть, а ассеты ещё не загружены CI.)
tags=$(curl -fsSL "$API/releases?per_page=30" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' || true)
for candidate in $tags; do
    if curl -fsSI --max-time 20 "$DL/$candidate/$asset" >/dev/null 2>&1; then
        tag="$candidate"
        break
    fi
done

if [ -n "$tag" ]; then
    echo "Aegis: downloading $asset from $tag ..."
    curl -fL --retry 3 --max-time 300 -o "$tmp/$asset" "$DL/$tag/$asset"

    # v6.0: SHA256 mandatory for release downloads (same policy as `aegis update`).
    if ! curl -fsSL --max-time 30 -o "$tmp/SHA256SUMS.txt" "$DL/$tag/SHA256SUMS.txt"; then
        echo "Aegis: SHA256SUMS.txt is missing — refusing to install an unverified binary" >&2
        exit 1
    fi
    expected=$(awk -v a="$asset" '$2==a {print tolower($1); exit}' "$tmp/SHA256SUMS.txt" || true)
    if [ -z "$expected" ]; then
        echo "Aegis: SHA256SUMS.txt has no entry for $asset" >&2
        exit 1
    fi
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$tmp/$asset" | awk '{print tolower($1)}')
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$tmp/$asset" | awk '{print tolower($1)}')
    else
        echo "Aegis: no SHA256 tool available (install sha256sum or shasum)" >&2
        exit 1
    fi
    if [ "$actual" != "$expected" ]; then
        echo "Aegis: SHA256 mismatch! expected $expected, got $actual" >&2
        exit 1
    fi
    echo "Aegis: SHA256 verified."

    # Optional v6.0: bot-approved mods published as release assets.
    # Every downloaded asset is verified against SHA256SUMS.txt as well.
    mod_dir="${HOME:-.}/.aegis/mods"
    awk 'NF>=2 {n=$2; sub(/^\*/,"",n); if (n ~ /\.(aegismod|bot\.txt)$/) print $1, n}' "$tmp/SHA256SUMS.txt" | while read -r mh mn; do
        if curl -fsSL --max-time 60 -o "$tmp/$mn" "$DL/$tag/$mn"; then
            ma=$(sha256sum "$tmp/$mn" 2>/dev/null | awk '{print tolower($1)}')
            if [ -n "$ma" ] && [ "$ma" = "$(echo "$mh" | tr 'A-Z' 'a-z')" ]; then
                mkdir -p "$mod_dir"
                cp -f "$tmp/$mn" "$mod_dir/$mn"
                echo "Aegis: installed mod asset $mn"
            else
                echo "Aegis: mod asset $mn failed SHA256 check — skipped" >&2
            fi
        fi
    done
else
    echo "Aegis: no published binary yet, building from source ..."
    command -v git >/dev/null 2>&1 || { echo "Aegis: need 'git'" >&2; exit 1; }
    command -v "$compiler" >/dev/null 2>&1 || { echo "Aegis: need '$compiler'" >&2; exit 1; }
    git clone --depth 1 "https://github.com/$REPO.git" "$tmp/src"
    cd "$tmp/src"
    if [ "$(uname -s)" = "Darwin" ]; then
        "$compiler" -std=c++17 -O2 -Wall src/*.cpp -o "$tmp/$asset" \
            -framework CoreServices -framework CoreFoundation
    else
        "$compiler" -std=c++17 -O2 -Wall src/*.cpp -o "$tmp/$asset"
    fi
fi

chmod +x "$tmp/$asset"
mkdir -p "$DEST"
install -m 755 "$tmp/$asset" "$DEST/aegis"

# macOS: снимаем карантинную метку, если она попала на файл.
if [ "$(uname -s)" = "Darwin" ]; then
    xattr -d com.apple.quarantine "$DEST/aegis" 2>/dev/null || true
fi

# Если /usr/local/bin доступен — делаем aegis доступным просто по имени.
if [ -w /usr/local/bin ] && [ ! -e /usr/local/bin/aegis ]; then
    ln -s "$DEST/aegis" /usr/local/bin/aegis 2>/dev/null || true
fi

echo "Aegis installed: $DEST/aegis"
echo "Run: aegis help (or: $DEST/aegis help)"
case ":$PATH:" in
    *":$DEST:"*) ;;
    *) echo "Tip: add $DEST to PATH: export PATH=\"\$PATH:$DEST\"" ;;
esac
