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

    # SHA256SUMS.txt лежит рядом с бинарником в релизах CI.
    if curl -fsSL --max-time 30 -o "$tmp/SHA256SUMS.txt" "$DL/$tag/SHA256SUMS.txt"; then
        expected=$(awk -v a="$asset" '$2==a {print tolower($1); exit}' "$tmp/SHA256SUMS.txt" || true)
        if [ -n "$expected" ]; then
            if command -v sha256sum >/dev/null 2>&1; then
                actual=$(sha256sum "$tmp/$asset" | awk '{print tolower($1)}')
            elif command -v shasum >/dev/null 2>&1; then
                actual=$(shasum -a 256 "$tmp/$asset" | awk '{print tolower($1)}')
            else
                actual=""
            fi
            if [ -n "$actual" ] && [ "$actual" != "$expected" ]; then
                echo "Aegis: SHA256 mismatch! expected $expected, got $actual" >&2
                exit 1
            fi
        fi
    fi
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
