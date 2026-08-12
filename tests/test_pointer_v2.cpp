// test_pointer_v2.cpp - Tests unitarios de la infraestructura V2 del Pointer
// Scanner: PointerBase/PointerChainRef, conversion de raiz absoluta a
// modulo+offset, calculo inverso (resolve_root), resolucion de cadenas con
// offsets sobre buffers sinteticos (follow_chain), independencia entre el
// kind 'pointer' y el value_type, y los formatos save/load v1+v2 de la
// AddressTable.
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer_v2.cpp \
//       src/pointer_resolver.cpp src/address_table.cpp src/memory.cpp \
//       -o build/test_pointer_v2
// Ejecutar: ./build/test_pointer_v2   (0 = exito, !=0 = fallo)
#include "address_table.h"
#include "memory.h"
#include "pointer.h"
#include "pointer_resolver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// --- Regiones sinteticas ----------------------------------------------------

static std::vector<Region> make_regions() {
    std::vector<Region> v;
    Region mod_code;
    mod_code.start = 0x400000; mod_code.end = 0x401000;
    mod_code.perms = "r-xp"; mod_code.offset = 0x0; mod_code.path = "/bin/testprog";
    Region mod_ro;
    mod_ro.start = 0x500000; mod_ro.end = 0x501000;
    mod_ro.perms = "r--p"; mod_ro.offset = 0x1000; mod_ro.path = "/bin/testprog";
    Region mod_data;
    mod_data.start = 0x601000; mod_data.end = 0x602000;
    mod_data.perms = "rw-p"; mod_data.offset = 0x2000; mod_data.path = "/bin/testprog";
    Region heap;
    heap.start = 0x7f0000000000ull; heap.end = 0x7f0000100000ull;
    heap.perms = "rw-p"; heap.path = "[heap]";
    Region anon;
    anon.start = 0x700000; anon.end = 0x710000;
    anon.perms = "rw-p"; anon.path = "";
    Region stack;
    stack.start = 0x7ffc00000000ull; stack.end = 0x7ffc00100000ull;
    stack.perms = "rw-p"; stack.path = "[stack]";
    v.push_back(mod_code); v.push_back(mod_ro); v.push_back(mod_data);
    v.push_back(heap); v.push_back(anon); v.push_back(stack);
    return v;
}

// --- PointerBase: conversion absoluto -> module+offset ----------------------

static void test_make_base() {
    const std::vector<Region> r = make_regions();

    // direccion en el data del modulo -> MODULE con pathname y offset
    PointerBase b = make_base_from_address(r, 0x601234);
    CHECK(b.kind == PointerBaseKind::MODULE);
    CHECK_STR(b.module, "/bin/testprog");
    CHECK_BITS(b.offset, 0x2000 + (0x601234 - 0x601000)); // = 0x2234

    // direccion en el codigo del modulo -> MODULE, offset dentro del mapping
    b = make_base_from_address(r, 0x400100);
    CHECK(b.kind == PointerBaseKind::MODULE);
    CHECK_BITS(b.offset, 0x100);

    // heap / anonima / stack -> ABSOLUTE (no se finge persistencia)
    b = make_base_from_address(r, 0x7f0000050000ull);
    CHECK(b.kind == PointerBaseKind::ABSOLUTE);
    CHECK_BITS(b.address, 0x7f0000050000ull);
    b = make_base_from_address(r, 0x705000);
    CHECK(b.kind == PointerBaseKind::ABSOLUTE);
    CHECK_BITS(b.address, 0x705000);
    b = make_base_from_address(r, 0x7ffc00010000ull);
    CHECK(b.kind == PointerBaseKind::ABSOLUTE);

    // fuera de todas las regiones -> ABSOLUTE
    b = make_base_from_address(r, 0x9999);
    CHECK(b.kind == PointerBaseKind::ABSOLUTE);
}

// --- resolve_root: calculo inverso (module+offset -> absoluto) --------------

