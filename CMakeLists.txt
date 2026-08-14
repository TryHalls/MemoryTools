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
  src/application.cpp
  src/process.cpp
  src/memory.cpp
  src/scanner.cpp
  src/pattern.cpp
  src/address_table.cpp
  src/pointer.cpp
  src/pointer_resolver.cpp
  src/web/json.cpp
  src/web/jobs.cpp
  src/web/job_runner.cpp
)
target_include_directories(memorytool PRIVATE src)
target_compile_options(memorytool PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(memorytool PRIVATE Threads::Threads)

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

# pointer_offset_test: cadena con offsets y raiz modulo-relative (V2)
add_executable(pointer_offset_test tests/pointer_offset_test.cpp)
target_compile_options(pointer_offset_test PRIVATE -Wall -Wextra -O0 -g)

# pointer_resolve_driver: conductor del PointerResolver V2 (tests/e2e.sh)
add_executable(pointer_resolve_driver
  tests/pointer_resolve_driver.cpp
  src/pointer_resolver.cpp
  src/address_table.cpp
  src/memory.cpp
)
target_include_directories(pointer_resolve_driver PRIVATE src)
target_compile_options(pointer_resolve_driver PRIVATE -Wall -Wextra)
