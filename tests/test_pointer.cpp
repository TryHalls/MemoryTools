// test_pointer.cpp - Tests unitarios del Pointer Scanner (parte pura).
//
// Cubre, con datos sinteticos y sin proceso real:
//   - classify_region / select_pointer_regions
//   - FlatHashSet (pertenencia)
//   - extend_chains: profundidad 1/2/3, reconstruccion de cadenas, cadenas
//     que comparten nodos, deteccion de ciclos por cadena, multiples
//     referencias al mismo target, target sin referencias y limites.
//
// El escaneo completo contra un proceso real se prueba en tests/e2e.sh
// (pointer_test + pointer_driver).
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer.cpp src/pointer.cpp src/memory.cpp -o build/test_pointer
// Ejecutar:  ./build/test_pointer      (0 = exito, !=0 = fallo)
#include "pointer.h"

#include <cstdio>
#include <string>
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

// ---------------------------------------------------------------------------
// classify_region
// ---------------------------------------------------------------------------
static void test_classify_region() {
    Region r;
    r.start = 0x1000; r.end = 0x2000;

    r.perms = "rw-p"; r.path = "[heap]";
    CHECK(classify_region(r) == RegionKind::HEAP);

    r.perms = "rw-p"; r.path = "[stack]";
    CHECK(classify_region(r) == RegionKind::STACK);
    r.path = "[stack:1234]";
    CHECK(classify_region(r) == RegionKind::STACK);

    r.perms = "rw-p"; r.path = "";
    CHECK(classify_region(r) == RegionKind::ANON_RW);

    r.perms = "r--p"; r.path = ""; // anonima sin escritura -> OTHER
    CHECK(classify_region(r) == RegionKind::OTHER);

    r.perms = "rw-p"; r.path = "/usr/bin/foo"; // data con archivo
    CHECK(classify_region(r) == RegionKind::DATA);

    r.perms = "r-xp"; r.path = "/usr/bin/foo";
    CHECK(classify_region(r) == RegionKind::CODE);

    r.perms = "r--p"; r.path = "/lib/libx.so"; // archivo de solo lectura
    CHECK(classify_region(r) == RegionKind::OTHER);

    r.perms = "r-xp"; r.path = "[vdso]";
    CHECK(classify_region(r) == RegionKind::OTHER);

    r.perms = "---p"; r.path = ""; // no legible
    CHECK(classify_region(r) == RegionKind::OTHER);
}

// ---------------------------------------------------------------------------
// select_pointer_regions
// ---------------------------------------------------------------------------
static void test_select_pointer_regions() {
    std::vector<Region> all;
    auto add = [&](const char* perms, const char* path, uint64_t start) {
        Region r;
        r.start = start; r.end = start + 0x1000; r.perms = perms; r.path = path;
        all.push_back(r);
    };
    add("rw-p", "[heap]", 0x10000);
    add("rw-p", "[stack]", 0x20000);
    add("rw-p", "", 0x30000);
    add("rw-p", "/usr/bin/foo", 0x40000);   // data
    add("r-xp", "/usr/bin/foo", 0x50000);   // code
    add("r--p", "/lib/libx.so", 0x60000);   // read-only file
    add("r-xp", "[vdso]", 0x70000);

    std::vector<Region> def = select_pointer_regions(all, false);
    CHECK_EQ(def.size(), (size_t)4);
    for (const Region& r : def) {
        CHECK(r.start != 0x50000); // sin code por defecto
        CHECK(r.start != 0x60000); // sin archivos r--
        CHECK(r.start != 0x70000); // sin vdso
    }
    // las cuatro incluidas estan
    bool h = false, s = false, a = false, d = false;
    for (const Region& r : def) {
        if (r.start == 0x10000) h = true;
        if (r.start == 0x20000) s = true;
        if (r.start == 0x30000) a = true;
        if (r.start == 0x40000) d = true;
    }
    CHECK(h && s && a && d);

    std::vector<Region> with_code = select_pointer_regions(all, true);
    CHECK_EQ(with_code.size(), (size_t)5);
    bool code = false;
    for (const Region& r : with_code)
        if (r.start == 0x50000) code = true;
    CHECK(code);

    // lista vacia -> vacia
    std::vector<Region> empty;
    CHECK(select_pointer_regions(empty, false).empty());
}

