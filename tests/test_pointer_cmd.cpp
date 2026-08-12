// test_pointer_cmd.cpp - Tests de integracion de la FASE 2 del Pointer
// Scanner: Session conserva el ultimo PointerScanResult, el parsing de
// 'pointer scan' (validacion de depth y opciones), la descripcion textual de
// una cadena, el comando 'pointer add' (crea una entrada tipo 'pointer' en la
// AddressTable) y la persistencia de ese tipo en el formato de tabla v1.
//
// Los handlers se ejercitan a traves de execute() capturando stdout con un
// dup2 temporal (no dependen de un proceso real: 'pointer add'/'results' y
// los paths de error no tocan memoria).
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer_cmd.cpp \
//       src/command.cpp src/session.cpp src/scanner.cpp src/memory.cpp \
//       src/pattern.cpp src/process.cpp src/address_table.cpp src/pointer.cpp \
//       -o build/test_pointer_cmd
// Ejecutar: ./build/test_pointer_cmd   (0 = exito, !=0 = fallo)
#include "command.h"
#include "session.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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

// Ejecuta una linea de comando capturando lo que imprime (stdout) en un
// archivo temporal. Los handlers usan printf, asi que se redirige con dup2.
static std::string capture_execute(const std::string& line, Session& s) {
    fflush(stdout);
    char tmpl[] = "/tmp/mt_ptr_cmd_XXXXXX";
    int fd = mkstemp(tmpl);
    const int saved = dup(STDOUT_FILENO);
    dup2(fd, STDOUT_FILENO);
    execute(line, s);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);

    std::string out;
    char buf[4096];
    lseek(fd, 0, SEEK_SET);
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
    close(fd);
    std::remove(tmpl);
    return out;
}

// --- Parsing y validacion de 'pointer scan' --------------------------------

static void test_parse_pointer_scan_args() {
    // direccion en hex
    PointerScanArgs a = parse_pointer_scan_args({"0x1234"});
    CHECK(a.error.empty());
    CHECK_BITS(a.target, 0x1234);
    CHECK_EQ(a.depth, 3);        // valor por defecto
    CHECK(!a.include_code);

    // direccion en decimal
    a = parse_pointer_scan_args({"123456"});
    CHECK(a.error.empty());
    CHECK_BITS(a.target, 123456);

    // depth valido (1..7)
    a = parse_pointer_scan_args({"0x1234", "depth=1"});
    CHECK(a.error.empty());
    CHECK_EQ(a.depth, 1);
    a = parse_pointer_scan_args({"0x1234", "depth=7"});
    CHECK(a.error.empty());
    CHECK_EQ(a.depth, 7);

    // depth invalido (0, 8, no numerico, vacio)
    a = parse_pointer_scan_args({"0x1234", "depth=0"});
    CHECK(!a.error.empty());
    CHECK(a.error.find("depth") != std::string::npos);
    a = parse_pointer_scan_args({"0x1234", "depth=8"});
    CHECK(!a.error.empty());
    a = parse_pointer_scan_args({"0x1234", "depth=abc"});
    CHECK(!a.error.empty());
    a = parse_pointer_scan_args({"0x1234", "depth="});
    CHECK(!a.error.empty());

    // code (y combinado con depth)
    a = parse_pointer_scan_args({"0x1234", "code"});
    CHECK(a.error.empty());
    CHECK(a.include_code);
    a = parse_pointer_scan_args({"0x1234", "code", "depth=2"});
    CHECK(a.error.empty());
    CHECK(a.include_code);
    CHECK_EQ(a.depth, 2);

    // opcion desconocida
    a = parse_pointer_scan_args({"0x1234", "foo"});
    CHECK(!a.error.empty());
    CHECK(a.error.find("foo") != std::string::npos);

    // sin argumentos / direccion invalida
    a = parse_pointer_scan_args({});
    CHECK(!a.error.empty());
    a = parse_pointer_scan_args({"zzz"});
    CHECK(!a.error.empty());

    // FASE 4: ventana de offsets, module-only y tipo
    a = parse_pointer_scan_args({"0x1234", "max_offset=0x40"});
    CHECK(a.error.empty());
    CHECK_BITS(a.max_offset, 0x40);
    CHECK_BITS(a.offset_step, 8); // por defecto
    a = parse_pointer_scan_args({"0x1234", "max_offset=64"});
    CHECK(a.error.empty());
    CHECK_BITS(a.max_offset, 64); // decimal aceptado
    a = parse_pointer_scan_args({"0x1234", "offset_step=4"});
    CHECK(a.error.empty());
    CHECK_BITS(a.offset_step, 4);

    // offset_step = 0 -> error
    a = parse_pointer_scan_args({"0x1234", "offset_step=0"});
    CHECK(!a.error.empty());
    CHECK(a.error.find("offset_step") != std::string::npos);
    // offset_step no numerico -> error
    a = parse_pointer_scan_args({"0x1234", "offset_step=abc"});
    CHECK(!a.error.empty());

    // max_offset demasiado grande (cap 0x10000) -> error
    a = parse_pointer_scan_args({"0x1234", "max_offset=0x100000"});
    CHECK(!a.error.empty());
    CHECK(a.error.find("max_offset") != std::string::npos);

    // max_offset menor que offset_step: permitido (ventana = {0})
    a = parse_pointer_scan_args({"0x1234", "max_offset=4", "offset_step=8"});
    CHECK(a.error.empty());
    CHECK_BITS(a.max_offset, 4);
    CHECK_BITS(a.offset_step, 8);

    // module-only
    a = parse_pointer_scan_args({"0x1234", "module-only"});
    CHECK(a.error.empty());
    CHECK(a.module_only);
    a = parse_pointer_scan_args({"0x1234", "module-only", "code", "depth=5"});
    CHECK(a.error.empty());
    CHECK(a.module_only);
    CHECK(a.include_code);
    CHECK_EQ(a.depth, 5);

    // type= valido (e invalido: 'pointer' no es un tipo de valor final)
    a = parse_pointer_scan_args({"0x1234", "type=int32"});
    CHECK(a.error.empty());
    CHECK(a.value_type.has_value());
    CHECK(a.value_type.value() == DataType::I32);
    a = parse_pointer_scan_args({"0x1234", "type=float"});
    CHECK(a.error.empty());
    CHECK(a.value_type.value() == DataType::F32);
    a = parse_pointer_scan_args({"0x1234", "type=pointer"});
    CHECK(!a.error.empty());

    // combinacion completa
    a = parse_pointer_scan_args({"0x1234", "depth=4", "max_offset=0x80",
                                 "offset_step=4", "module-only", "code",
                                 "type=u64"});
    CHECK(a.error.empty());
    CHECK_EQ(a.depth, 4);
    CHECK_BITS(a.max_offset, 0x80);
    CHECK_BITS(a.offset_step, 4);
    CHECK(a.module_only);
    CHECK(a.include_code);
    CHECK(a.value_type.value() == DataType::U64);
}

