// jobs.h - Gestion de jobs asincronos (FASE W-1).
//
// (FASE W-2 PASO 6) Integracion con el core: el worker usa
//   Job::cancel_flag()          -> const std::atomic<bool>* para el scanner
//   Job::make_progress_callback() -> ProgressFn que actualiza bytes/total
// El Job conserva SOLO estado, count, progreso, error y cancelacion; los
// resultados grandes viven en Application/Session/Scanner.
#include "../chunk.h"
//
// Infraestructura headless para la futura Web UI local: un Job representa
// una operacion pesada (First/Next Scan, Pattern, Pointer Scan) que puede
// tardar, y un JobRegistry gestiona su estado y ownership. Esta fase NO
// abre sockets ni crea threads: solo estado, progreso y cancelacion.
//
// Politica: UN SOLO JOB ACTIVO A LA VEZ (QUEUED o RUNNING). El segundo
// intento de create() devuelve 0 y la capa superior lo detecta.
//
// Concurrencia:
//   - campos de solo progreso/cancelacion -> atomicos (sin lock)
//   - estado (transiciones QUEUED/RUNNING/COMPLETED/FAILED/CANCELLED)
//     -> atomic<int> con compare_exchange (CAS)
//   - campos no atomicos (id, kind, total_bytes, elapsed, error)
//     -> protegidos por el mutex del Job
//   - el mapa de jobs del registry -> mutex propio
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mt {
namespace web {

enum class JobState {
    QUEUED,     // creado, esperando al worker
    RUNNING,    // en ejecucion
    COMPLETED,  // termino bien
    FAILED,     // termino con error
    CANCELLED,  // cancelado (antes de empezar o durante)
};

const char* job_state_name(JobState s);

// Un job individual. Los campos visibles al hilo HTTP se leen con metodos
// seguros (atomicos + mutex); los mutadores los usa el worker.
class Job {
public:
    Job(uint64_t id, std::string kind);

    // --- lectura (cualquier hilo) -----------------------------------------
    uint64_t id() const { return id_; }
    std::string kind() const;

    JobState state() const;
    bool cancel_requested() const;
    uint64_t bytes_scanned() const;
    uint64_t count() const;
    uint64_t total_bytes() const;
    // ms transcurridos: en vivo si RUNNING, valor final si terminal.
    int64_t elapsed_ms() const;
    std::string error() const;
    // Progreso 0-100. total==0 -> 0; bytes>total -> clamp a 100; sin overflow.
    double progress_percent() const;

    // --- para el worker (integracion con el core) -------------------------
    // Flag de cancelacion observable por el scanner (se comprueba por bloque).
    const std::atomic<bool>* cancel_flag() const { return &cancel_requested_; }
    // Callback de progreso que actualiza bytes_scanned/total_bytes del Job
    // (thread-safe: total bajo mutex, scanned atomico). Sin throttling.
    ProgressFn make_progress_callback();

    // --- mutadores (worker / capa superior) -------------------------------
    void set_total_bytes(uint64_t n);
    void set_bytes_scanned(uint64_t n);
    void set_count(uint64_t n);
    // Marca el flag de cancelacion (lo observa el worker; no cambia estado).
    void request_cancel();
    // Transiciones de estado (CAS): devuelven false si el estado actual no
    // permite la transicion.
    bool start();                          // QUEUED -> RUNNING
    bool complete(uint64_t count);         // RUNNING -> COMPLETED
    bool fail(const std::string& error);   // QUEUED/RUNNING -> FAILED
    bool cancel();                         // QUEUED/RUNNING -> CANCELLED (+flag)

private:
    bool transition(JobState from, JobState to);
    bool transition_active_to(JobState to); // QUEUED/RUNNING -> to
    void mark_terminal();

    const uint64_t id_;
    const std::string kind_;

    std::atomic<int> state_{static_cast<int>(JobState::QUEUED)};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<uint64_t> bytes_scanned_{0};
    std::atomic<uint64_t> count_{0};

    mutable std::mutex mtx_;
    uint64_t total_bytes_ = 0;
    std::chrono::steady_clock::time_point started_at_{};
    int64_t elapsed_ms_ = 0;
    std::string error_;
};

// Registry de jobs: ownership + politica de un solo job activo.
class JobRegistry {
public:
    // Crea un job QUEUED con el siguiente id (creciente, nunca se reutiliza).
    // Devuelve 0 si ya existe un job activo (QUEUED/RUNNING).
    uint64_t create(const std::string& kind);

    // Transiciones sobre el job con ese id (false si no existe o estado invalido).
    bool start(uint64_t id);
    bool complete(uint64_t id, uint64_t count);
    bool fail(uint64_t id, const std::string& error);

    // Cancelacion:
    //   - si el job aun esta QUEUED -> pasa a CANCELLED directamente;
    //   - si esta RUNNING -> solo marca el flag atomico (el worker lo observa
    //     y terminara el job cuando pueda).
    bool request_cancel(uint64_t id);
    bool is_cancel_requested(uint64_t id) const;

    // Acceso por id (nullptr si no existe). El shared_ptr mantiene vivo el
    // job aunque se llame a remove() mientras un worker lo usa.
    std::shared_ptr<Job> get(uint64_t id) const;

    // Elimina un job del mapa (no interrumpe un job en marcha: el worker
    // conserva su shared_ptr). Devuelve false si no existia.
    bool remove(uint64_t id);

    // Elimina los jobs ya terminales (COMPLETED/FAILED/CANCELLED).
    void clear_finished();

    bool has_active_job() const;
    // id del job activo (QUEUED/RUNNING), o 0 si no hay ninguno.
    uint64_t active_job_id() const;

private:
    mutable std::mutex mtx_;
    uint64_t next_id_ = 1;
    std::unordered_map<uint64_t, std::shared_ptr<Job>> jobs_;
};

} // namespace web
} // namespace mt