// ---------------------------------------------------------------------------
// FlatHashSet
// ---------------------------------------------------------------------------
static void test_flat_hash_set() {
    FlatHashSet s;
    CHECK(!s.contains(0x1234));

    s.build({0x1234, 0x5678, 0x1234}); // con duplicado
    CHECK_EQ(s.size(), (size_t)2);
    CHECK(s.contains(0x1234));
    CHECK(s.contains(0x5678));
    CHECK(!s.contains(0x9999));
    CHECK(!s.contains(0));

    s.build({}); // vacio
    CHECK(!s.contains(0x1));
    CHECK_EQ(s.size(), (size_t)0);

    // volumen pequeno: valores dispersos
    std::vector<uint64_t> many;
    for (uint64_t i = 0; i < 5000; ++i) many.push_back(i * 0x100000ULL + 0x1234);
    FlatHashSet big(many);
    CHECK_EQ(big.size(), (size_t)5000);
    for (uint64_t i = 0; i < 5000; ++i)
        CHECK(big.contains(i * 0x100000ULL + 0x1234));
    CHECK(!big.contains(0x100000ULL + 0x1234 + 1));
}

// ---------------------------------------------------------------------------
// extend_chains: reconstruccion y control de ciclos
// ---------------------------------------------------------------------------
static void test_depth1() {
    // Nivel 1: A -> TARGET
    std::vector<PointerChain> frontier = {PointerChain{{0x1000}, 0}};
    std::vector<PointerEdge> edges = {{0x2000, 0x1000}}; // source=A, target=T
    bool trunc = false;
    std::vector<PointerChain> out = extend_chains(frontier, edges, 100, trunc);
    CHECK(!trunc);
    CHECK_EQ(out.size(), (size_t)1);
    CHECK_EQ(out[0].depth, 1);
    CHECK_EQ(out[0].nodes.size(), (size_t)2);
    CHECK_BITS(out[0].nodes[0], 0x2000); // A
    CHECK_BITS(out[0].nodes[1], 0x1000); // TARGET
}

static void test_reconstruct_depth3() {
    // Reconstruccion completa con datos sinteticos, nivel a nivel:
    //   N3 -> N2 -> N1 -> TARGET
    const uint64_t T = 0x1000, N1 = 0x2000, N2 = 0x3000, N3 = 0x4000;
    bool trunc = false;

    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    frontier = extend_chains(frontier, {{N1, T}}, 100, trunc);   // nivel 1
    CHECK_EQ(frontier.size(), (size_t)1);
    CHECK_EQ(frontier[0].depth, 1);

    frontier = extend_chains(frontier, {{N2, N1}}, 100, trunc);  // nivel 2
    CHECK_EQ(frontier.size(), (size_t)1);
    CHECK_EQ(frontier[0].depth, 2);
    CHECK_BITS(frontier[0].nodes[0], N2);
    CHECK_BITS(frontier[0].nodes[1], N1);
    CHECK_BITS(frontier[0].nodes[2], T);

    frontier = extend_chains(frontier, {{N3, N2}}, 100, trunc);  // nivel 3
    CHECK(!trunc);
    CHECK_EQ(frontier.size(), (size_t)1);
    CHECK_EQ(frontier[0].depth, 3);
    CHECK_EQ(frontier[0].nodes.size(), (size_t)4);
    CHECK_BITS(frontier[0].nodes[0], N3);
    CHECK_BITS(frontier[0].nodes[1], N2);
    CHECK_BITS(frontier[0].nodes[2], N1);
    CHECK_BITS(frontier[0].nodes[3], T);
}

static void test_shared_nodes_no_global_visited() {
    // X -> Y -> TARGET  y  Z -> Y -> TARGET deben conservarse ambas (Y se
    // comparte): un visited global perderia la segunda.
    const uint64_t T = 0x1000, Y = 0x2000, X = 0x3000, Z = 0x4000;
    bool trunc = false;

    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    frontier = extend_chains(frontier, {{Y, T}}, 100, trunc); // nivel 1
    CHECK_EQ(frontier.size(), (size_t)1);

    std::vector<PointerEdge> lvl2 = {{X, Y}, {Z, Y}}; // ambos apuntan a Y
    std::vector<PointerChain> d2 = extend_chains(frontier, lvl2, 100, trunc);
    CHECK(!trunc);
    CHECK_EQ(d2.size(), (size_t)2);
    bool hasX = false, hasZ = false;
    for (const PointerChain& c : d2) {
        CHECK_EQ(c.nodes.size(), (size_t)3);
        CHECK_BITS(c.nodes[1], Y);
        CHECK_BITS(c.nodes[2], T);
        if (c.nodes[0] == X) hasX = true;
        if (c.nodes[0] == Z) hasZ = true;
    }
    CHECK(hasX && hasZ);
}

