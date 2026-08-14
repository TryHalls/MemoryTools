// job_runner.cpp - Implementacion del JobRunner (FASE W-2 PASO 6).
#include "job_runner.h"

namespace mt {
namespace web {

bool JobRunner::run(uint64_t job_id, const JobTask& task) {
    std::lock_guard<std::mutex> lk(runner_mtx_);
    if (active_ != 0) return false; // un solo job pesado a la vez
    // El worker anterior ya termino (active_ == 0): unir su hilo antes de
    // lanzar el siguiente. No hay deadlock: finish_slot() ya se ejecuto y el
    // worker no vuelve a tomar runner_mtx_.
    if (thread_.joinable()) thread_.join();
    const auto job = registry_.get(job_id);
    if (!job) return false;
    if (job->state() != JobState::QUEUED) return false;
    active_ = job_id;
    thread_ = std::thread([this, job_id, task]() { worker(job_id, task); });
    return true;
}

bool JobRunner::busy() const {
    std::lock_guard<std::mutex> lk(runner_mtx_);
    return active_ != 0;
}

uint64_t JobRunner::active_job_id() const {
    std::lock_guard<std::mutex> lk(runner_mtx_);
    return active_;
}

void JobRunner::shutdown() {
    {
        std::lock_guard<std::mutex> lk(runner_mtx_);
        if (active_ != 0) registry_.request_cancel(active_);
    }
    std::thread t;
    {
        std::lock_guard<std::mutex> lk(runner_mtx_);
        if (thread_.joinable()) t = std::move(thread_);
    }
    if (t.joinable()) t.join();
}

void JobRunner::worker(uint64_t job_id, const JobTask& task) {
    const auto job = registry_.get(job_id);
    if (!job) {
        finish_slot();
        return;
    }
    // QUEUED -> RUNNING. False si el job fue cancelado antes de empezar (la
    // capa superior ya lo dejo CANCELLED); en ese caso no se ejecuta nada.
    if (!job->start()) {
        finish_slot();
        return;
    }

    JobRunResult r;
    {
        // Ownership de Session: el mutex queda tomado durante TODA la
        // operacion. Otras operaciones sobre la sesion quedan bloqueadas
        // (o rechazadas con try_lock) mientras el worker corre.
        std::lock_guard<std::mutex> lk(session_mtx_);
        if (job->cancel_requested()) {
            r.cancelled = true; // cancel justo entre start() y la tarea
        } else {
            try {
                r = task(app_, *job);
            } catch (const std::exception& e) {
                r.ok = false;
                r.cancelled = false;
                r.error = std::string("excepcion en el worker: ") + e.what();
            } catch (...) {
                r.ok = false;
                r.cancelled = false;
                r.error = "excepcion desconocida en el worker";
            }
        }
    }

    // Transicion terminal: un job realmente cancelado NUNCA termina como
    // COMPLETED ni FAILED.
    if (r.cancelled) {
        job->cancel(); // RUNNING -> CANCELLED (el flag ya estaba marcado)
    } else if (r.ok) {
        job->complete(r.count);
    } else {
        job->fail(r.error);
    }
    finish_slot();
}

void JobRunner::finish_slot() {
    std::lock_guard<std::mutex> lk(runner_mtx_);
    active_ = 0;
}

} // namespace web
} // namespace mt