// --- Descripcion textual de una cadena -------------------------------------

static void test_pointer_chain_description() {
    CHECK_STR(pointer_chain_description({0x1, 0x2, 0x3}),
              "0x0000000000000001 -> 0x0000000000000002 -> 0x0000000000000003");
    CHECK_STR(pointer_chain_description({0xABC}),
              "0x0000000000000abc");
    CHECK_STR(pointer_chain_description({}), "");

    // version con offsets: intercala +0x... (solo no nulos)
    CHECK_STR(pointer_chain_description({0x1, 0x2, 0x3}, {0x20, 0x18}),
              "0x0000000000000001 -> +0x20 -> 0x0000000000000002 -> +0x18 -> "
              "0x0000000000000003");
    // offsets a 0 se ocultan (comportamiento V1)
    CHECK_STR(pointer_chain_description({0x1, 0x2, 0x3}, {0, 0}),
              "0x0000000000000001 -> 0x0000000000000002 -> 0x0000000000000003");
    // cantidad de offsets que no cuadra -> version basica
    CHECK_STR(pointer_chain_description({0x1, 0x2, 0x3}, {0x20}),
              "0x0000000000000001 -> 0x0000000000000002 -> 0x0000000000000003");
    // depth 0: sin offsets
    CHECK_STR(pointer_chain_description({0xABC}, {}), "0x0000000000000abc");
}

// --- Tipo 'pointer' (DataType::PTR) en el sistema de tipos ------------------

static void test_ptr_type() {
    DataType t;
    CHECK(parse_type("pointer", t));
    CHECK(t == DataType::PTR);
    CHECK(parse_type("ptr", t));
    CHECK(t == DataType::PTR);
    CHECK_STR(type_name(DataType::PTR), "pointer");
    CHECK_EQ(type_size(DataType::PTR), 8);

    Value v;
    CHECK(parse_value("0x7f1234567890", DataType::PTR, v));
    CHECK_BITS(v.bits, 0x7f1234567890ull);
    CHECK(parse_value("123456", DataType::PTR, v));
    CHECK_BITS(v.bits, 123456);
    CHECK(!parse_value("-5", DataType::PTR, v)); // negativo rechazado
    CHECK_STR(value_to_string(v, DataType::PTR), "0x000000000001e240");
}

