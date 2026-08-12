// test_dynamic.cpp - Tests unitarios de los valores dinamicos (string/bytes)
// del escaner First/Next.
//
// Cubre:
//  - pattern_from_text (string -> bytes exactos; vacia; limite kMaxDynamicLength)
//  - parse_pattern (bytes con wildcards; invalidos; limite compartido)
//  - pattern_window_matches (exacto y con wildcards)
//  - make_dynamic_spec (wild_pos precomputadas) y dynamic_window_changed
//    (semantica changed/unchanged sin proceso real)
//  - for_each_window con un FAKE de memoria: overlap correcto y un patron
//    que cruza el limite de bloque (4 MiB) se encuentra
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_dynamic.cpp \
//            src/pattern.cpp src/scanner.cpp src/memory.cpp -o build/test_dynamic
// Ejecutar:  ./build/test_dynamic      (0 = exito, !=0 = fallo)
#include "pattern.h"
#include "scanner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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

// --- 1) pattern_from_text (strings) -----------------------------------------

static void test_pattern_from_text() {
    std::string err;
    BytePattern p = pattern_from_text("hola", err);
    CHECK(p.valid);
    CHECK_EQ(p.size(), 4u);
    CHECK_EQ(p.bytes[0], 'h');
    CHECK_EQ(p.bytes[3], 'a');
    CHECK(!p.has_wildcards());
    for (bool m : p.mask) CHECK(m);

    // texto con espacios (longitud variable)
    p = pattern_from_text("hola memorytool", err);
    CHECK(p.valid);
    CHECK_EQ(p.size(), 15u);
    CHECK_EQ(p.bytes[4], ' ');
    CHECK_EQ(p.bytes[5], 'm');

    // string vacia rechazada
    p = pattern_from_text("", err);
    CHECK(!p.valid);
    CHECK(!err.empty());

    // limite: 4096 bytes ok, 4097 rechazada
    p = pattern_from_text(std::string(kMaxDynamicLength, 'a'), err);
    CHECK(p.valid);
    CHECK_EQ(p.size(), kMaxDynamicLength);
    p = pattern_from_text(std::string(kMaxDynamicLength + 1, 'a'), err);
    CHECK(!p.valid);
    CHECK(!err.empty());
}

// --- 2) parse_pattern (bytes con wildcards) ---------------------------------

static void test_parse_pattern_bytes() {
    BytePattern p;
    CHECK(parse_pattern("48 8B 05 ?? ?? 48 85 C0", p));
    CHECK(p.valid);
    CHECK_EQ(p.size(), 8u);
    CHECK(p.mask[0] && p.mask[1] && p.mask[2]);
    CHECK(!p.mask[3] && !p.mask[4]);
    CHECK(p.mask[5] && p.mask[6] && p.mask[7]);
    CHECK(p.has_wildcards());
    CHECK_EQ(p.bytes[0], 0x48);
    CHECK_EQ(p.bytes[1], 0x8B);
    CHECK_EQ(p.bytes[2], 0x05);
    CHECK_EQ(p.bytes[5], 0x48);

    // sin separadores: 488B05???? = 5 bytes (2 wildcards al final)
    CHECK(parse_pattern("488B05????", p));
    CHECK_EQ(p.size(), 5u);
    CHECK(!p.mask[3] && !p.mask[4]);

    // patrones invalidos
    CHECK(!parse_pattern("48 8", p));   // longitud impar
    CHECK(!parse_pattern("zz", p));     // caracter raro
    CHECK(!parse_pattern("", p));       // vacio
    CHECK(parse_pattern("??", p));      // un solo wildcard es valido
    CHECK_EQ(p.size(), 1u);
    CHECK(!p.mask[0]);

    // limite compartido (kMaxDynamicLength): 4096 bytes ok, mas no
    std::string longpat;
    for (int i = 0; i < kMaxDynamicLength; ++i) longpat += "aa ";
    CHECK(parse_pattern(longpat, p));
    CHECK_EQ(p.size(), kMaxDynamicLength);
    CHECK(!parse_pattern(longpat + "aa", p));
    CHECK(!p.error.empty());
}

// --- 3) pattern_window_matches ----------------------------------------------

