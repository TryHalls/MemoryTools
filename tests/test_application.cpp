// test_application.cpp - Tests de la capa Application (FASE 0 de la GUI).
//
// Verifica que Application orquesta las operaciones headless (sin imprimir)
// y devuelve resultados estructurados. No depende de un proceso real: se
// prueban los caminos de error sin proceso, la construccion de PointerChainRef
// desde una cadena sintetica (add_pointer_chain), la lectura/escritura sin
// proceso, y el acceso a la tabla de la sesion.
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_application.cpp
//       src/application.cpp src/session.cpp src/address_table.cpp
//       src/pointer_resolver.cpp src/memory.cpp
//       -o build/test_application
// Ejecutar: ./build/test_application   (0 = exito, !=0 = fallo)
#include "application.h"
#include "session.h"

#include <atomic>
#include <cstdio>
#include <csignal>
#include <cstdint>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
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

// --- Cancelacion + progreso a traves de Application (proceso real) ---------

// Lanza un proceso hijo que concede ptrace (PR_SET_PTRACER_ANY) y guarda un
// valor conocido en una variable estatica; comunica su direccion por un pipe
// (fork duplica el espacio de direcciones: la direccion es valida en el hijo).
// Devuelve el PID (o -1 si falla); 'addr_out' recibe la direccion del valor.
static pid_t spawn_target(uint64_t& addr_out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
        static uint64_t g_val = 0x1122334455667788ull;
        const uint64_t p = (uintptr_t)&g_val; // la DIRECCION, no el valor
        (void)write(pipefd[1], &p, sizeof p);
        close(pipefd[1]);
        for (;;) pause(); // mantener vivo hasta que el padre lo mate
        _exit(0);
    }
    close(pipefd[1]);
    const ssize_t n = read(pipefd[0], &addr_out, sizeof addr_out);
    close(pipefd[0]);
    if (n != (ssize_t)sizeof addr_out) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return -1;
    }
    return pid;
}

static void test_scan_cancel_and_progress() {
    uint64_t addr = 0;
    const pid_t pid = spawn_target(addr);
    CHECK(pid > 0);
    if (pid <= 0) return;

    Session s;
    Application app(s);
    AttachOutcome a = app.attach((int)pid);
    CHECK(a.ok);
    if (!a.ok) { // sin permiso ptrace: no dejar huerfanos y terminar
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return;
    }

    const Value known{0x1122334455667788ull};

    // I) cancel=nullptr: comportamiento actual (first normal encuentra el valor).
    ScanOutcome f = app.first_scan(DataType::U64, known);
    CHECK(f.ok);
    CHECK(!f.cancelled);
    CHECK(f.count >= 1);
    const size_t prev = s.scanner().count();

    // B) first con cancel preactivo: cancelado, sin resultado parcial y con el
    //    resultado anterior del Scanner intacto (H).
    std::atomic<bool> cancel{true};
    ScanOutcome fc = app.first_scan(DataType::U64, known, &cancel);
    CHECK(fc.cancelled);
    CHECK(!fc.ok);
    CHECK_EQ(fc.count, 0);
    CHECK_EQ(s.scanner().count(), prev);

    // next normal sin cancel: unchanged conserva todos los candidatos (el
    // valor no ha cambiado) -> count == prev.
    cancel.store(false);
    ScanOutcome n = app.next_scan(DataType::U64, Filter::UNCHANGED, std::nullopt);
    CHECK(n.ok);
    CHECK(!n.cancelled);
    CHECK_EQ(n.count, prev);

    // C) next cancelado: conserva exactamente los candidatos anteriores.
    cancel.store(true);
    ScanOutcome nc = app.next_scan(DataType::U64, Filter::UNCHANGED, std::nullopt,
                                   &cancel);
    CHECK(nc.cancelled);
    CHECK(!nc.ok);
    CHECK_EQ(s.scanner().count(), prev);

    // D) first dinamico cancelado (el resultado numerico anterior sigue intacto).
    std::string perr;
    DynamicScanSpec dspec =
        make_dynamic_spec(DataType::STRING, pattern_from_text("hola", perr));
    cancel.store(true);
    ScanOutcome fd = app.first_scan_dynamic(dspec, &cancel);
    CHECK(fd.cancelled);
    CHECK(!fd.ok);
    CHECK_EQ(s.scanner().count(), prev);

    // E) pattern cancelado: hits vacios y cancelled (nunca parcial).
    BytePattern pat;
    CHECK(parse_pattern("48 8B 05", pat));
    cancel.store(true);
    PatternOutcome po = app.pattern_scan(pat, &cancel);
    CHECK(po.cancelled);
    CHECK(!po.ok);
    CHECK_EQ(po.hits.size(), 0);

    // F) pointer scan cancelado: no publica nada; el resultado previo de la
    //    sesion queda intacto.
    PointerScanResult synth;
    synth.target = 0x4242;
    PointerChain c;
    c.nodes = {0x10, 0x4242};
    c.offsets = {0};
    c.depth = 1;
    synth.chains.push_back(c);
    s.set_pointer_result(std::move(synth));

    PointerScanInput pin;
    pin.opts.target = addr; // region legible del proceso vivo
    pin.opts.max_depth = 1;
    pin.value_type = DataType::U64;
    cancel.store(true);
    PointerScanOutcome psc = app.pointer_scan(pin, &cancel);
    CHECK(psc.cancelled);
    CHECK(!psc.ok);
    CHECK(s.pointer_result().has_value());
    CHECK_BITS(s.pointer_result()->target, 0x4242); // preservado

    // G) el progreso llega a Application: monotónico, nunca supera el total y
    //    termina en 100% al completar normalmente.
    cancel.store(false);
    std::vector<std::pair<uint64_t, uint64_t>> prog;
    ProgressFn progress = [&](uint64_t scanned, uint64_t total) {
        prog.emplace_back(scanned, total);
    };
    ScanOutcome fp = app.first_scan(DataType::U64, known, nullptr, progress);
    CHECK(fp.ok);
    CHECK(!fp.cancelled);
    CHECK(!prog.empty());
    for (size_t i = 1; i < prog.size(); ++i)
        CHECK(prog[i].first >= prog[i - 1].first); // monotónico
    for (const auto& pr : prog) CHECK(pr.first <= pr.second); // <= total
    CHECK(prog.back().first == prog.back().second);           // 100% al terminar

    // Limpieza: detach y matar al hijo (no dejar procesos huerfanos).
    app.detach();
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
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
    test_scan_cancel_and_progress();
    test_parse_addr();
    test_next_type_mismatch();

    std::printf("\n== test_application: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