// --- Session conserva el ultimo PointerScanResult --------------------------

static void test_session_pointer_result() {
    Session s;
    CHECK(!s.pointer_result().has_value());

    PointerScanResult r;
    r.target = 0x4242;
    PointerChain c;
    c.nodes = {0x10, 0x20, 0x4242};
    c.depth = 2;
    r.chains.push_back(c);
    s.set_pointer_result(r);

    CHECK(s.pointer_result().has_value());
    CHECK_BITS(s.pointer_result()->target, 0x4242);
    CHECK_EQ(s.pointer_result()->chains.size(), 1);
    CHECK_EQ(s.pointer_result()->chains[0].depth, 2);
    CHECK_BITS(s.pointer_result()->chains[0].nodes[0], 0x10);

    // detach descarta el resultado (pertenece al proceso anterior)
    s.detach();
    CHECK(!s.pointer_result().has_value());
}

// --- Handlers a traves de execute() -----------------------------------------

static void test_pointer_commands() {
    Session s;

    // sin scan previo: 'results' y 'add' avisan
    std::string out = capture_execute("pointer results", s);
    CHECK(out.find("No hay un pointer scan previo") != std::string::npos);
    out = capture_execute("pointer add 0", s);
    CHECK(out.find("No hay un pointer scan previo") != std::string::npos);

    // sin proceso objetivo: 'pointer scan' pide proceso
    out = capture_execute("pointer scan 0x1", s);
    CHECK(out.find("Primero selecciona un proceso") != std::string::npos);

    // con un resultado falso (no toca memoria): results y add
    PointerScanResult r;
    r.target = 0x4242;
    PointerChain c1;
    c1.nodes = {0x10, 0x20, 0x4242};
    c1.depth = 2;
    PointerChain c2;
    c2.nodes = {0x30, 0x4242};
    c2.depth = 1;
    r.chains.push_back(c1);
    r.chains.push_back(c2);
    s.set_pointer_result(std::move(r));

    out = capture_execute("pointer results 1", s);
    CHECK(out.find("[0] depth 2:") != std::string::npos);
    CHECK(out.find("0x0000000000000010 -> 0x0000000000000020 -> 0x0000000000004242") !=
          std::string::npos);
    CHECK(out.find("[1]") == std::string::npos); // solo la primera

    out = capture_execute("pointer results", s);
    CHECK(out.find("[1] depth 1:") != std::string::npos);
    CHECK(out.find("0x0000000000000030 -> 0x0000000000004242") != std::string::npos);

    // indice fuera de rango
    out = capture_execute("pointer add 99", s);
    CHECK(out.find("fuera de rango") != std::string::npos);

    // pointer add 0 (V2): crea una cadena dinamica. Sin proceso -> raiz
    // ABSOLUTE (no persistente); value_type por defecto = I32.
    out = capture_execute("pointer add 0", s);
    CHECK(out.find("anadida desde la cadena 0") != std::string::npos);
    CHECK(out.find("pointer[abs]") != std::string::npos);
    CHECK(out.find("no persistente") != std::string::npos);
    CHECK_EQ(s.table().size(), 1);
    const AddressEntry* e = s.table().get(0);
    CHECK(e != nullptr);
    CHECK(e->ptr.has_value());
    CHECK_BITS(e->ptr->root.address, 0x10); // nodes[0] = base de la cadena
    CHECK(e->ptr->root.kind == PointerBaseKind::ABSOLUTE);
    CHECK(e->type == DataType::I32); // value_type, NO 'pointer'
    CHECK(e->ptr->value_type == DataType::I32);
    CHECK_STR(e->description,
              "0x0000000000000010 -> 0x0000000000000020 -> 0x0000000000004242");
}

// --- FASE 4: pointer add con offsets, resolve y table dinamica ---------------

