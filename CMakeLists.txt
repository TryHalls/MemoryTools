cmake_minimum_required(VERSION 3.16)
project(MemoryTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Herramienta principal ----
add_executable(memorytool
  src/main.cpp
  src/session.cpp
  src/command.cpp
  src/process.cpp
  src/memory.cpp
  src/scanner.cpp
  src/pattern.cpp
  src/address_table.cpp
)
target_include_directories(memorytool PRIVATE src)
target_compile_options(memorytool PRIVATE -Wall -Wextra)

# ---- Programas de prueba ----
# -O0 y -g: facilitan las primeras pruebas (variables visibles, sin optimizacion)
add_executable(objetivo tests/objetivo.cpp)
target_compile_options(objetivo PRIVATE -Wall -Wextra -O0 -g)

# pointer_test: genera cadenas de punteros deliberadas para el Pointer Scanner
add_executable(pointer_test tests/pointer_test.cpp)
target_compile_options(pointer_test PRIVATE -Wall -Wextra -O0 -g)

# pointer_driver: conductor de prueba del Pointer Scanner (tests/e2e.sh)
add_executable(pointer_driver
  tests/pointer_driver.cpp
  src/pointer.cpp
  src/memory.cpp
)
target_include_directories(pointer_driver PRIVATE src)
target_compile_options(pointer_driver PRIVATE -Wall -Wextra)