static void test_resolve_root() {
    const std::vector<Region> r = make_regions();
    uint64_t out = 0;
    std::string err;

    PointerBase mod;
    mod.kind = PointerBaseKind::MODULE;
    mod.module = "/bin/testprog";
    mod.offset = 0x2234; // data
    CHECK(resolve_root(mod, r, out, err));
    CHECK_BITS(out, 0x601234);
    mod.offset = 0x100; // codigo
    CHECK(resolve_root(mod, r, out, err));
    CHECK_BITS(out, 0x400100);

    // offset fuera de todas las mappings del modulo
    mod.offset = 0x5000;
    CHECK(!resolve_root(mod, r, out, err));
    CHECK(err.find("offset fuera") != std::string::npos);

    // modulo inexistente
    mod.module = "/bin/otro";
    mod.offset = 0x100;
    CHECK(!resolve_root(mod, r, out, err));
    CHECK(err.find("modulo no encontrado") != std::string::npos);

    // ABSOLUTE: devuelve la direccion guardada
    PointerBase abs;
    abs.kind = PointerBaseKind::ABSOLUTE;
    abs.address = 0x1234;
    CHECK(resolve_root(abs, r, out, err));
    CHECK_BITS(out, 0x1234);
}

// --- follow_chain con buffer sintetico ---------------------------------------

struct FakeMem {
    std::vector<uint8_t> buf;
    explicit FakeMem(size_t n) : buf(n, 0) {}
    ssize_t read(uint64_t addr, void* out, size_t len) {
        if (addr + len > buf.size()) return -1;
        std::memcpy(out, buf.data() + addr, len);
        return (ssize_t)len;
    }
    void put_u64(uint64_t addr, uint64_t v) {
        std::memcpy(buf.data() + addr, &v, sizeof(v));
    }
    void put_u32(uint64_t addr, uint32_t v) {
        std::memcpy(buf.data() + addr, &v, sizeof(v));
    }
};

static std::vector<Region> fake_regions() {
    Region big;
    big.start = 0; big.end = 0x100000;
    big.perms = "rw-p"; big.path = "";
    return {big};
}

static PointerChainRef abs_chain(uint64_t root, std::vector<uint64_t> offsets,
                                 DataType vt) {
    PointerChainRef c;
    c.root.kind = PointerBaseKind::ABSOLUTE;
    c.root.address = root;
    c.offsets = std::move(offsets);
    c.value_type = vt;
    return c;
}

static void test_follow_chain() {
    FakeMem fm(0x100000);
    const std::vector<Region> r = fake_regions();
    auto rd = [&](uint64_t a, void* b, size_t n) -> ssize_t {
        return fm.read(a, b, n);
    };

    // offsets [0x20, 0x18]: root -> p1 -> +0x20 -> p2 -> +0x18 -> valor 4242
    fm.put_u64(0x8000, 0x10000);
    fm.put_u64(0x10020, 0x20000);
    fm.put_u32(0x20018, 4242);
    ResolveResult r1 = follow_chain(abs_chain(0x8000, {0x20, 0x18}, DataType::I32),
                                    0x8000, r, rd);
    CHECK(r1.ok);
    CHECK_BITS(r1.address, 0x20018);
    CHECK_BITS(r1.value.bits, 4242);

    // offsets [] (depth 0): el valor se lee directamente en la raiz
    fm.put_u32(0x9000, 7);
    ResolveResult r0 = follow_chain(abs_chain(0x9000, {}, DataType::I32), 0x9000, r, rd);
    CHECK(r0.ok);
    CHECK_BITS(r0.address, 0x9000);
    CHECK_BITS(r0.value.bits, 7);

    // offsets [0]: un deref, valor en la direccion apuntada
    fm.put_u64(0x9000, 0x10000);
    fm.put_u32(0x10000, 9);
    ResolveResult r2 = follow_chain(abs_chain(0x9000, {0}, DataType::I32), 0x9000, r, rd);
    CHECK(r2.ok);
    CHECK_BITS(r2.address, 0x10000);
    CHECK_BITS(r2.value.bits, 9);

    // offsets [0x20]: un deref, valor en puntero+0x20
    fm.put_u32(0x10020, 11);
    ResolveResult r3 = follow_chain(abs_chain(0x9000, {0x20}, DataType::I32), 0x9000, r, rd);
    CHECK(r3.ok);
    CHECK_BITS(r3.address, 0x10020);
    CHECK_BITS(r3.value.bits, 11);

    // cadena rota: un puntero INTERMEDIO apunta fuera de las regiones
    // legibles (profundidad 2: el segundo deref ocurre en 0x500000)
    fm.put_u64(0x9000, 0x500000);
    ResolveResult rb = follow_chain(abs_chain(0x9000, {0, 0}, DataType::I32),
                                    0x9000, r, rd);
    CHECK(!rb.ok);
    CHECK(rb.error.find("cadena rota") != std::string::npos);

    // puntero no legible: lectura de 8 bytes que se sale del buffer
    ResolveResult rp = follow_chain(abs_chain(0xFFFFC, {0}, DataType::I32),
                                    0xFFFFC, r, rd);
    CHECK(!rp.ok);
    CHECK(rp.error.find("puntero no legible") != std::string::npos);

    // valor final no legible: lectura de 4 bytes que se sale del buffer
    ResolveResult rf = follow_chain(abs_chain(0xFFFFE, {}, DataType::I32),
                                    0xFFFFE, r, rd);
    CHECK(!rf.ok);
    CHECK(rf.error.find("valor final") != std::string::npos);
}

