// test_cancel.cpp - Tests de cancelacion y progreso del core (FASE W-2).
//
// Cubre:
//  - for_each_window: cancel preactivo, cancel en mitad del recorrido,
//    progreso monotonico y <= total, 100% al terminar, total = 0
//  - scan_pattern: scan normal, cancel preactivo, cancel en mitad, progreso
//    0..100, truncado normal (!= cancelado), patron que cruza el limite de
//    bloque (4 MiB)
//
// Todo con memoria fake (sin proceso real): for_each_window y scan_pattern
// son templates genericos en 'Mem', igual que el resto de tests del chunk.
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_cancel.cpp
//            src/pattern.cpp src/pointer.cpp -o build/test_cancel
// Ejecutar:  ./build/test_cancel      (0 = exito, !=0 = fallo)
#include "chunk.h"
#include "pattern.h"
#include "pointer.h"

#include <atomic>
#include <cstdio>
#include <cstring>
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

// --- memoria fake compatible con for_each_window / scan_pattern ------------

struct FakeMem {
    std::vector<uint8_t> data;
    ssize_t read(uint64_t addr, void* buf, size_t len) const {
        if (addr >= data.size()) return -1;
        size_t n = std::min(len, data.size() - (size_t)addr);
        if (n == 0) return -1;
        std::memcpy(buf, data.data() + addr, n);
        return (ssize_t)n;
    }
};

static Region make_region(uint64_t start, uint64_t end) {
    Region r;
    r.start = start;
    r.end = end;
    r.perms = "r--";
    r.path = "";
    return r;
}

// Region anonima legible+escribible (ANON_RW para el pointer scanner).
static Region make_rw_region(uint64_t start, uint64_t end) {
    Region r = make_region(start, end);
    r.perms = "rw-p";
    return r;
}

// Escribe un uint64 little-endian en la direccion dada (memoria fake).
static void store_u64(FakeMem& mem, uint64_t addr, uint64_t v) {
    std::memcpy(mem.data.data() + addr, &v, sizeof(v));
}

// --- 1) for_each_window: cancel y progreso ---------------------------------

static void test_chunk_cancel() {
    const size_t N = 3 * kChunkBytes; // 12 MiB
    FakeMem mem;
    mem.data.assign(N, 0x11);
    std::vector<Region> regions{make_region(0, N)};

    // A) cancel preactivo: ninguna ventana visitada
    {
        std::atomic<bool> cancel{true};
        size_t visits = 0;
        for_each_window(mem, regions, 8, 1, [&](const uint8_t*, uint64_t) {
            ++visits;
            return true;
        }, &cancel);
        CHECK_EQ(visits, 0u);
    }

    // B) cancel en mitad: se detiene en la frontera del primer bloque
    {
        std::atomic<bool> cancel{false};
        size_t visits = 0;
        for_each_window(mem, regions, 8, 1, [&](const uint8_t*, uint64_t) {
            ++visits;
            if (visits >= 1000) cancel.store(true);
            return true;
        }, &cancel);
        CHECK(visits < 2 * kChunkBytes); // no completo el recorrido
        CHECK(visits >= 1000);           // avanzo algo
    }
}

static void test_chunk_progress() {
    const size_t N = 3 * kChunkBytes;
    FakeMem mem;
    mem.data.assign(N, 0x11);
    std::vector<Region> regions{make_region(0, N)};
    const uint64_t total = N;

    std::atomic<bool> cancel{false};
    uint64_t last = 0;
    bool reached_total = false;
    size_t calls = 0;
    for_each_window(mem, regions, 8, 1, [&](const uint8_t*, uint64_t) {
        return true;
    }, &cancel, [&](uint64_t scanned, uint64_t tot) {
        ++calls;
        CHECK(scanned >= last);    // monotonico
        CHECK(scanned <= tot);     // nunca supera el total
        CHECK_EQ(tot, total);      // total correcto
        last = scanned;
        if (scanned == total) reached_total = true;
    });
    CHECK(calls > 1);        // progreso por bloque (varios bloques)
    CHECK(reached_total);    // 100% al terminar correctamente

    // total = 0 (sin regiones legibles): progreso nunca llamado
    {
        Region nr = make_region(0, 4096);
        nr.perms = "---";
        std::vector<Region> nregions{nr};
        size_t c2 = 0;
        for_each_window(mem, nregions, 8, 1, [&](const uint8_t*, uint64_t) {
            return true;
        }, nullptr, [&](uint64_t, uint64_t) { ++c2; });
        CHECK_EQ(c2, 0u);
    }
}

// --- 1b) pointer_scan: scan real con memoria fake ---------------------------

