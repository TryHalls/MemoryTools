// test_jobs.cpp - Tests unitarios de Job / JobRegistry (src/web/jobs.h).
//
// Cubre: ciclo de vida (queued -> running -> completed/failed/cancelled),
// ids crecientes, cancelacion (flag + transiciones), progreso (0-100 con
// clamp y total=0), count, elapsed, politica de UN SOLO job activo, get de
// ids inexistentes, remove, clear_finished y concurrencia basica sobre
// cancel (varios hilos pidiendo cancelacion a la vez).
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -pthread tests/test_jobs.cpp src/web/jobs.cpp -o build/test_jobs
// Ejecutar:
//   ./build/test_jobs      (0 = exito, !=0 = fallo)
#include "web/jobs.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

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
            std::printf("FALLO %s:%d: %s == %s (%lld != %lld)\n",                         \
                        __FILE__, __LINE__, #a, #b, _a, _b);                             \
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
            std::printf("FALLO %s:%d: %s == %s (\"%s\" != \"%s\")\n",                     \
                        __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str());             \
        }                                                                                \
    } while (0)

// --- 1) creacion, ids, estado inicial ---------------------------------------

static void test_create() {
    JobRegistry reg;
    CHECK(!reg.has_active_job());
    CHECK_EQ(reg.active_job_id(), 0u);

    const uint64_t id1 = reg.create("first");
    CHECK(id1 != 0);
    CHECK_EQ(id1, 1u);
    CHECK(reg.has_active_job());
    CHECK_EQ(reg.active_job_id(), id1);

    const auto j = reg.get(id1);
    CHECK(j != nullptr);
    CHECK_EQ(j->id(), id1);
    CHECK_STR(j->kind(), "first");
    CHECK(j->state() == JobState::QUEUED);
    CHECK(!j->cancel_requested());
    CHECK_EQ(j->count(), 0u);
    CHECK_EQ(j->bytes_scanned(), 0u);
    CHECK_EQ(j->total_bytes(), 0u);
    CHECK_STR(j->error(), "");

    // get de un id inexistente
    CHECK(reg.get(999) == nullptr);
    CHECK(reg.get(0) == nullptr);
}

// --- 2) ids crecientes (nunca se reutilizan) --------------------------------

static void test_ids_increasing() {
    JobRegistry reg;
    const uint64_t a = reg.create("a");
    CHECK_EQ(a, 1u);
    CHECK(reg.start(a));
    CHECK(reg.complete(a, 0));
    reg.remove(a);
    const uint64_t b = reg.create("b");
    CHECK_EQ(b, 2u);
    CHECK(reg.start(b));
    CHECK(reg.complete(b, 0));
    reg.remove(b);
    const uint64_t c = reg.create("c");
    CHECK_EQ(c, 3u);
    CHECK(reg.start(c));
    CHECK(reg.complete(c, 0));
}

// --- 3) ciclo de vida completo ----------------------------------------------

static void test_lifecycle_completed() {
    JobRegistry reg;
    const uint64_t id = reg.create("next");
    CHECK(reg.start(id));
    auto j = reg.get(id);
    CHECK(j && j->state() == JobState::RUNNING);
    // start sobre un job ya running -> false
    CHECK(!reg.start(id));
    // complete con count
    CHECK(reg.complete(id, 4242));
    CHECK(j->state() == JobState::COMPLETED);
    CHECK_EQ(j->count(), 4242u);
    CHECK(j->elapsed_ms() >= 0);
    // complete sobre un job ya completado -> false
    CHECK(!reg.complete(id, 1));
    // ya no es un job activo
    CHECK(!reg.has_active_job());
    CHECK_EQ(reg.active_job_id(), 0u);
    // clear_finished lo elimina
    CHECK(reg.get(id) != nullptr);
    reg.clear_finished();
    CHECK(reg.get(id) == nullptr);
}

// --- 4) fallo ---------------------------------------------------------------

static void test_failed() {
    JobRegistry reg;
    const uint64_t id = reg.create("pattern");
    CHECK(reg.start(id));
    CHECK(reg.fail(id, "region no legible"));
    const auto j = reg.get(id);
    CHECK(j && j->state() == JobState::FAILED);
    CHECK_STR(j->error(), "region no legible");
    // fail sobre un job ya terminado -> false
    CHECK(!reg.fail(id, "otra vez"));
    // fail sobre un job inexistente -> false
    CHECK(!reg.fail(999, "x"));
}

// --- 5) cancelacion ---------------------------------------------------------

