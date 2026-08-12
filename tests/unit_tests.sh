#!/usr/bin/env bash
# Compila y ejecuta los tests unitarios de MemoryTool (types.h y memory.h).
# Uso: bash tests/unit_tests.sh      (0 = todo OK, !=0 = hay fallos)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_types.cpp \
    -o build/test_types

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_memory.cpp src/memory.cpp \
    -o build/test_memory

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_address_table.cpp src/address_table.cpp \
    -o build/test_address_table

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_pointer.cpp src/pointer.cpp src/memory.cpp \
    -o build/test_pointer

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_pointer_cmd.cpp \
    src/command.cpp src/session.cpp src/scanner.cpp src/memory.cpp \
    src/pattern.cpp src/process.cpp src/address_table.cpp src/pointer.cpp \
    src/pointer_resolver.cpp \
    -o build/test_pointer_cmd

g++ -std=c++17 -O2 -Wall -Wextra -I src \
    tests/test_pointer_v2.cpp \
    src/pointer_resolver.cpp src/address_table.cpp src/memory.cpp \
    -o build/test_pointer_v2

ok=1
echo "== test_types =="
./build/test_types || ok=0
echo
echo "== test_memory =="
./build/test_memory || ok=0
echo
echo "== test_address_table =="
./build/test_address_table || ok=0
echo
echo "== test_pointer =="
./build/test_pointer || ok=0
echo
echo "== test_pointer_cmd =="
./build/test_pointer_cmd || ok=0
echo
echo "== test_pointer_v2 =="
./build/test_pointer_v2 || ok=0

if [ "$ok" = 1 ]; then
    echo
    echo "== TESTS UNITARIOS: OK =="
else
    echo
    echo "== TESTS UNITARIOS: FALLOS =="
    exit 1
fi