// --- value_type independiente del kind 'pointer' -----------------------------

static void test_value_type_independent() {
    FakeMem fm(0x100000);
    const std::vector<Region> r = fake_regions();
    auto rd = [&](uint64_t a, void* b, size_t n) -> ssize_t {
        return fm.read(a, b, n);
    };
    fm.put_u64(0x8000, 0x10000);
    fm.put_u32(0x10020, 0x3FC00000u); // 1.5f
    PointerChainRef c = abs_chain(0x8000, {0x20}, DataType::F32);
    ResolveResult r1 = follow_chain(c, 0x8000, r, rd);
    CHECK(r1.ok);
    float f;
    std::memcpy(&f, &r1.value.bits, sizeof(f));
    CHECK(f == 1.5f);

    // en la tabla: type = value_type (F32), nunca 'pointer'
    AddressTable t;
    t.add(c, "velocidad");
    const AddressEntry* e = t.get(0);
    CHECK(e != nullptr);
    CHECK(e->type == DataType::F32);
    CHECK(e->ptr.has_value());
    CHECK(e->ptr->value_type == DataType::F32);
    CHECK(e->ptr->root.kind == PointerBaseKind::ABSOLUTE);

    PointerChainRef ci = abs_chain(0x9000, {0x20}, DataType::I32);
    t.add(ci, "vida");
    CHECK(t.get(1)->type == DataType::I32);
    CHECK(t.get(1)->ptr.has_value());
}

// --- make_chain_ref (V1 -> V2) ----------------------------------------------

static void test_make_chain_ref() {
    const std::vector<Region> r = make_regions();
    // raiz en modulo
    PointerChainRef ref = make_chain_ref(r, {0x601234, 0x7f0000050000ull, 0x500000},
                                         DataType::I32);
    CHECK(ref.root.kind == PointerBaseKind::MODULE);
    CHECK_STR(ref.root.module, "/bin/testprog");
    CHECK_BITS(ref.root.offset, 0x2234);
    CHECK_EQ(ref.offsets.size(), 2); // derefs = nodes-1, todos a 0
    CHECK_BITS(ref.offsets[0], 0);
    CHECK_BITS(ref.offsets[1], 0);
    CHECK(ref.value_type == DataType::I32);

    // raiz en heap -> ABSOLUTE
    ref = make_chain_ref(r, {0x7f0000050000ull, 0x500000}, DataType::I32);
    CHECK(ref.root.kind == PointerBaseKind::ABSOLUTE);
    CHECK_BITS(ref.root.address, 0x7f0000050000ull);
    CHECK_EQ(ref.offsets.size(), 1);
}

// --- save/load v2 + compatibilidad v1 ----------------------------------------

static std::string tmp_file() {
    return "/tmp/mt_v2_" + std::to_string((long)getpid()) + ".txt";
}