static void test_cancel() {
    // cancel de un job QUEUED -> CANCELLED directo
    {
        JobRegistry reg;
        const uint64_t id = reg.create("first");
        CHECK(reg.request_cancel(id));
        const auto j = reg.get(id);
        CHECK(j && j->state() == JobState::CANCELLED);
        CHECK(j->cancel_requested());
        // start de un job cancelado -> false
        CHECK(!reg.start(id));
        // active_job_id ya no lo considera activo
        CHECK_EQ(reg.active_job_id(), 0u);
    }
    // cancel de un job RUNNING -> solo flag (el worker decide el estado)
    {
        JobRegistry reg;
        const uint64_t id = reg.create("first");
        CHECK(reg.start(id));
        CHECK(reg.request_cancel(id));
        const auto j = reg.get(id);
        CHECK(j->cancel_requested());
        CHECK(j->state() == JobState::RUNNING); // el worker aun termina
        // el worker observa el flag y termina
        CHECK(reg.complete(id, 7));
        CHECK(j->state() == JobState::COMPLETED);
        CHECK(j->cancel_requested()); // el flag queda como aviso
    }
    // cancel de un job inexistente -> false
    {
        JobRegistry reg;
        CHECK(!reg.request_cancel(555));
        CHECK(!reg.is_cancel_requested(555));
    }
    // cancelacion directa via Job::cancel() (solo estado, sin flag previo)
    {
        JobRegistry reg;
        const uint64_t id = reg.create("first");
        const auto j = reg.get(id);
        CHECK(j->cancel());
        CHECK(j->state() == JobState::CANCELLED);
        CHECK(j->cancel_requested());
        // cancel de un job ya cancelado -> false
        CHECK(!j->cancel());
    }
}

// --- 6) progreso ------------------------------------------------------------

static void test_progress() {
    JobRegistry reg;
    const uint64_t id = reg.create("first");
    const auto j = reg.get(id);

    // total == 0 -> 0%
    CHECK(j->progress_percent() == 0.0);
    CHECK_EQ(j->total_bytes(), 0u);

    j->set_total_bytes(100);
    CHECK_EQ(j->total_bytes(), 100u);
    CHECK(j->progress_percent() == 0.0);
    j->set_bytes_scanned(50);
    CHECK_EQ(j->bytes_scanned(), 50u);
    CHECK(j->progress_percent() == 50.0);
    j->set_bytes_scanned(100);
    CHECK(j->progress_percent() == 100.0);
    // bytes > total -> clamp a 100
    j->set_bytes_scanned(200);
    CHECK(j->progress_percent() == 100.0);
    // overflow imposible: double, no enteros
    j->set_bytes_scanned(UINT64_MAX);
    CHECK(j->progress_percent() == 100.0);

    // count via set_count y via complete (start antes de completar)
    j->set_count(33);
    CHECK_EQ(j->count(), 33u);
    CHECK(reg.start(id));
    CHECK(reg.complete(id, 99));
    CHECK_EQ(j->count(), 99u);
}

// --- 7) un solo job activo --------------------------------------------------

static void test_single_active() {
    JobRegistry reg;
    const uint64_t a = reg.create("first");
    CHECK(a != 0);
    // segundo create con uno QUEUED -> 0
    CHECK_EQ(reg.create("next"), 0u);
    CHECK(reg.start(a));
    // tercero con uno RUNNING -> 0
    CHECK_EQ(reg.create("pattern"), 0u);
    // al completar, se puede crear otro
    CHECK(reg.complete(a, 1));
    const uint64_t b = reg.create("next");
    CHECK(b != 0);
    CHECK_EQ(reg.active_job_id(), b);
    // b sigue QUEUED: crear otro -> 0
    CHECK_EQ(reg.create("pattern"), 0u);
    // al completar b, se puede crear otro
    CHECK(reg.start(b));
    CHECK(reg.complete(b, 0));
    const uint64_t c = reg.create("pattern");
    CHECK(c != 0);
    CHECK(reg.request_cancel(c));
    const uint64_t d = reg.create("first");
    CHECK(d != 0);
    CHECK(reg.start(d));
    CHECK(reg.complete(d, 0));
    CHECK_EQ(reg.active_job_id(), 0u);
}

// --- 8) remove --------------------------------------------------------------

static void test_remove() {
    JobRegistry reg;
    const uint64_t id = reg.create("first");
    CHECK(reg.remove(id));
    CHECK(reg.get(id) == nullptr);
    // remove de un id inexistente -> false
    CHECK(!reg.remove(id));
    // remove de un id nunca creado -> false
    CHECK(!reg.remove(777));
    // remove de un job terminado
    const uint64_t b = reg.create("next");
    CHECK(reg.start(b));
    CHECK(reg.complete(b, 0));
    CHECK(reg.remove(b));
    CHECK(reg.get(b) == nullptr);
}

// --- 9) concurrencia basica sobre cancel ------------------------------------

static void test_concurrent_cancel() {
    JobRegistry reg;
    const uint64_t id = reg.create("first");
    CHECK(reg.start(id));

    std::atomic<int> done{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2000; ++i) {
                reg.request_cancel(id);
                reg.is_cancel_requested(id);
            }
            done.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();
    CHECK_EQ(done.load(), 4);
    // el flag quedo marcado y el estado del job sigue consistente
    CHECK(reg.is_cancel_requested(id));
    const auto j = reg.get(id);
    CHECK(j != nullptr);
    // transicion terminal sigue funcionando tras el caos
    CHECK(reg.complete(id, 42));
    CHECK(j->state() == JobState::COMPLETED);
    CHECK_EQ(j->count(), 42u);
}

int main() {
    test_create();
    test_ids_increasing();
    test_lifecycle_completed();
    test_failed();
    test_cancel();
    test_progress();
    test_single_active();
    test_remove();
    test_concurrent_cancel();
    std::printf("\n== test_jobs: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
