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
  src/process.cpp
  src/memory.cpp
  src/scanner.cpp
  src/pattern.cpp
)
target_include_directories(memorytool PRIVATE src)
target_compile_options(memorytool PRIVATE -Wall -Wextra)

# ---- Programa de prueba (objetivo) ----
# -O0 y -g: facilitan las primeras pruebas (variables visibles, sin optimizacion)
add_executable(objetivo tests/objetivo.cpp)
target_compile_options(objetivo PRIVATE -Wall -Wextra -O0 -g)
