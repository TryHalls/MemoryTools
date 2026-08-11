#!/usr/bin/env bash
# Compilacion rapida sin CMake (por si no esta instalado en el Chromebook)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra \
    src/main.cpp src/process.cpp src/memory.cpp src/scanner.cpp \
    -o build/memorytool

g++ -std=c++17 -O0 -g -Wall -Wextra \
    tests/objetivo.cpp \
    -o build/objetivo

echo "OK: build/memorytool y build/objetivo"