static void test_pointer_depth() {
    // N3 -> N2 -> N1 -> TARGET en memoria fake.
    const uint64_t T = 0x100800, N1 = 0x100700, N2 = 0x100600, N3 = 0x100500;
    FakeMem mem;
    mem.data.assign(0x101000, 0);
    store_u64(mem, N1, T);
    store_u64(mem, N2, N1);
    store_u64(mem, N3, N2);
    std::vector<Region> regions{make_rw_region(0x100000, 0x101000)};

    PointerScanOptions o;
    o.target = T;
    o.max_offset = 0; // ventana {0} (V1): solo coincidencias exactas

    // Profundidad 1: solo [N1, T]
    o.max_depth = 1;
    PointerScanResult r1 = pointer_scan(mem, regions, o);
    CHECK(!r1.cancelled);
    CHECK_EQ(r1.chains.size(), (size_t)1);
    CHECK_EQ(r1.chains[0].depth, 1);
    CHECK_BITS(r1.chains[0].nodes[0], N1);
    CHECK_BITS(r1.chains[0].nodes[1], T);

    // Profundidad 2: [N1,T] y [N2,N1,T]
    o.max_depth = 2;
    PointerScanResult r2 = pointer_scan(mem, regions, o);
    CHECK(!r2.cancelled);
    CHECK_EQ(r2.chains.size(), (size_t)2);
    CHECK_EQ(r2.levels, 2);

    // Profundidad 3: cadena completa reconstruida
    o.max_depth = 3;
    PointerScanResult r3 = pointer_scan(mem, regions, o);
    CHECK(!r3.cancelled);
    CHECK_EQ(r3.chains.size(), (size_t)3);
    bool found = false;
    for (const PointerChain& c : r3.chains) {
        if (c.nodes.size() == 4 && c.nodes[0] == N3 && c.nodes[1] == N2 &&
            c.nodes[2] == N1 && c.nodes[3] == T)
            found = true;
    }
    CHECK(found);
}

static void test_pointer_shared_and_cycle() {
    // X -> Y -> T  y  Z -> Y -> T comparten Y (ambas deben conservarse).
    const uint64_t T = 0x100800, Y = 0x100700, X = 0x100600, Z = 0x100500;
    FakeMem mem;
    mem.data.assign(0x101000, 0);
    store_u64(mem, Y, T);
    store_u64(mem, X, Y);
    store_u64(mem, Z, Y);
    std::vector<Region> regions{make_rw_region(0x100000, 0x101000)};

    PointerScanOptions o;
    o.target = T;
    o.max_depth = 2;
    o.max_offset = 0; // ventana {0} (V1)
    PointerScanResult r = pointer_scan(mem, regions, o);
    CHECK(!r.cancelled);
    bool hasX = false, hasZ = false;
    for (const PointerChain& c : r.chains)
        if (c.nodes.size() == 3) {
            if (c.nodes[0] == X) hasX = true;
            if (c.nodes[0] == Z) hasZ = true;
        }
    CHECK(hasX && hasZ); // sin visited global

    // Ciclo A <-> B (target = B): la cadena [B, A, B] se descarta por ciclo
    // por cadena y el scan termina sin colgarse.
    const uint64_t B = 0x100800, A = 0x100700;
    FakeMem mem2;
    mem2.data.assign(0x101000, 0);
    store_u64(mem2, A, B);
    store_u64(mem2, B, A); // ciclo
    PointerScanOptions o2;
    o2.target = B;
    o2.max_depth = 3;
    o2.max_offset = 0; // ventana {0} (V1)
    PointerScanResult r2 = pointer_scan(mem2, regions, o2);
    CHECK(!r2.cancelled);
    CHECK(!r2.edges_truncated);
    CHECK(!r2.chains_truncated);
    CHECK_EQ(r2.chains.size(), (size_t)1); // solo [A, B]
    CHECK_BITS(r2.chains[0].nodes[0], A);
}

static void test_pointer_truncated() {
    // 4 punteros a T con max_edges_per_level = 3: truncado por aristas.
    const uint64_t T = 0x100800;
    FakeMem mem;
    mem.data.assign(0x101000, 0);
    for (int i = 0; i < 4; ++i)
        store_u64(mem, 0x100700 - 0x20u * (uint64_t)i, T);
    std::vector<Region> regions{make_rw_region(0x100000, 0x101000)};

    PointerScanOptions o;
    o.target = T;
    o.max_depth = 1;
    o.max_offset = 0; // ventana {0} (V1)
    o.max_edges_per_level = 3;
    PointerScanResult r = pointer_scan(mem, regions, o);
    CHECK(r.edges_truncated);
    CHECK(!r.cancelled);   // truncado != cancelado
    CHECK_EQ(r.chains.size(), (size_t)3);

    // max_chains = 2 con 3 referentes: truncado por cadenas.
    PointerScanOptions o2;
    o2.target = T;
    o2.max_depth = 1;
    o2.max_offset = 0; // ventana {0} (V1)
    o2.max_chains = 2;
    PointerScanResult r2 = pointer_scan(mem, regions, o2);
    CHECK(r2.chains_truncated);
    CHECK(!r2.cancelled);
    CHECK_EQ(r2.chains.size(), (size_t)2);
}

