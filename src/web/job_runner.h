// job_runner.h - Ejecuta UNA operacion pesada de Application en un hilo
// (FASE W-2 PASO 6).
//
// Integra Job/JobRegistry con Application: el worker traduce el outcome de la
// operacion (ok/cancelled/count/error) al estado terminal del Job
// (COMPLETED/CANCELLED/FAILED). Un solo job a la vez (refuerza la politica
// del registry con un slot propio).
//
// Ownership de Session: Application/Session NO son thread-safe, asi que el
// worker mantiene session_mutex() tomado durante TODA la operacion. Cualquier
// otra operacion que toque la sesion (attach/detach/escrituras/otros scans)
// debe bloquear ese mutex (o usar try_lock para rechazar con 409 en el futuro
// servidor). Consultar el estado/progreso del Job NO necesita ese mutex.
//
// Cancelacion: request_cancel() solo marca el atomic del Job (sin tocar
// session_mtx); el scanner lo observa por bloque y la operacion termina con
// cancelled=true -> el worker pone el Job en CANCELLED (nunca COMPLETED/FAILED
// si realmente fue cancelado).
//
// Shutdown: request_cancel del job activo (sin session_mtx) + join del worker.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "../application.h"
#include "jobs.h"

namespace mt {
namespace web {

// Resultado que el worker traduce al estado terminal del Job. La TAREA lo
// rellena a partir del outcome de la operacion de Application; el worker
// decide la transicion (el Job no conserva resultados grandes, solo count).
struct JobRunResult {
    bool ok = false;
    bool cancelled = false;
    uint64_t count = 0;
    std::string error;
};

// Tarea ejecutada por el worker: recibe Application (para operar) y el Job
// (para cancel_flag/progress) y devuelve el resultado terminal. Ejecuta la
// operacion con Application::... (&job->cancel_flag(), job->make_progress_callback())
// y mapea su outcome a JobRunResult.
using JobTask = std::function<JobRunResult(Application&, Job&)>;

class JobRunner {
public:
    JobRunner(Application& app, JobRegistry& registry)
        : app_(app), registry_(registry) {}

    // Lanza el worker para un job QUEUED ya creado. Devuelve false si ya hay
    // un worker activo, el job no existe o no esta QUEUED (p. ej. ya fue
    // cancelado antes de empezar). El hilo del worker anterior (ya terminado)
    // se une antes de lanzar el nuevo.
    bool run(uint64_t job_id, const JobTask& task);

    // Estado del slot, sin esperar al worker.
    bool busy() const;
    uint64_t active_job_id() const;

    // Mutex que protege Session/Application. El worker lo mantiene durante la
    // operacion; el futuro servidor lo bloqueara (o hara try_lock) para las
    // demas operaciones que toquen la sesion.
    std::mutex& session_mutex() { return session_mtx_; }

    // Cierre limpio: marca cancel al job activo (sin adquirir session_mtx) y
    // espera a que el worker termine. Seguro llamarlo desde cualquier hilo.
    void shutdown();

private:
    void worker(uint64_t job_id, const JobTask& task);
    void finish_slot(); // libera el slot (runner_mtx_)

    Application& app_;
    JobRegistry& registry_;
    std::mutex session_mtx_;
    mutable std::mutex runner_mtx_;
    std::thread thread_;
    uint64_t active_ = 0; // 0 = sin worker; protegido por runner_mtx_
};

} // namespace web
} // namespace mt
