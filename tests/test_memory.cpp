// test_memory.cpp - Tests unitarios de las funciones puras de src/memory.h:
// parse_maps_line (parseo de /proc/PID/maps con strings simulados) y
// region_at (seleccion de region por direccion). No depende de un proceso
// externo; solo usa /proc/self para una comprobacion de integracion minima.
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_memory.cpp src/memory.cpp -o build/test_memory
// Ejecutar:  ./build/test_memory      (0 = exito, !=0 = fallo)
#include "memory.h"

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

using namespace mt;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (cond) {                                                      \
            ++g_pass;                                                    \
        } else {                                                         \
            ++g_fail;                                                    \
            std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        long long _a = (long long)(a);                                                   \
        long long _b = (long long)(b);                                                   \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (%lld != %lld)\n",                        \
                        __FILE__, __LINE__, #a, #b, _a, _b);                             \
        }                                                                                \
    } while (0)

#define CHECK_BITS(a, b)                                                                 \
    do {                                                                                 \
        uint64_t _a = (uint64_t)(a);                                                     \
        uint64_t _b = (uint64_t)(b);                                                     \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (0x%llx != 0x%llx)\n",                    \
                        __FILE__, __LINE__, #a, #b,                                      \
                        (unsigned long long)_a, (unsigned long long)_b);                 \
        }                                                                                \
    } while (0)

#define CHECK_STR(a, b)                                                                  \
    do {                                                                                 \
        std::string _a = (a);                                                            \
        std::string _b = (b);                                                            \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (\"%s\" != \"%s\")\n",                    \
                        __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str());             \
        }                                                                                \
    } while (0)

// ---------------------------------------------------------------------------
// parse_maps_line: lineas simuladas de /proc/PID/maps
// ---------------------------------------------------------------------------
static void test_parse_rx_with_path() {
    Region r;
    bool ok = parse_maps_line(
        "55a0b000-55a0c000 r-xp 00001000 fd:01 12345 /usr/bin/foo", r);
    CHECK(ok);
    CHECK_BITS(r.start, 0x55a0b000);
    CHECK_BITS(r.end, 0x55a0c000);
    CHECK_BITS(r.size(), 0x1000);
    // El campo perms guarda el token completo de /proc/PID/maps, incluido el
    // 4o caracter de paginacion (p/s): "r-xp", no "r-x".
    CHECK_STR(r.perms, "r-xp");
    CHECK_BITS(r.offset, 0x1000);
    CHECK_STR(r.dev, "fd:01");
    CHECK_BITS(r.inode, 12345);
    CHECK_STR(r.path, "/usr/bin/foo");
    CHECK(r.readable());
    CHECK(!r.writable());
    CHECK(r.executable());
}

static void test_parse_rw_stack() {
    Region r;
    bool ok = parse_maps_line(
        "7ffc00000000-7ffc00100000 rw-p 00000000 00:00 0 [stack]", r);
    CHECK(ok);
    CHECK_BITS(r.start, 0x7ffc00000000ull);
    CHECK_BITS(r.end, 0x7ffc00100000ull);
    CHECK_STR(r.perms, "rw-p"); // token completo, con el flag de paginacion
    CHECK_BITS(r.offset, 0);
    CHECK_BITS(r.inode, 0);
    CHECK_STR(r.path, "[stack]");
    CHECK(r.readable());
    CHECK(r.writable());
    CHECK(!r.executable());
}

static void test_parse_r_readonly() {
    Region r;
    bool ok = parse_maps_line(
        "7f0000000000-7f0000100000 r--p 0002a000 08:01 99 /lib/libx.so", r);
    CHECK(ok);
    CHECK_STR(r.perms, "r--p");
    CHECK(r.readable());
    CHECK(!r.writable());
    CHECK(!r.executable());
    CHECK_BITS(r.offset, 0x2a000);
    CHECK_BITS(r.inode, 99);
    CHECK_STR(r.path, "/lib/libx.so");
}

static void test_parse_path_with_spaces() {
    Region r;
    bool ok = parse_maps_line(
        "55a0b000-55a0c000 rw-p 00001000 00:0a 123 /home/user/mi archivo.txt", r);
    CHECK(ok);
    CHECK_STR(r.path, "/home/user/mi archivo.txt"); // el pathname conserva espacios
    CHECK_BITS(r.inode, 123);
    CHECK_STR(r.dev, "00:0a");
}

static void test_parse_no_path() {
    Region r;
    // sin pathname: la linea termina tras el inode
    bool ok = parse_maps_line("55a0b000-55a0c000 rw-p 00001000 00:0a 123", r);
    CHECK(ok);
    CHECK_STR(r.path, "");
    // con salto de linea final (como la lee fgets)
    ok = parse_maps_line("55a0b000-55a0c000 rw-p 00001000 00:0a 123\n", r);
    CHECK(ok);
    CHECK_STR(r.path, "");
}

static void test_parse_trailing_newline() {
    Region r;
    bool ok = parse_maps_line(
        "55a0b000-55a0c000 r-xp 00001000 fd:01 12345 /usr/bin/foo\n", r);
    CHECK(ok);
    CHECK_STR(r.perms, "r-xp");
    CHECK_STR(r.path, "/usr/bin/foo"); // trim elimina el \n
}

static void test_parse_malformed() {
    Region r;
    CHECK(!parse_maps_line("", r));
    CHECK(!parse_maps_line("garbage", r));
    CHECK(!parse_maps_line("55a0b000-55a0c000", r));                  // faltan campos
    CHECK(!parse_maps_line("55a0b000-55a0c000 r-xp 00001000", r));    // faltan dev/inode
    CHECK(!parse_maps_line("xxxx-55a0c000 r-xp 00001000 fd:01 5 /x", r)); // start no hex
    CHECK(!parse_maps_line("55a0b000-zzzz r-xp 00001000 fd:01 5 /x", r)); // end no hex
}

// ---------------------------------------------------------------------------
// region_at: seleccion por direccion
// ---------------------------------------------------------------------------
static std::vector<Region> sample_regions() {
    std::vector<Region> v;
    Region a;
    a.start = 0x1000; a.end = 0x2000; a.perms = "rw-"; a.path = "[stack]";
    Region b;
    b.start = 0x5000; b.end = 0x6000; b.perms = "r-x"; b.path = "/bin/x";
    Region c;
    c.start = 0x9000; c.end = 0xa000; c.perms = "r--"; c.path = "/data";
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    return v;
}

static void test_region_at() {
    auto regions = sample_regions();
    // exactamente en start
    auto ra = region_at(regions, 0x1000);
    CHECK(ra.has_value());
    CHECK_BITS(ra->start, 0x1000);
    CHECK_STR(ra->path, "[stack]");
    // exactamente antes de end (end - 1)
    auto rb = region_at(regions, 0x1FFF);
    CHECK(rb.has_value());
    CHECK_BITS(rb->start, 0x1000);
    // exactamente en end: la region es [start, end) -> NO contenida
    CHECK(!region_at(regions, 0x2000).has_value());
    CHECK(!region_at(regions, 0x6000).has_value());
    CHECK(!region_at(regions, 0xa000).has_value());
    // dentro de la segunda region
    auto r2 = region_at(regions, 0x5000);
    CHECK(r2.has_value());
    CHECK_BITS(r2->start, 0x5000);
    CHECK_STR(r2->perms, "r-x");
    CHECK_STR(r2->path, "/bin/x");
    auto r3 = region_at(regions, 0x5FFF);
    CHECK(r3.has_value());
    CHECK_BITS(r3->end, 0x6000);
    // fuera de todas
    CHECK(!region_at(regions, 0x0000).has_value());
    CHECK(!region_at(regions, 0x2FFF).has_value());
    CHECK(!region_at(regions, 0xFFFF).has_value());
    // lista vacia
    std::vector<Region> empty;
    CHECK(!region_at(empty, 0x1000).has_value());
}

// ---------------------------------------------------------------------------
// parse_maps: integracion minima con /proc/self (sin proceso externo)
// ---------------------------------------------------------------------------
static void test_parse_maps_self() {
    auto regions = parse_maps(getpid());
    CHECK(!regions.empty());
    size_t readable = 0;
    for (const auto& r : regions) {
        if (r.readable()) ++readable;
        // toda region parseada de /proc/self debe tener end > start
        CHECK(r.end > r.start);
    }
    CHECK(readable > 0);
    // la primera region legible debe poder recuperarse con region_at en su start
    for (const auto& r : regions) {
        if (!r.readable()) continue;
        auto ra = region_at(regions, r.start);
        CHECK(ra.has_value());
        CHECK_BITS(ra->start, r.start);
        break;
    }
}

int main() {
    test_parse_rx_with_path();
    test_parse_rw_stack();
    test_parse_r_readonly();
    test_parse_path_with_spaces();
    test_parse_no_path();
    test_parse_trailing_newline();
    test_parse_malformed();
    test_region_at();
    test_parse_maps_self();

    std::printf("\n== test_memory: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