static void test_pointer_add_v2_offsets() {
    Session s;
    PointerScanResult r;
    r.target = 0x4242;
    r.value_type = DataType::I32;
    PointerChain c;
    c.nodes = {0x10, 0x20, 0x30, 0x4242}; // depth 3
    c.offsets = {0x20, 0x18, 0x8};        // raiz -> +0x20 -> +0x18 -> +0x8
    c.depth = 3;
    r.chains.push_back(c);
    s.set_pointer_result(std::move(r));

    std::string out = capture_execute("pointer results 1", s);
    CHECK(out.find("[0] depth 3:") != std::string::npos);
    CHECK(out.find("[root ABSOLUTE 0x0000000000000010]") != std::string::npos);
    CHECK(out.find("+0x20") != std::string::npos);
    CHECK(out.find("+0x18") != std::string::npos);
    // la descripcion intercala offsets no nulos: A -> +0x20 -> B -> +0x18 -> ...
    CHECK(out.find("0x0000000000000010 -> +0x20 -> 0x0000000000000020 -> +0x18 -> "
                   "0x0000000000000030 -> +0x8 -> 0x0000000000004242") !=
          std::string::npos);

    out = capture_execute("pointer add 0", s);
    CHECK(out.find("pointer[abs]") != std::string::npos);
    CHECK(out.find("tipo: int32") != std::string::npos);
    const AddressEntry* e = s.table().get(0);
    CHECK(e != nullptr);
    CHECK(e->ptr.has_value());
    CHECK_EQ(e->ptr->offsets.size(), 3);
    CHECK_BITS(e->ptr->offsets[0], 0x20);
    CHECK_BITS(e->ptr->offsets[1], 0x18);
    CHECK_BITS(e->ptr->offsets[2], 0x8);
    CHECK(e->ptr->value_type == DataType::I32);
}

static void test_pointer_resolve_errors() {
    Session s;
    PointerScanResult r;
    r.target = 0x4242;
    PointerChain c;
    c.nodes = {0x10, 0x4242};
    c.offsets = {0};
    c.depth = 1;
    r.chains.push_back(c);
    s.set_pointer_result(std::move(r));

    // anadir la cadena (entrada 0) y una absoluta (entrada 1)
    capture_execute("pointer add 0", s);
    s.table().add(0x1000, DataType::I32, "absoluta");

    // sin proceso objetivo: la entrada 0 existe pero no hay proceso
    std::string out = capture_execute("pointer resolve 0", s);
    CHECK(out.find("Primero selecciona un proceso") != std::string::npos);

    // entrada que no es cadena dinamica
    out = capture_execute("pointer resolve 1", s);
    CHECK(out.find("no es una cadena dinamica") != std::string::npos);

    // indice inexistente
    out = capture_execute("pointer resolve 5", s);
    CHECK(out.find("No existe la entrada 5") != std::string::npos);
}

static void test_table_dynamic_errors() {
    Session s;
    // table read / set sin proceso -> piden proceso
    std::string out = capture_execute("table read 0", s);
    CHECK(out.find("Primero selecciona un proceso") != std::string::npos);
    out = capture_execute("table set 0 10", s);
    CHECK(out.find("Primero selecciona un proceso") != std::string::npos);

    // entrada pointer no persistente en una tabla con proceso falso no es
    // posible sin proceso real; aqui solo se verifica el marcado ABSOLUTE.
    PointerScanResult r;
    r.target = 0x4242;
    PointerChain c;
    c.nodes = {0x10, 0x4242};
    c.offsets = {0};
    c.depth = 1;
    r.chains.push_back(c);
    s.set_pointer_result(std::move(r));
    capture_execute("pointer add 0", s);
    const AddressEntry* e = s.table().get(0);
    CHECK(e != nullptr);
    CHECK(e->ptr.has_value());
    CHECK(e->ptr->root.kind == PointerBaseKind::ABSOLUTE); // no persistente
}

// --- Persistencia del tipo 'pointer' en el formato de tabla v1 --------------

static void test_ptr_save_load() {
    AddressTable t;
    t.add(0x7F1234567890ull, DataType::PTR,
          "0x7F1234567890 -> 0x0000000000004242");
    const std::string path =
        "/tmp/mt_ptr_at_" + std::to_string((long)getpid()) + ".txt";
    std::string err;
    CHECK(t.save(path, err));

    AddressTable u;
    CHECK(u.load(path, err));
    CHECK_EQ(u.size(), 1);
    const AddressEntry* e = u.get(0);
    CHECK(e != nullptr);
    CHECK_BITS(e->address, 0x7F1234567890ull);
    CHECK(e->type == DataType::PTR); // el tipo se restaura por nombre "pointer"
    CHECK_STR(e->description, "0x7F1234567890 -> 0x0000000000004242");
    std::remove(path.c_str());
}

int main() {
    test_parse_pointer_scan_args();
    test_pointer_chain_description();
    test_ptr_type();
    test_session_pointer_result();
    test_pointer_commands();
    test_pointer_add_v2_offsets();
    test_pointer_resolve_errors();
    test_table_dynamic_errors();
    test_ptr_save_load();

    std::printf("\n== test_pointer_cmd: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