static void test_matches() {
    BytePattern p;
    CHECK(parse_pattern("48 8B ?? 90", p)); // wildcard en la posicion 2
    uint8_t win1[] = {0x48, 0x8B, 0x00, 0x90};
    uint8_t win2[] = {0x48, 0x8B, 0xFF, 0x90};
    uint8_t win3[] = {0x48, 0x8B, 0x00, 0x91}; // difiere en byte exacto
    uint8_t win4[] = {0x49, 0x8B, 0x00, 0x90}; // difiere en byte exacto
    CHECK(pattern_window_matches(win1, p));
    CHECK(pattern_window_matches(win2, p));
    CHECK(!pattern_window_matches(win3, p));
    CHECK(!pattern_window_matches(win4, p));

    // string: comparacion exacta byte a byte
    std::string err;
    BytePattern sp = pattern_from_text("hola", err);
    uint8_t ok[] = {'h', 'o', 'l', 'a'};
    uint8_t bad[] = {'h', 'o', 'l', 'b'};
    CHECK(pattern_window_matches(ok, sp));
    CHECK(!pattern_window_matches(bad, sp));
}

// --- 4) make_dynamic_spec + dynamic_window_changed --------------------------

static void test_spec_and_changed() {
    std::string err;
    DynamicScanSpec str = make_dynamic_spec(
        DataType::STRING, pattern_from_text("abc", err));
    CHECK_EQ(str.length(), 3u);
    CHECK(str.wild_pos.empty());
    uint8_t cur[] = {'a', 'b', 'c'};
    uint8_t dif[] = {'a', 'b', 'x'};
    CHECK(!dynamic_window_changed(cur, str, {})); // iguales -> sin cambio
    CHECK(dynamic_window_changed(dif, str, {}));  // cambiados

    BytePattern bp;
    CHECK(parse_pattern("48 ?? 90", bp));
    DynamicScanSpec b = make_dynamic_spec(DataType::BYTES, std::move(bp));
    CHECK_EQ(b.length(), 3u);
    CHECK_EQ(b.wild_pos.size(), 1u);
    CHECK_EQ(b.wild_pos[0], 1u);
    uint8_t w1[] = {0x48, 0xAA, 0x90};
    CHECK(!dynamic_window_changed(w1, b, {0xAA})); // igual al 'anterior'
    CHECK(dynamic_window_changed(w1, b, {0xBB}));  // difiere del 'anterior'
    uint8_t w2[] = {0x49, 0xAA, 0x90};             // cambio en byte exacto
    CHECK(dynamic_window_changed(w2, b, {0xAA}));
    // prev_wild vacio (sin wildcards): solo se compara el patron
    BytePattern no_wild;
    CHECK(parse_pattern("48 8B 90", no_wild));
    DynamicScanSpec nw = make_dynamic_spec(DataType::BYTES, std::move(no_wild));
    uint8_t n1[] = {0x48, 0x8B, 0x90};
    uint8_t n2[] = {0x48, 0x8B, 0x91};
    CHECK(!dynamic_window_changed(n1, nw, {}));
    CHECK(dynamic_window_changed(n2, nw, {}));
}

// --- 5) for_each_window con fake: overlap y cruce de limite de bloque -------

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

static void test_window_boundary() {
    const size_t N = kChunkBytes + 32; // un poco mas de un bloque de 4 MiB
    FakeMem mem;
    mem.data.assign(N, 0xCC);

    // Patron que cruza el limite del bloque: empieza en kChunkBytes - 2.
    const uint64_t pat_start = kChunkBytes - 2;
    const uint8_t pat[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    std::memcpy(mem.data.data() + pat_start, pat, sizeof(pat));

    Region r;
    r.start = 0;
    r.end = N;
    r.perms = "r--";
    r.path = "";
    std::vector<Region> regions{r};

    // 1) Cada posicion de ventana se entrega exactamente una vez y en orden
    //    (sin huecos ni duplicados, incluido el cruce de bloque).
    std::vector<size_t> starts;
    for_each_window(mem, regions, 4, [&](const uint8_t*, uint64_t addr) {
        starts.push_back((size_t)addr);
        return true;
    });
    CHECK_EQ(starts.size(), N - 4 + 1);
    bool found = false;
    for (size_t a : starts)
        if (a == pat_start) found = true;
    CHECK(found);
    bool sorted = true;
    for (size_t i = 1; i < starts.size(); ++i)
        if (starts[i] <= starts[i - 1]) { sorted = false; break; }
    CHECK(sorted);

    // 2) La busqueda del patron (igual que el escaner dinamico) encuentra el
    //    patron que cruza el limite de bloque, y solo ese.
    BytePattern bp;
    CHECK(parse_pattern("AA BB CC DD", bp));
    size_t hits = 0;
    for_each_window(mem, regions, 4, [&](const uint8_t* win, uint64_t addr) {
        if (pattern_window_matches(win, bp)) {
            CHECK_EQ(addr, pat_start);
            ++hits;
        }
        return true;
    });
    CHECK_EQ(hits, 1u);
}

int main() {
    test_pattern_from_text();
    test_parse_pattern_bytes();
    test_matches();
    test_spec_and_changed();
    test_window_boundary();
    std::printf("\n== test_dynamic: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