static void test_pointer_cancel() {
    FakeMem mem;
    mem.data.assign(0x101000, 0);
    std::vector<Region> regions{make_rw_region(0x100000, 0x101000)};
    PointerScanOptions o;
    o.target = 0x100800;
    o.max_depth = 3;

    // A) cancel preactivo: ninguna operacion, resultado cancelado sin cadenas
    {
        std::atomic<bool> cancel{true};
        PointerScanResult r = pointer_scan(mem, regions, o, &cancel);
        CHECK(r.cancelled);
        CHECK(!r.edges_truncated);
        CHECK(!r.chains_truncated);
        CHECK(r.chains.empty());
        CHECK_EQ(r.levels, 0);
    }

    // B) cancel durante un nivel: region de 9 MiB (3 bloques); el flag se
    //    activa en el progreso del primer bloque y el recorrido se detiene
    //    en la frontera del bloque siguiente (1 sola llamada de progreso)
    {
        const uint64_t end = 0x100000 + 9u * 1024u * 1024u;
        FakeMem big;
        big.data.assign(end, 0);
        std::vector<Region> big_regions{make_rw_region(0x100000, end)};
        std::atomic<bool> cancel{false};
        size_t pcalls = 0;
        PointerScanResult r = pointer_scan(big, big_regions, o, &cancel,
                                           [&](uint64_t, uint64_t) {
                                               ++pcalls;
                                               cancel.store(true);
                                           });
        CHECK(r.cancelled);
        CHECK(!r.edges_truncated);
        CHECK(!r.chains_truncated);
        CHECK(r.chains.empty()); // sin resultado parcial
        CHECK_EQ(pcalls, 1u);    // se detuvo antes del segundo bloque
    }

    // C) cancel entre niveles: nivel 1 completo (una sola llamada de
    //    progreso) y el flag se detecta antes de extender cadenas
    {
        const uint64_t T = 0x100800, N1 = 0x100700, N2 = 0x100600;
        FakeMem mem2;
        mem2.data.assign(0x101000, 0);
        store_u64(mem2, N1, T);
        store_u64(mem2, N2, N1);
        std::atomic<bool> cancel{false};
        size_t pcalls = 0;
        PointerScanOptions o2;
        o2.target = T;
        o2.max_depth = 3;
        PointerScanResult r = pointer_scan(mem2, regions, o2, &cancel,
                                           [&](uint64_t, uint64_t) {
                                               ++pcalls;
                                               cancel.store(true);
                                           });
        CHECK(r.cancelled);
        CHECK(r.chains.empty()); // el nivel 1 encontro aristas pero no se
                                 // publica nada (resultado cancelado)
        CHECK(pcalls >= 1);
    }
}

static void test_pointer_progress() {
    const uint64_t T = 0x100800, N1 = 0x100700, N2 = 0x100600;
    FakeMem mem;
    mem.data.assign(0x101000, 0);
    store_u64(mem, N1, T);
    store_u64(mem, N2, N1);
    std::vector<Region> regions{make_rw_region(0x100000, 0x101000)};

    PointerScanOptions o;
    o.target = T;
    o.max_depth = 2;
    o.max_offset = 0; // ventana {0} (V1)

    std::atomic<bool> cancel{false};
    uint64_t last = 0;
    bool reached = false;
    size_t calls = 0;
    PointerScanResult r = pointer_scan(mem, regions, o, &cancel,
                                       [&](uint64_t s, uint64_t t) {
        ++calls;
        CHECK(s >= last); // monotonico
        CHECK(s <= t);    // nunca supera el total
        last = s;
        if (s == t) reached = true;
    });
    CHECK(!r.cancelled);
    CHECK_EQ(r.chains.size(), (size_t)2);
    CHECK(calls >= 1);
    CHECK(reached); // 100% al completar normalmente
}

// --- 2) scan_pattern: normal / cancel / progreso / truncado ----------------

static BytePattern pat_aa() {
    BytePattern p;
    p.bytes = {0xAA};
    p.mask = {true};
    p.valid = true;
    return p;
}

