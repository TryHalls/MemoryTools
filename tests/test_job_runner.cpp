// test_job_runner.cpp - Tests de la integracion Job <-> Application
// (FASE W-2 PASO 6): un worker (std::thread) ejecuta una operacion pesada de
// Application protegiendo Session con session_mtx, y el Job termina en
// COMPLETED / CANCELLED / FAILED segun el outcome.
//
// Usa un proceso real controlado (fork + PR_SET_PTRACER, valor conocido +
// region grande de ceros para escaneos largos). La cancelacion mid-run es
// determinista: la tarea senaliza cuando empieza (task_started) y el test
// cancela justo despues, sin depender de sleeps fragiles.
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -pthread
//       tests/test_job_runner.cpp
//       src/application.cpp src/session.cpp src/scanner.cpp src/pattern.cpp
//       src/address_table.cpp src/pointer.cpp src/pointer_resolver.cpp
//       src/memory.cpp src/web/jobs.cpp src/web/job_runner.cpp
//       -o build/test_job_runner
// Ejecutar: ./build/test_job_runner   (0 = exito, !=0 = fallo)
#include "application.h"
#include "session.h"
#include "web/job_runner.h"
#include "web/jobs.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

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

// --- Proceso de prueba ------------------------------------------------------
// Hijo: concede ptrace, guarda un valor conocido en una estatica y mapea una
// region grande de ceros (escaneos largos). Comunica {direccion, tamano} por
// un pipe (fork duplica el espacio de direcciones: la direccion es valida en
// el hijo).
struct TargetInfo {
    uint64_t val_addr = 0;
    uint64_t big_size = 0;
};

