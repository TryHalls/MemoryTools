// test_address_table.cpp - Tests unitarios de src/address_table.h/.cpp.
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_address_table.cpp src/address_table.cpp -o build/test_address_table
// Ejecutar:  ./build/test_address_table   (0 = exito, !=0 = fallo)
#include "address_table.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

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

static std::string tmp_file() {
    return "/tmp/mt_at_test_" + std::to_string((long)getpid()) + ".txt";
}

static void test_add_get_remove_clear() {
    AddressTable t;
    CHECK(t.empty());
    CHECK_EQ(t.size(), 0);

    // add devuelve indices consecutivos
    size_t i0 = t.add(0x1000, DataType::I32, "primera");
    size_t i1 = t.add(0x2000, DataType::F32, "");
    CHECK_EQ(i0, 0);
    CHECK_EQ(i1, 1);
    CHECK(!t.empty());
    CHECK_EQ(t.size(), 2);

    // get
    const AddressEntry* e = t.get(0);
    CHECK(e != nullptr);
    CHECK_BITS(e->address, 0x1000);
    CHECK(e->type == DataType::I32);
    CHECK_STR(e->description, "primera");
    CHECK(e->enabled);
    CHECK(!e->stale);
    CHECK(t.get(1) != nullptr);
    CHECK(t.get(2) == nullptr);   // fuera de rango
    CHECK(t.get(999) == nullptr);

    // remove
    CHECK(t.remove(1));
    CHECK_EQ(t.size(), 1);
    CHECK(t.get(1) == nullptr);
    CHECK(!t.remove(5));          // no existe
    CHECK(!t.remove(1));

    // clear
    t.clear();
    CHECK(t.empty());
    CHECK_EQ(t.size(), 0);
    CHECK(t.get(0) == nullptr);
}

static void test_enabled_and_stale() {
    AddressTable t;
    t.add(0x3000, DataType::U16, "nivel");
    AddressEntry* e = t.get(0);
    CHECK(e != nullptr);
    CHECK(e->enabled);
    e->enabled = false;
    CHECK(!t.get(0)->enabled);

    t.mark_all_stale();
    CHECK(t.get(0)->stale);

    // stale no bloquea el almacenamiento; una entrada nueva no es stale
    t.add(0x4000, DataType::I64);
    CHECK(!t.get(1)->stale);
}

static void test_save_load_roundtrip() {
    AddressTable t;
    // descripcion con espacios, comillas y backslash; tipos variados;
    // direcciones de 64 bits; entrada desactivada.
    t.add(0x7F123456789ABCDEull, DataType::I32, "dinero");
    t.add(0x000055a92c9ab29eull, DataType::F32, "velocidad del coche");
    t.add(0xFFFFFFFFFFFFFFFFull, DataType::U64, "comillas \" y backslash \\");
    t.add(0x1234, DataType::I8, "desactivada");
    t.get(3)->enabled = false;
    // stale NO se persiste: simular que una entrada quedo stale
    t.mark_all_stale();
    t.get(0)->stale = false; // solo la entrada 0 queda limpia

    std::string path = tmp_file();
    std::string err;
    CHECK(t.save(path, err));

    AddressTable u; // tabla nueva: la carga debe reemplazar y restaurar
    CHECK(u.load(path, err));
    CHECK_EQ(u.size(), 4);

    const AddressEntry* e0 = u.get(0);
    CHECK(e0 != nullptr);
    CHECK_BITS(e0->address, 0x7F123456789ABCDEull);
    CHECK(e0->type == DataType::I32);
    CHECK_STR(e0->description, "dinero");
    CHECK(e0->enabled);

    const AddressEntry* e1 = u.get(1);
    CHECK_BITS(e1->address, 0x000055a92c9ab29eull);
    CHECK(e1->type == DataType::F32);
    CHECK_STR(e1->description, "velocidad del coche");

    const AddressEntry* e2 = u.get(2);
    CHECK_BITS(e2->address, 0xFFFFFFFFFFFFFFFFull);
    CHECK(e2->type == DataType::U64);
    CHECK_STR(e2->description, "comillas \" y backslash \\");

    const AddressEntry* e3 = u.get(3);
    CHECK_BITS(e3->address, 0x1234);
    CHECK(e3->type == DataType::I8);
    CHECK(!e3->enabled); // estado restaurado

    // stale no se persiste: al cargar todo queda fresco
    CHECK(!u.get(0)->stale);
    CHECK(!u.get(1)->stale);

    std::remove(path.c_str());
}

static void test_load_errors_and_format() {
    // archivo inexistente
    AddressTable t;
    std::string err;
    CHECK(!t.load("/tmp/archivo_que_no_existe_xyz.txt", err));
    CHECK(!err.empty());

    // carga parcial: lineas malformadas se saltan, comentarios se ignoran
    std::string path = tmp_file();
    FILE* f = fopen(path.c_str(), "w");
    CHECK(f != nullptr);
    fprintf(f, "# MemoryTool Address Table v1\n");
    fprintf(f, "\n");
    fprintf(f, "0x1111 i32 1 \"buena\"\n");
    fprintf(f, "linea malformada sin campos\n");
    fprintf(f, "0x2222 tipoinexistente 1 \"mala\"\n");
    fprintf(f, "0x3333 i32 1 \"con comillas \\\" dentro\"\n");
    fclose(f);

    AddressTable u;
    CHECK(u.load(path, err));
    CHECK_EQ(u.size(), 2); // solo las dos validas
    CHECK_BITS(u.get(0)->address, 0x1111);
    CHECK_STR(u.get(0)->description, "buena");
    CHECK_BITS(u.get(1)->address, 0x3333);
    CHECK_STR(u.get(1)->description, "con comillas \" dentro");

    // el tipo se lee con alias ("float", "u8"...)
    FILE* f2 = fopen(path.c_str(), "w");
    fprintf(f2, "0x4444 float 1 \"alias\"\n");
    fprintf(f2, "0x5555 u8 0 \"otro\"\n");
    fclose(f2);
    AddressTable v;
    CHECK(v.load(path, err));
    CHECK_EQ(v.size(), 2);
    CHECK(v.get(0)->type == DataType::F32);
    CHECK(v.get(1)->type == DataType::U8);
    CHECK(!v.get(1)->enabled);

    std::remove(path.c_str());
}

int main() {
    test_add_get_remove_clear();
    test_enabled_and_stale();
    test_save_load_roundtrip();
    test_load_errors_and_format();

    std::printf("\n== test_address_table: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
