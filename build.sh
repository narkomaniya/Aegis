#!/usr/bin/env bash
# === Aegis build (Linux / macOS) ===
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall src/*.cpp -o build/aegis
echo "[OK] build/aegis собран"
