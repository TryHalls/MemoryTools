// test_api.cpp - Tests de la API JSON (FASE W-4) sin abrir puertos.
//
// Ejercita Api::handle() directamente con HttpRequest construidos por el
// parser, cubriendo: status, processes, validacion de attach, JSON invalido,
// tipo invalido, busy (mutex de session y registry), paginacion, limites de
// memoria, indices de tabla/pointer, seguridad de paths y codigos de error.
// Un test usa un proceso real controlado para la politica "un solo job".
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -pthread
//       tests/test_api.cpp
//       src/application.cpp src/session.cpp src/scanner.cpp src/pattern.cpp
//       src/address_table.cpp src/pointer.cpp src/pointer_resolver.cpp
//       src/memory.cpp src/process.cpp
//       src/web/json.cpp src/web/jobs.cpp src/web/job_runner.cpp
//       src/web/http.cpp src/web/api.cpp
//       -o build/test_api
// Ejecutar: ./build/test_api   (0 = exito, !=0 = fallo)
#include "application.h"
#include "session.h"
#include "web/api.h"
#include "web/http.h"
#include "web/job_runner.h"
#include "web/jobs.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstdint>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace mt;
using namespace mt::web;

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

// Construye una request completa (parser puro, sin sockets).
static HttpRequest req(const std::string& method, const std::string& path,
                       const std::string& body = "") {
    std::string raw = method + " " + path + " HTTP/1.1\r\n";
    raw += "Host: 127.0.0.1:8080\r\n";
    if (!body.empty())
        raw += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    raw += "\r\n" + body;
    return parse_http_request(raw);
}

// --- status / processes -------------------------------------------------------

static void test_status() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    const ApiResponse r = api.handle(req("GET", "/api/status"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"ok\":true") != std::string::npos);
    CHECK(r.body.find("\"attached\":false") != std::string::npos);
    CHECK(r.body.find("\"pid\":\"0\"") != std::string::npos);
    CHECK(r.body.find("\"runner_busy\":false") != std::string::npos);
    CHECK(r.body.find("\"job\":null") != std::string::npos);
}

static void test_processes() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    const ApiResponse r = api.handle(req("GET", "/api/processes"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"ok\":true") != std::string::npos);
    CHECK(r.body.find("\"processes\":[") != std::string::npos);
    CHECK(r.body.find("\"pid\":\"") != std::string::npos);
    CHECK(r.body.find("\"accessible\":") != std::string::npos);
}

// --- attach: validacion y errores ----------------------------------------------