static void test_save_load_v2() {
    AddressTable t;
    PointerChainRef mod;
    mod.root.kind = PointerBaseKind::MODULE;
    mod.root.module = "/bin/testprog";
    mod.root.offset = 0x2234;
    mod.offsets = {0x20, 0x18};
    mod.value_type = DataType::I32;
    t.add(mod, "player health con espacios");

    PointerChainRef abs;
    abs.root.kind = PointerBaseKind::ABSOLUTE;
    abs.root.address = 0x1234;
    abs.offsets = {}; // depth 0
    abs.value_type = DataType::F32;
    t.add(abs, "absoluta");

    t.add(0x1000, DataType::I32, "v1"); // entrada absoluta clasica

    const std::string path = tmp_file();
    std::string err;
    CHECK(t.save(path, err));

    // el archivo contiene la linea v2 con el formato esperado
    FILE* f = fopen(path.c_str(), "r");
    CHECK(f != nullptr);
    char line[4096];
    bool saw_v2 = false;
    while (fgets(line, sizeof line, f)) {
        if (std::strstr(line, "pointer type=int32 module=/bin/testprog root=0x2234") &&
            std::strstr(line, "steps=0x20,0x18")) {
            saw_v2 = true;
        }
    }
    fclose(f);
    CHECK(saw_v2);

    AddressTable u;
    CHECK(u.load(path, err));
    CHECK_EQ(u.size(), 3);

    const AddressEntry* e0 = u.get(0);
    CHECK(e0 != nullptr);
    CHECK(e0->ptr.has_value());
    CHECK(e0->type == DataType::I32);          // value_type, no 'pointer'
    CHECK(e0->ptr->value_type == DataType::I32);
    CHECK(e0->ptr->root.kind == PointerBaseKind::MODULE);
    CHECK_STR(e0->ptr->root.module, "/bin/testprog");
    CHECK_BITS(e0->ptr->root.offset, 0x2234);
    CHECK_EQ(e0->ptr->offsets.size(), 2);
    CHECK_BITS(e0->ptr->offsets[0], 0x20);
    CHECK_BITS(e0->ptr->offsets[1], 0x18);
    CHECK_STR(e0->description, "player health con espacios");
    CHECK(e0->enabled);

    const AddressEntry* e1 = u.get(1);
    CHECK(e1 != nullptr);
    CHECK(e1->ptr.has_value());
    CHECK(e1->type == DataType::F32);
    CHECK(e1->ptr->root.kind == PointerBaseKind::ABSOLUTE);
    CHECK_BITS(e1->ptr->root.address, 0x1234);
    CHECK(e1->ptr->offsets.empty());
    CHECK_BITS(e1->address, 0x1234); // la base absoluta se conserva

    const AddressEntry* e2 = u.get(2);
    CHECK(e2 != nullptr);
    CHECK(!e2->ptr.has_value()); // v1 intacta
    CHECK_BITS(e2->address, 0x1000);
    CHECK(e2->type == DataType::I32);

    std::remove(path.c_str());
}

static void test_v1_compat() {
    // una tabla v1 pura sigue guardandose en el formato v1
    AddressTable t;
    t.add(0x7F1234567890ull, DataType::I32, "dinero");
    const std::string path = tmp_file();
    std::string err;
    CHECK(t.save(path, err));

    FILE* f = fopen(path.c_str(), "r");
    CHECK(f != nullptr);
    char line[4096];
    bool saw_v1 = false;
    while (fgets(line, sizeof line, f))
        if (std::strstr(line, "0x00007f1234567890 int32 1 \"dinero\"")) saw_v1 = true;
    fclose(f);
    CHECK(saw_v1);

    // y carga sin entradas dinamicas (ptr == nullopt)
    AddressTable u;
    CHECK(u.load(path, err));
    CHECK_EQ(u.size(), 1);
    CHECK(u.get(0) != nullptr);
    CHECK(!u.get(0)->ptr.has_value());
    CHECK_BITS(u.get(0)->address, 0x7F1234567890ull);
    std::remove(path.c_str());

    // una linea v2 con type=pointer se rechaza ('pointer' no es value_type)
    FILE* f2 = fopen(path.c_str(), "w");
    CHECK(f2 != nullptr);
    fprintf(f2, "pointer type=pointer module=/bin/x root=0x10 enabled=1 \"mala\"\n");
    fclose(f2);
    AddressTable v;
    CHECK(v.load(path, err));
    CHECK(v.empty()); // linea invalida saltada
    std::remove(path.c_str());

    // lineas malformadas v2 tambien se saltan
    FILE* f3 = fopen(path.c_str(), "w");
    CHECK(f3 != nullptr);
    fprintf(f3, "pointer type=i32 module=/bin/x enabled=1 \"sin root\"\n");
    fprintf(f3, "pointer type=i32 module=/bin/x root=zzz enabled=1 \"root malo\"\n");
    fprintf(f3, "0x1000 i32 1 \"v1 valida\"\n");
    fclose(f3);
    AddressTable w;
    CHECK(w.load(path, err));
    CHECK_EQ(w.size(), 1); // solo la v1
    CHECK_BITS(w.get(0)->address, 0x1000);
    std::remove(path.c_str());
}

int main() {
    test_make_base();
    test_resolve_root();
    test_follow_chain();
    test_value_type_independent();
    test_make_chain_ref();
    test_save_load_v2();
    test_v1_compat();

    std::printf("\n== test_pointer_v2: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