static pid_t spawn_target(TargetInfo& info) {
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
        const size_t big = 256u * 1024u * 1024u;
        void* p = mmap(nullptr, big, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        TargetInfo t;
        t.val_addr = (uintptr_t)&g_val;
        t.big_size = (p == MAP_FAILED) ? 0 : big;
        (void)write(pipefd[1], &t, sizeof t);
        close(pipefd[1]);
        for (;;) pause(); // mantener vivo hasta que el padre lo mate
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

// Mapea el outcome de un ScanOutcome a JobRunResult (patron de tarea comun).
static JobRunResult scan_result(const ScanOutcome& o) {
    JobRunResult r;
    r.ok = o.ok;
    r.cancelled = o.cancelled;
    r.count = o.count;
    r.error = o.error;
    return r;
}

// Espera (con presupuesto) a que el Job alcance un estado terminal. El test
// espera ANTES de shutdown(): shutdown() pide cancelacion si el worker aun
// corre, asi que solo se usa para abortar o para unir un worker ya terminado.
static void wait_terminal(const std::shared_ptr<Job>& j, int ms_budget = 30000) {
    for (int i = 0;
         i < ms_budget && j->state() != JobState::COMPLETED &&
         j->state() != JobState::FAILED && j->state() != JobState::CANCELLED;
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// A) Job normal -> COMPLETED (con count del scan).
static void test_normal_completes(pid_t pid) {
    Session s;
    Application app(s);
    CHECK(app.attach((int)pid).ok);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id = reg.create("first");
    CHECK(id != 0);
    JobTask task = [](Application& a, Job&) -> JobRunResult {
        return scan_result(
            a.first_scan(DataType::U64, Value{0x1122334455667788ull}));
    };
    CHECK(runner.run(id, task));
    const auto j = reg.get(id);
    wait_terminal(j);
    runner.shutdown();
    CHECK(j->state() == JobState::COMPLETED);
    CHECK(j->count() >= 1);
    CHECK(j->error().empty());
    CHECK(!j->cancel_requested());
    app.detach();
}

// B) Cancel antes de empezar -> CANCELLED y el worker nunca llega a lanzarse.
static void test_cancel_before_start() {
    Session s;
    Application app(s);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id = reg.create("first");
    CHECK(id != 0);
    CHECK(reg.request_cancel(id)); // QUEUED -> CANCELLED directo
    JobTask task = [](Application&, Job&) -> JobRunResult {
        JobRunResult r;
        r.ok = true; // no deberia ejecutarse jamas
        return r;
    };
    CHECK(!runner.run(id, task)); // no esta QUEUED -> rechazado
    CHECK(!runner.busy());
    const auto j = reg.get(id);
    CHECK(j->state() == JobState::CANCELLED);
    CHECK(j->cancel_requested());
}

// C) Cancel durante el scan -> CANCELLED, sin resultado parcial y con la
//    session_mtx tomada por el worker (I) mientras request_cancel funciona
//    sin ella (J).
static void test_cancel_during_scan(pid_t pid, uint64_t big_size) {
    CHECK(big_size > 0);
    Session s;
    Application app(s);
    CHECK(app.attach((int)pid).ok);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id = reg.create("first");
    CHECK(id != 0);
    std::atomic<bool> task_started{false};
    JobTask task = [&](Application& a, Job& j) -> JobRunResult {
        task_started.store(true);
        return scan_result(a.first_scan(DataType::U64,
                                        Value{0x1122334455667788ull},
                                        j.cancel_flag(),
                                        j.make_progress_callback()));
    };
    CHECK(runner.run(id, task));
    const auto j = reg.get(id);
    // Esperar a que el worker este DENTRO de la tarea (bajo session_mtx).
    for (int i = 0; i < 20000 && !task_started.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(task_started.load());

    // I) el worker mantiene session_mtx durante la operacion.
    CHECK(!runner.session_mutex().try_lock());
    // J) request_cancel NO necesita session_mtx (funciona mientras el worker
    //    la tiene tomada).
    CHECK(reg.request_cancel(id));

    runner.shutdown();
    CHECK(j->state() == JobState::CANCELLED);
    CHECK(j->count() == 0); // sin resultado parcial publicado
    // progreso nunca supera el total (si el cancel llego antes del primer
    // callback, total puede ser 0 y scanned 0: 0 <= 0 se cumple)
    CHECK(j->bytes_scanned() <= j->total_bytes());
    CHECK(j->progress_percent() >= 0.0 && j->progress_percent() <= 100.0);
    // el scan cancelado no publico nada en el Scanner de la sesion
    CHECK_EQ(s.scanner().count(), 0);
    // tras el worker: session_mtx liberada
    CHECK(runner.session_mutex().try_lock());
    runner.session_mutex().unlock();
    app.detach();
}

// D/E) Progreso monotónico, nunca supera el total y termina en 100%.
static void test_progress(pid_t pid) {
    Session s;
    Application app(s);
    CHECK(app.attach((int)pid).ok);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id = reg.create("first");
    CHECK(id != 0);
    std::vector<std::pair<uint64_t, uint64_t>> rec;
    JobTask task = [&](Application& a, Job& j) -> JobRunResult {
        const ProgressFn cb = j.make_progress_callback();
        ProgressFn wrapped = [&, cb](uint64_t sc, uint64_t tot) {
            cb(sc, tot);
            rec.emplace_back(sc, tot);
        };
        return scan_result(a.first_scan(DataType::U64,
                                        Value{0x1122334455667788ull},
                                        j.cancel_flag(), wrapped));
    };
    CHECK(runner.run(id, task));
    const auto j = reg.get(id);
    wait_terminal(j);
    runner.shutdown();
    CHECK(j->state() == JobState::COMPLETED);
    CHECK(j->count() >= 1);

    // D) progress monotónico
    CHECK(!rec.empty());
    bool mono = true;
    for (size_t i = 1; i < rec.size(); ++i)
        if (rec[i].first < rec[i - 1].first) mono = false;
    CHECK(mono);
    // E) nunca supera el total; el Job no reporta >100
    for (const auto& pr : rec) {
        CHECK(pr.first <= pr.second);
        CHECK(j->progress_percent() >= 0.0);
        CHECK(j->progress_percent() <= 100.0);
    }
    // 100% al terminar normalmente
    CHECK(rec.back().first == rec.back().second);
    CHECK(j->progress_percent() == 100.0);
    // I) el worker libero session_mtx al terminar
    CHECK(runner.session_mutex().try_lock());
    runner.session_mutex().unlock();
    app.detach();
}

// F/G) Un solo job activo: el registry rechaza el segundo mientras hay uno
//      QUEUED o RUNNING, y el runner rechaza cuando su slot esta ocupado.
static void test_single_active(pid_t pid) {
    Session s;
    Application app(s);
    CHECK(app.attach((int)pid).ok);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id1 = reg.create("first");
    CHECK(id1 != 0);
    CHECK(reg.has_active_job());
    CHECK_EQ(reg.active_job_id(), id1);
    // F) segundo job rechazado mientras el primero esta QUEUED
    CHECK_EQ(reg.create("first"), 0);

    JobTask task = [](Application& a, Job&) -> JobRunResult {
        return scan_result(
            a.first_scan(DataType::U64, Value{0x1122334455667788ull}));
    };
    CHECK(runner.run(id1, task));
    CHECK(runner.busy());
    CHECK_EQ(runner.active_job_id(), id1);
    // F/G) segundo job rechazado mientras el primero RUNNING (registry y runner)
    CHECK_EQ(reg.create("first"), 0);
    CHECK(!runner.run(9999, task)); // id inexistente
    CHECK(!runner.run(id1, task));  // slot ocupado

    const auto j = reg.get(id1);
    wait_terminal(j);
    runner.shutdown();
    CHECK(j->state() == JobState::COMPLETED);
    // el slot del registry se libera al terminar
    CHECK(reg.create("first") != 0);
    app.detach();
}

// H) Falla (sin proceso objetivo) -> FAILED con error.
static void test_failed() {
    Session s; // sin attach: la operacion falla con error claro
    Application app(s);
    JobRegistry reg;
    JobRunner runner(app, reg);

    const uint64_t id = reg.create("first");
    CHECK(id != 0);
    JobTask task = [](Application& a, Job&) -> JobRunResult {
        return scan_result(a.first_scan(DataType::I32, Value{1}));
    };
    CHECK(runner.run(id, task));
    const auto j = reg.get(id);
    wait_terminal(j);
    runner.shutdown();
    CHECK(j->state() == JobState::FAILED);
    CHECK(!j->error().empty());
    CHECK(!j->cancel_requested());
}

int main() {
    TargetInfo info;
    const pid_t pid = spawn_target(info);
    CHECK(pid > 0);
    if (pid <= 0) {
        std::printf("\n== test_job_runner: %d checks, %d fallos ==\n", g_pass,
                    g_fail);
        return g_fail == 0 ? 0 : 1;
    }
    CHECK(info.val_addr != 0);
    CHECK(info.big_size > 0);

    test_normal_completes(pid);
    test_cancel_before_start();
    test_cancel_during_scan(pid, info.big_size);
    test_progress(pid);
    test_single_active(pid);
    test_failed();

    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);

    std::printf("\n== test_job_runner: %d checks, %d fallos ==\n", g_pass,
                g_fail);
    return g_fail == 0 ? 0 : 1;
}