static void test_attach_validation() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // JSON invalido -> 400 bad_request
    ApiResponse r = api.handle(req("POST", "/api/attach", "no es json"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("\"code\":\"bad_request\"") != std::string::npos);

    // sin pid -> 400
    r = api.handle(req("POST", "/api/attach", "{}"));
    CHECK_EQ(r.status, 400);

    // pid no numerico -> 400
    r = api.handle(req("POST", "/api/attach", "{\"pid\":\"abc\"}"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("pid invalido o fuera de rango") != std::string::npos);

    // pid enorme (IMP-1) -> 400, sin crash
    r = api.handle(req("POST", "/api/attach",
                       "{\"pid\":\"999999999999999999999999\"}"));
    CHECK_EQ(r.status, 400);

    // pid negativo -> 400
    r = api.handle(req("POST", "/api/attach", "{\"pid\":\"-5\"}"));
    CHECK_EQ(r.status, 400);

    // pid inexistente -> 400 failed (error de Application, sin excepcion)
    r = api.handle(req("POST", "/api/attach", "{\"pid\":\"99999999\"}"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("\"code\":\"failed\"") != std::string::npos);
    CHECK(!s.has_pid()); // el attach fallido no cambio el estado

    // detach sin proceso -> ok
    r = api.handle(req("POST", "/api/detach"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"attached\":false") != std::string::npos);
}

// --- busy: el mutex de session esta tomado -------------------------------------

static void test_busy_session() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // El test toma session_mutex (como si un worker escaneara).
    std::lock_guard<std::mutex> lk(runner.session_mutex());

    ApiResponse r = api.handle(req("POST", "/api/attach", "{\"pid\":\"1\"}"));
    CHECK_EQ(r.status, 409);
    CHECK(r.body.find("\"code\":\"busy\"") != std::string::npos);
    CHECK(r.body.find("Another operation is running") != std::string::npos);

    r = api.handle(req("POST", "/api/detach"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("GET", "/api/results"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("GET", "/api/memory?address=0x1000&length=256"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("POST", "/api/write",
                       "{\"address\":\"0x1000\",\"type\":\"int32\",\"value\":\"1\"}"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("GET", "/api/table"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("POST", "/api/table/add",
                       "{\"address\":\"0x1000\",\"type\":\"int32\"}"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("GET", "/api/pointer/results"));
    CHECK_EQ(r.status, 409);

    r = api.handle(req("POST", "/api/pointer/add", "{\"chain_index\":0}"));
    CHECK_EQ(r.status, 409);

    // status/jobs NO necesitan session_mutex: siguen respondiendo.
    r = api.handle(req("GET", "/api/status"));
    CHECK_EQ(r.status, 200);
    r = api.handle(req("GET", "/api/jobs/1"));
    CHECK_EQ(r.status, 404); // no existe, pero responde (no 409)
}

// --- scans: validacion de tipo/valor y no_process -------------------------------

static void test_scan_validation() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // tipo invalido -> 400 scan_invalid
    ApiResponse r = api.handle(
        req("POST", "/api/scan/first", "{\"type\":\"xyz\",\"value\":\"5\"}"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("\"code\":\"scan_invalid\"") != std::string::npos);

    // valor invalido para el tipo -> 400
    r = api.handle(req("POST", "/api/scan/first",
                       "{\"type\":\"int32\",\"value\":\"no\"}"));
    CHECK_EQ(r.status, 400);

    // sin proceso (input valido) -> 409 no_process
    r = api.handle(req("POST", "/api/scan/first",
                       "{\"type\":\"int32\",\"value\":\"5\"}"));
    CHECK_EQ(r.status, 409);
    CHECK(r.body.find("\"code\":\"no_process\"") != std::string::npos);

    // dynamic sin proceso -> 409 no_process
    r = api.handle(req("POST", "/api/scan/first",
                       "{\"type\":\"string\",\"value\":\"hola\"}"));
    CHECK_EQ(r.status, 409);

    // pattern sin proceso -> 409
    r = api.handle(req("POST", "/api/pattern", "{\"pattern\":\"48 8B ??\"}"));
    CHECK_EQ(r.status, 409);

    // pattern invalido -> 400
    r = api.handle(req("POST", "/api/pattern", "{\"pattern\":\"48 8G\"}"));
    CHECK_EQ(r.status, 400);

    // next sin proceso -> 409
    r = api.handle(req("POST", "/api/scan/next", "{\"filter\":\"changed\"}"));
    CHECK_EQ(r.status, 409);

    // next sin filtro -> 400 (falta filter)
    r = api.handle(req("POST", "/api/scan/next", "{}"));
    CHECK_EQ(r.status, 400);
}

// --- resultados: paginacion y limites ---------------------------------------------

static void test_results() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // sin scan previo -> total 0, rows vacio, offset devuelto
    ApiResponse r = api.handle(req("GET", "/api/results?offset=5&limit=100"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"total\":\"0\"") != std::string::npos);
    CHECK(r.body.find("\"offset\":5") != std::string::npos);
    CHECK(r.body.find("\"rows\":[]") != std::string::npos);

    // limites extremos no rompen nada (offset enorme, limit enorme)
    r = api.handle(req("GET", "/api/results?offset=999999999999&limit=99999999"));
    CHECK_EQ(r.status, 200);

    // parametros no numericos -> defaults
    r = api.handle(req("GET", "/api/results?offset=zzz&limit=abc"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"offset\":0") != std::string::npos);

    // pointer results sin scan -> total 0
    r = api.handle(req("GET", "/api/pointer/results?offset=0&limit=50"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"total\":\"0\"") != std::string::npos);

    // pattern results sin job -> total 0
    r = api.handle(req("GET", "/api/pattern/results"));
    CHECK_EQ(r.status, 200);
}

// --- memoria: limites y no_process ---------------------------------------------------

static void test_memory_limits() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // sin proceso -> 409 no_process (aunque el length sea enorme: el clamp no
    // debe producir overflow ni crash)
    ApiResponse r = api.handle(
        req("GET", "/api/memory?address=0x1000&length=999999999999999999"));
    CHECK_EQ(r.status, 409);
    CHECK(r.body.find("\"code\":\"no_process\"") != std::string::npos);

    // address malformada -> 0 (default); sin proceso -> no_process igualmente
    r = api.handle(req("GET", "/api/memory?address=zzz&length=256"));
    CHECK_EQ(r.status, 409);
}

// --- tabla --------------------------------------------------------------------------

static void test_table() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // vacia
    ApiResponse r = api.handle(req("GET", "/api/table"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"entries\":[]") != std::string::npos);

    // add sin proceso (no toca memoria)
    r = api.handle(req("POST", "/api/table/add",
                       "{\"address\":\"0x1000\",\"type\":\"int32\","
                       "\"description\":\"prueba\"}"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"index\":0") != std::string::npos);

    // address invalido -> 400
    r = api.handle(req("POST", "/api/table/add",
                       "{\"address\":\"zzz\",\"type\":\"int32\"}"));
    CHECK_EQ(r.status, 400);

    // tipo invalido -> 400
    r = api.handle(req("POST", "/api/table/add",
                       "{\"address\":\"0x1000\",\"type\":\"xyz\"}"));
    CHECK_EQ(r.status, 400);

    // listado con la entrada
    r = api.handle(req("GET", "/api/table"));
    CHECK(r.body.find("\"count\":1") != std::string::npos);
    CHECK(r.body.find("\"description\":\"prueba\"") != std::string::npos);

    // toggle: apagar y encender; indice invalido -> 404
    r = api.handle(req("POST", "/api/table/toggle", "{\"index\":0}"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"enabled\":false") != std::string::npos);
    r = api.handle(req("POST", "/api/table/toggle", "{\"index\":0}"));
    CHECK(r.body.find("\"enabled\":true") != std::string::npos);
    r = api.handle(req("POST", "/api/table/toggle", "{\"index\":99}"));
    CHECK_EQ(r.status, 404);

    // remove: existe y luego no
    r = api.handle(req("POST", "/api/table/remove", "{\"index\":0}"));
    CHECK_EQ(r.status, 200);
    r = api.handle(req("POST", "/api/table/remove", "{\"index\":0}"));
    CHECK_EQ(r.status, 404);

    // table read / set sin proceso -> 409 no_process
    r = api.handle(req("POST", "/api/table/read", "{\"index\":0}"));
    CHECK_EQ(r.status, 409);
    r = api.handle(req("POST", "/api/table/set", "{\"index\":0,\"value\":\"1\"}"));
    CHECK_EQ(r.status, 409);

    // clear
    r = api.handle(req("POST", "/api/table/clear"));
    CHECK_EQ(r.status, 200);
    r = api.handle(req("GET", "/api/table"));
    CHECK(r.body.find("\"count\":0") != std::string::npos);
}

// --- seguridad de paths (table save/load) ---------------------------------------------

static void test_path_security() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // nombres peligrosos -> 403 forbidden
    const std::string long_name(65, 'a');
    const std::vector<std::string> bads = {
        "../evil", "a/b", "a\\b", "..", ".", "~x", "", "a b",
        ".hidden", long_name};
    for (const std::string& bad : bads) {
        const ApiResponse r = api.handle(
            req("POST", "/api/table/save",
                std::string("{\"name\":\"") + bad + "\"}"));
        CHECK_EQ(r.status, 403);
        CHECK(r.body.find("\"code\":\"forbidden\"") != std::string::npos);
    }

    // nombre valido -> save y load round-trip
    ApiResponse r = api.handle(
        req("POST", "/api/table/save", "{\"name\":\"mitabla\"}"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"path\":\"tables/mitabla\"") != std::string::npos);

    r = api.handle(req("POST", "/api/table/add",
                       "{\"address\":\"0x2000\",\"type\":\"u64\","
                       "\"description\":\"roundtrip\"}"));
    CHECK_EQ(r.status, 200);

    r = api.handle(req("POST", "/api/table/load", "{\"name\":\"mitabla\"}"));
    CHECK_EQ(r.status, 200);

    // cargar una tabla inexistente -> 400
    r = api.handle(req("POST", "/api/table/load", "{\"name\":\"noexiste\"}"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("\"code\":\"failed\"") != std::string::npos);
}

// --- pointer ------------------------------------------------------------------------------

static void test_pointer() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // scan sin proceso -> 409 no_process
    ApiResponse r = api.handle(req("POST", "/api/pointer/scan",
                                   "{\"target\":\"0x1000\"}"));
    CHECK_EQ(r.status, 409);

    // target invalido -> 400
    r = api.handle(req("POST", "/api/pointer/scan", "{\"target\":\"zzz\"}"));
    CHECK_EQ(r.status, 400);

    // depth=8 -> 400 (reglas de la CLI)
    r = api.handle(req("POST", "/api/pointer/scan",
                       "{\"target\":\"0x1000\",\"depth\":8}"));
    CHECK_EQ(r.status, 400);

    // combinacion de offsets peligrosa (65537 offsets) -> 400
    r = api.handle(req("POST", "/api/pointer/scan",
                       "{\"target\":\"0x1000\",\"max_offset\":\"0x10000\","
                       "\"offset_step\":1}"));
    CHECK_EQ(r.status, 400);
    CHECK(r.body.find("demasiado grande") != std::string::npos);

    // pointer add sin scan -> 400
    r = api.handle(req("POST", "/api/pointer/add", "{\"chain_index\":0}"));
    CHECK_EQ(r.status, 400);

    // pointer resolve sin proceso -> 409 no_process
    r = api.handle(req("POST", "/api/pointer/resolve", "{\"index\":0}"));
    CHECK_EQ(r.status, 409);
}

// --- errores genericos ---------------------------------------------------------------------

static void test_errors() {
    Session s;
    Application app(s);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // ruta desconocida -> 404
    ApiResponse r = api.handle(req("GET", "/api/noexiste"));
    CHECK_EQ(r.status, 404);
    CHECK(r.body.find("\"code\":\"not_found\"") != std::string::npos);

    // metodo no permitido en ruta conocida -> 405
    r = api.handle(req("POST", "/api/status"));
    CHECK_EQ(r.status, 405);
    // DELETE se rechaza en el parser HTTP (405) antes de llegar a la API;
    // el servidor devuelve ese mismo status para requests invalidas.
    const HttpRequest del =
        parse_http_request("DELETE /api/results HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n\r\n");
    CHECK(!del.valid);
    CHECK_EQ(del.status, 405);
}

// --- un solo job activo (proceso real con region grande) -----------------------------------

struct TargetInfo {
    uint64_t val_addr = 0;
    uint64_t big_size = 0;
};

static pid_t spawn_target(TargetInfo& info) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        close(pipefd[0]);
        prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
        const size_t big = 256u * 1024u * 1024u;
        void* p = mmap(nullptr, big, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        TargetInfo t;
        t.val_addr = 0x1122334455667788ull;
        t.big_size = (p == MAP_FAILED) ? 0 : big;
        (void)write(pipefd[1], &t, sizeof t);
        close(pipefd[1]);
        for (;;) pause();
        _exit(0);
    }
    close(pipefd[1]);
    const ssize_t n = read(pipefd[0], &info, sizeof info);
    close(pipefd[0]);
    if (n != (ssize_t)sizeof info) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return -1;
    }
    return pid;
}

static void test_scan_busy() {
    TargetInfo info;
    const pid_t pid = spawn_target(info);
    CHECK(pid > 0);
    CHECK(info.big_size > 0);
    if (pid <= 0) return;

    Session s;
    Application app(s);
    CHECK(app.attach((int)pid).ok);
    JobRegistry jobs;
    JobRunner runner(app, jobs);
    Api api(app, jobs, runner);

    // Primer scan -> job creado (el scan de la region grande tarda ~1s).
    ApiResponse r = api.handle(req("POST", "/api/scan/first",
                                   "{\"type\":\"int32\",\"value\":\"12345\"}"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"job_id\":\"") != std::string::npos);

    // Segundo scan -> 409 busy (un solo job activo), deterministico: el
    // registro cuenta el job desde create().
    r = api.handle(req("POST", "/api/scan/first",
                       "{\"type\":\"int32\",\"value\":\"12345\"}"));
    CHECK_EQ(r.status, 409);
    CHECK(r.body.find("\"code\":\"busy\"") != std::string::npos);

    // Cancelar y esperar a que el worker termine.
    const size_t p = r.body.find("job_id") == std::string::npos ? 0 : 0; // (r es la respuesta busy)
    (void)p;
    // El job_id esta en la PRIMERA respuesta; se cancela via registry.
    const uint64_t jid = jobs.active_job_id();
    CHECK(jid != 0);
    jobs.request_cancel(jid);
    for (int i = 0; i < 20000 && jobs.has_active_job(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!jobs.has_active_job());
    runner.shutdown();
    app.detach();
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

int main() {
    test_status();
    test_processes();
    test_attach_validation();
    test_busy_session();
    test_scan_validation();
    test_results();
    test_memory_limits();
    test_table();
    test_path_security();
    test_pointer();
    test_errors();
    test_scan_busy();

    // Limpiar el directorio de tablas creado por test_path_security.
    std::remove("tables/mitabla");
    rmdir("tables");

    std::printf("\n== test_api: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
