#!/usr/bin/env bash
# Compilacion rapida sin CMake (por si no esta instalado en el Chromebook)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    src/main.cpp src/session.cpp src/command.cpp src/application.cpp \
    src/process.cpp \
    src/memory.cpp src/scanner.cpp src/pattern.cpp src/address_table.cpp \
    src/pointer.cpp src/pointer_resolver.cpp \
    src/web/json.cpp src/web/jobs.cpp src/web/job_runner.cpp \
    src/web/http.cpp src/web/server.cpp \
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

g++ -std=c++17 -O0 -g -Wall -Wextra \
    tests/pointer_offset_test.cpp \
    -o build/pointer_offset_test

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/pointer_resolve_driver.cpp \
    src/pointer_resolver.cpp src/address_table.cpp src/memory.cpp \
    -o build/pointer_resolve_driver

echo "OK: memorytool, objetivo, pointer_test, pointer_driver, pointer_offset_test y pointer_resolve_driver"
