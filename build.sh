#!/usr/bin/env bash
# Compilacion rapida sin CMake (por si no esta instalado en el Chromebook)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra \
    src/main.cpp src/session.cpp src/command.cpp src/process.cpp \
    src/memory.cpp src/scanner.cpp src/pattern.cpp src/address_table.cpp \
    -o build/memorytool

g++ -std=c++17 -O0 -g -Wall -Wextra \
    tests/objetivo.cpp \
    -o build/objetivo

g++ -std=c++17 -O0 -g -Wall -Wextra \
    tests/pointer_test.cpp \
    -o build/pointer_test

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/pointer_driver.cpp src/pointer.cpp src/memory.cpp \
    -o build/pointer_driver

echo "OK: build/memorytool, build/objetivo, build/pointer_test y build/pointer_driver"
