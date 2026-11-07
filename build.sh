#!/usr/bin/env bash
# === Aegis build (Linux / macOS) ===
set -e
cd "$(dirname "$0")"
mkdir -p build

if [[ "$(uname)" == "Darwin" ]]; then
    # macOS: нужен CoreServices (FSEvents) + CoreFoundation (CFRunLoop)
    g++ -std=c++17 -O2 -Wall src/*.cpp -o build/aegis \
        -framework CoreServices -framework CoreFoundation
else
    g++ -std=c++17 -O2 -Wall src/*.cpp -o build/aegis
fi

echo "[OK] build/aegis собран"