static void test_pattern_normal() {
    const size_t N = kChunkBytes + 32; // cruza el limite de bloque
    FakeMem mem;
    mem.data.assign(N, 0xCC);
    const uint64_t a1 = 100;
    const uint64_t a2 = kChunkBytes - 2; // patron que cruza el limite
    const uint8_t pat[3] = {0xDE, 0xAD, 0xBE};
    std::memcpy(mem.data.data() + a1, pat, 3);
    std::memcpy(mem.data.data() + a2, pat, 3);

    std::vector<Region> regions{make_region(0, N)};
    BytePattern bp;
    CHECK(parse_pattern("DE AD BE", bp));

    std::atomic<bool> cancel{false};
    PatternScanResult res = scan_pattern(mem, regions, bp, &cancel);
    CHECK(!res.cancelled);
    CHECK(!res.truncated);
    CHECK_EQ(res.hits.size(), 2u);
    bool f1 = false, f2 = false;
    for (uint64_t h : res.hits) {
        if (h == a1) f1 = true;
        if (h == a2) f2 = true;
    }
    CHECK(f1);
    CHECK(f2); // patron que cruza el limite de bloque se encuentra
}

static void test_pattern_cancel() {
    const size_t N = 3 * kChunkBytes;
    FakeMem mem;
    mem.data.assign(N, 0xCC);
    std::memcpy(mem.data.data() + 100, "\xAA\xBB\xCC", 3);
    std::vector<Region> regions{make_region(0, N)};
    BytePattern bp;
    CHECK(parse_pattern("AA BB CC", bp));

    // A) cancel preactivo: cancelled, sin hits
    {
        std::atomic<bool> cancel{true};
        PatternScanResult res = scan_pattern(mem, regions, bp, &cancel);
        CHECK(res.cancelled);
        CHECK(!res.truncated);
        CHECK(res.hits.empty());
    }

    // B) cancel en mitad: se activa tras el primer bloque (dentro del
    //    callback de progreso); el recorrido se detiene en la frontera del
    //    bloque siguiente y el resultado cancelado no publica hits parciales
    {
        std::atomic<bool> cancel{false};
        size_t progress_calls = 0;
        PatternScanResult res =
            scan_pattern(mem, regions, bp, &cancel,
                         [&](uint64_t, uint64_t) {
                             ++progress_calls;
                             cancel.store(true);
                         });
        CHECK(res.cancelled);
        CHECK(!res.truncated);
        CHECK(res.hits.empty());       // no se publica el hit parcial
        CHECK(progress_calls > 0);     // el recorrido empezo
    }
}

static void test_pattern_progress() {
    const size_t N = 3 * kChunkBytes;
    FakeMem mem;
    mem.data.assign(N, 0xCC);
    std::memcpy(mem.data.data() + 100, "\xAA\xBB\xCC", 3);
    std::vector<Region> regions{make_region(0, N)};
    BytePattern bp;
    CHECK(parse_pattern("AA BB CC", bp));

    std::atomic<bool> cancel{false};
    uint64_t last = 0;
    bool reached = false;
    size_t calls = 0;
    PatternScanResult res = scan_pattern(mem, regions, bp, &cancel,
                                         [&](uint64_t s, uint64_t t) {
        ++calls;
        CHECK(s >= last);   // monotonico
        CHECK(s <= t);      // nunca supera el total
        last = s;
        if (s == t) reached = true;
    });
    CHECK(!res.cancelled);
    CHECK_EQ(res.hits.size(), 1u);
    CHECK(calls > 1);   // progreso por bloque
    CHECK(reached);     // 100% al completar
}

static void test_pattern_truncated() {
    // 5 MiB + 1 byte de 0xAA con patron "AA" (1 byte): mas de 5M de hits
    // -> truncado por limite, NO cancelado.
    const size_t N = 5u * 1024u * 1024u + 1;
    FakeMem mem;
    mem.data.assign(N, 0xAA);
    std::vector<Region> regions{make_region(0, N)};
    BytePattern p = pat_aa();

    std::atomic<bool> cancel{false};
    PatternScanResult res = scan_pattern(mem, regions, p, &cancel);
    CHECK(res.truncated);
    CHECK(!res.cancelled); // cancelado != truncado
    CHECK_EQ(res.hits.size(), 5u * 1000u * 1000u); // exactamente el limite
}

int main() {
    test_chunk_cancel();
    test_chunk_progress();
    test_pointer_depth();
    test_pointer_shared_and_cycle();
    test_pointer_truncated();
    test_pointer_cancel();
    test_pointer_progress();
    test_pattern_normal();
    test_pattern_cancel();
    test_pattern_progress();
    test_pattern_truncated();
    std::printf("\n== test_cancel: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