static void test_cycle_by_path() {
    // Ciclo simple: A <-> B, TARGET = B.
    // Nivel 1: A -> B ; nivel 2: B -> A -> B  (B ya esta en la cadena: se
    // descarta por el control de ciclos por cadena).
    const uint64_t B = 0x1000, A = 0x2000;
    bool trunc = false;

    std::vector<PointerChain> frontier = {PointerChain{{B}, 0}};
    frontier = extend_chains(frontier, {{A, B}}, 100, trunc); // nivel 1
    CHECK_EQ(frontier.size(), (size_t)1);

    std::vector<PointerChain> d2 = extend_chains(frontier, {{B, A}}, 100, trunc);
    CHECK(!trunc);
    CHECK(d2.empty()); // B -> A -> B descartada (ciclo)
}

static void test_cycle_mid_chain_keeps_acyclic() {
    // Ciclo entre A y T (A <-> T) con una cadena aciclica que pasa por A:
    // B -> A -> T debe conservarse; T -> A -> T no.
    const uint64_t T = 0x1000, A = 0x2000, B = 0x3000;
    bool trunc = false;

    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    frontier = extend_chains(frontier, {{A, T}}, 100, trunc); // nivel 1: [A,T]
    CHECK_EQ(frontier.size(), (size_t)1);

    // Nivel 2: B -> A (valida) y T -> A (T ya esta en [A,T]: se descarta).
    std::vector<PointerEdge> lvl2 = {{B, A}, {T, A}};
    std::vector<PointerChain> d2 = extend_chains(frontier, lvl2, 100, trunc);
    CHECK_EQ(d2.size(), (size_t)1);
    CHECK_BITS(d2[0].nodes[0], B);
    CHECK_BITS(d2[0].nodes[1], A);
    CHECK_BITS(d2[0].nodes[2], T);
}

static void test_multiple_refs_same_target() {
    // Dos referentes del mismo TARGET en el mismo nivel.
    const uint64_t T = 0x1000, A = 0x2000, B = 0x3000;
    bool trunc = false;
    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    std::vector<PointerEdge> lvl1 = {{A, T}, {B, T}};
    std::vector<PointerChain> d1 = extend_chains(frontier, lvl1, 100, trunc);
    CHECK(!trunc);
    CHECK_EQ(d1.size(), (size_t)2);
    CHECK_BITS(d1[0].nodes[0], A);
    CHECK_BITS(d1[1].nodes[0], B);
}

static void test_no_references() {
    // Target sin referencias: ningun edge -> sin cadenas.
    bool trunc = false;
    std::vector<PointerChain> frontier = {PointerChain{{0x1000}, 0}};
    std::vector<PointerEdge> empty;
    std::vector<PointerChain> out = extend_chains(frontier, empty, 100, trunc);
    CHECK(!trunc);
    CHECK(out.empty());
}

static void test_max_chains_limit() {
    // max_chains = 2 con 3 referentes del TARGET: solo 2 cadenas + flag.
    const uint64_t T = 0x1000;
    bool trunc = false;
    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    std::vector<PointerEdge> lvl1 = {{0x2000, T}, {0x3000, T}, {0x4000, T}};
    std::vector<PointerChain> out = extend_chains(frontier, lvl1, 2, trunc);
    CHECK(trunc);
    CHECK_EQ(out.size(), (size_t)2);

    // max_chains = 0: nada, flag de truncado
    trunc = false;
    out = extend_chains(frontier, lvl1, 0, trunc);
    CHECK(trunc);
    CHECK(out.empty());
}

static void test_edges_not_sorted_input_is_sorted() {
    // El contrato pide edges ordenadas por target (lower_bound). Con aristas
    // ya ordenadas el resultado debe ser deterministico.
    const uint64_t T = 0x1000;
    bool trunc = false;
    std::vector<PointerChain> frontier = {PointerChain{{T}, 0}};
    std::vector<PointerEdge> lvl1 = {{0x2000, T}, {0x3000, T}};
    std::vector<PointerChain> d1 = extend_chains(frontier, lvl1, 100, trunc);
    CHECK_EQ(d1.size(), (size_t)2);
    CHECK_BITS(d1[0].nodes[0], 0x2000);
    CHECK_BITS(d1[1].nodes[0], 0x3000);
}

int main() {
    test_classify_region();
    test_select_pointer_regions();
    test_flat_hash_set();
    test_depth1();
    test_reconstruct_depth3();
    test_shared_nodes_no_global_visited();
    test_cycle_by_path();
    test_cycle_mid_chain_keeps_acyclic();
    test_multiple_refs_same_target();
    test_no_references();
    test_max_chains_limit();
    test_edges_not_sorted_input_is_sorted();

    std::printf("\n== test_pointer: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
