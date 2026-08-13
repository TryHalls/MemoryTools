// test_application.cpp - Tests de la capa Application (FASE 0 de la GUI).
//
// Verifica que Application orquesta las operaciones headless (sin imprimir)
// y devuelve resultados estructurados. No depende de un proceso real: se
// prueban los caminos de error sin proceso, la construccion de PointerChainRef
// desde una cadena sintetica (add_pointer_chain), la lectura/escritura sin
// proceso, y el acceso a la tabla de la sesion.
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_application.cpp \
//       src/application.cpp src/session.cpp src/address_table.cpp \
//       src/pointer_resolver.cpp src/memory.cpp \
//       -o build/test_application
// Ejecutar: ./build/test_application   (0 = exito, !=0 = fallo)
#include "application.h"
#include "session.h"

#include <cstdio>
#include <string>

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

// --- Operaciones sin proceso objetivo --------------------------------------

static void test_no_process_errors() {
    Session s;
    Application app(s);

    // attach con un PID inaccesible: error estructurado, estado intacto.
    AttachOutcome a = app.attach(999999);
    CHECK(!a.ok);
    CHECK(!a.error.empty());
    CHECK(!s.has_pid());

    // detach sin proceso: no rompe nada.
    OperationResult d = app.detach();
    CHECK(d.ok);
    CHECK(d.error.empty());

    // escaneos sin proceso: error, no excepcion.
    ScanOutcome f = app.first_scan(DataType::I32, Value{123});
    CHECK(!f.ok);
    CHECK(!f.error.empty());
    ScanOutcome n = app.next_scan(DataType::I32, Filter::EXACT, Value{5});
    CHECK(!n.ok);
    ScanOutcome nd = app.next_scan_dynamic(Filter::CHANGED, std::nullopt);
    CHECK(!nd.ok);

    // patron sin proceso.
    PatternOutcome p = app.pattern_scan(BytePattern{});
    CHECK(!p.ok);

    // memoria sin proceso.
    ReadBytesOutcome rb = app.read_bytes(0x1000, 16);
    CHECK(!rb.ok);
    InfoOutcome inf = app.region_info(0x1000);
    CHECK(!inf.ok);
    WriteOutcome w = app.write(0x1000, DataType::I32, Value{1});
    CHECK(!w.ok);

    // tabla sin proceso: las operaciones de memoria fallan claro.
    CHECK_EQ(app.table_size(), 0);
    EntryReadOutcome re = app.read_entry(0);
    CHECK(!re.attempted);
    CHECK(!re.error.empty());
    WriteOutcome we = app.write_entry(0, Value{1});
    CHECK(!we.ok);
}

// --- PointerChainRef desde una cadena sintetica -----------------------------

static void test_add_pointer_chain() {
    Session s;
    Application app(s);

    PointerScanResult r;
    r.target = 0x4242;
    r.value_type = DataType::I32;
    PointerChain c;
    c.nodes = {0x10, 0x20, 0x4242};
    c.offsets = {0x20, 0x18};
    c.depth = 2;
    r.chains.push_back(c);
    s.set_pointer_result(std::move(r));

    // sin proceso -> raiz ABSOLUTE (no persistente) con los offsets de la cadena.
    AddChainOutcome o = app.add_pointer_chain(0, "cadena sintetica");
    CHECK(o.ok);
    CHECK_EQ(o.table_index, 0);
    CHECK(o.ref.root.kind == PointerBaseKind::ABSOLUTE);
    CHECK_BITS(o.ref.root.address, 0x10);
    CHECK_EQ(o.ref.offsets.size(), 2);
    CHECK_BITS(o.ref.offsets[0], 0x20);
    CHECK_BITS(o.ref.offsets[1], 0x18);
    CHECK(o.ref.value_type == DataType::I32);

    // la entrada queda en la tabla con tipo = value_type (no 'pointer').
    CHECK_EQ(app.table_size(), 1);
    const AddressEntry* e = app.entry(0);
    CHECK(e != nullptr);
    CHECK(e->ptr.has_value());
    CHECK(e->type == DataType::I32);
}

// --- Resolucion y lectura de entradas sin proceso ---------------------------

static void test_resolve_entry_errors() {
    Session s;
    Application app(s);

    // indice inexistente.
    ResolveEntryOutcome o = app.resolve_entry(0);
    CHECK(!o.ok);
    CHECK(o.error.find("No existe") != std::string::npos);

    // entrada absoluta: no es cadena dinamica.
    s.table().add(0x1000, DataType::I32, "absoluta");
    o = app.resolve_entry(0);
    CHECK(!o.ok);
    CHECK(o.error.find("no es una cadena dinamica") != std::string::npos);

    // entrada dinamica sin proceso: error claro (sin crashear).
    PointerScanResult r;
    r.target = 0x4242;
    PointerChain c;
    c.nodes = {0x10, 0x4242};
    c.offsets = {0};
    c.depth = 1;
    r.chains.push_back(c);
    s.set_pointer_result(std::move(r));
    CHECK(app.add_pointer_chain(0, "").ok);

    ResolveEntryOutcome o2 = app.resolve_entry(1);
    CHECK(!o2.ok);
    CHECK(!o2.error.empty());
}

// --- Helpers puros reutilizables --------------------------------------------

static void test_parse_addr() {
    uint64_t v = 0;
    CHECK(parse_addr("0x1234", v));
    CHECK_BITS(v, 0x1234);
    CHECK(parse_addr("1234", v));
    CHECK_BITS(v, 1234);
    CHECK(parse_addr("0xffffffffffffffff", v));
    CHECK_BITS(v, 0xffffffffffffffffull);
    CHECK(!parse_addr("", v));
    CHECK(!parse_addr("zzz", v));
    CHECK(!parse_addr("0x", v));
    // overflow: strtoull -> ERANGE -> rechazado
    CHECK(!parse_addr("0xfffffffffffffffff", v));
}

static void test_next_type_mismatch() {
    CHECK(next_type_mismatch_message(DataType::I32, DataType::I32).empty());
    CHECK(!next_type_mismatch_message(DataType::I32, DataType::U64).empty());
}

int main() {
    test_no_process_errors();
    test_add_pointer_chain();
    test_resolve_entry_errors();
    test_parse_addr();
    test_next_type_mismatch();

    std::printf("\n== test_application: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
