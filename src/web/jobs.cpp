// jobs.cpp - Implementacion de Job y JobRegistry (FASE W-1).
#include "jobs.h"

namespace mt {
namespace web {

const char* job_state_name(JobState s) {
    switch (s) {
        case JobState::QUEUED:    return "queued";
        case JobState::RUNNING:   return "running";
        case JobState::COMPLETED: return "completed";
        case JobState::FAILED:    return "failed";
        case JobState::CANCELLED: return "cancelled";
    }
    return "unknown";
}

Job::Job(uint64_t id, std::string kind) : id_(id), kind_(std::move(kind)) {}

std::string Job::kind() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return kind_;
}

JobState Job::state() const {
    return static_cast<JobState>(state_.load(std::memory_order_acquire));
}

bool Job::cancel_requested() const {
    return cancel_requested_.load(std::memory_order_relaxed);
}

uint64_t Job::bytes_scanned() const {
    return bytes_scanned_.load(std::memory_order_relaxed);
}

uint64_t Job::count() const {
    return count_.load(std::memory_order_relaxed);
}

uint64_t Job::total_bytes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return total_bytes_;
}

int64_t Job::elapsed_ms() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (state() == JobState::RUNNING && started_at_ != std::chrono::steady_clock::time_point{}) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started_at_)
            .count();
    }
    return elapsed_ms_;
}

std::string Job::error() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return error_;
}

double Job::progress_percent() const {
    const uint64_t total = total_bytes();
    if (total == 0) return 0.0;
    const uint64_t done = bytes_scanned();
    if (done >= total) return 100.0;
    // double evita overflow de la multiplicacion por 100.
    double p = (double)done / (double)total * 100.0;
    return p < 0.0 ? 0.0 : (p > 100.0 ? 100.0 : p);
}

void Job::set_total_bytes(uint64_t n) {
    std::lock_guard<std::mutex> lk(mtx_);
    total_bytes_ = n;
}

void Job::set_bytes_scanned(uint64_t n) {
    bytes_scanned_.store(n, std::memory_order_relaxed);
}

void Job::set_count(uint64_t n) {
    count_.store(n, std::memory_order_relaxed);
}

void Job::request_cancel() {
    cancel_requested_.store(true, std::memory_order_relaxed);
}

bool Job::transition(JobState from, JobState to) {
    int expected = static_cast<int>(from);
    return state_.compare_exchange_strong(expected, static_cast<int>(to),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire);
}

bool Job::transition_active_to(JobState to) {
    while (true) {
        int cur = state_.load(std::memory_order_acquire);
        if (cur != static_cast<int>(JobState::QUEUED) &&
            cur != static_cast<int>(JobState::RUNNING))
            return false;
        if (state_.compare_exchange_weak(cur, static_cast<int>(to),
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
            return true;
    }
}

bool Job::start() {
    if (!transition(JobState::QUEUED, JobState::RUNNING)) return false;
    std::lock_guard<std::mutex> lk(mtx_);
    started_at_ = std::chrono::steady_clock::now();
    return true;
}

bool Job::complete(uint64_t count) {
    if (!transition(JobState::RUNNING, JobState::COMPLETED)) return false;
    count_.store(count, std::memory_order_relaxed);
    mark_terminal();
    return true;
}

bool Job::fail(const std::string& error) {
    if (!transition_active_to(JobState::FAILED)) return false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = error;
    }
    mark_terminal();
    return true;
}

bool Job::cancel() {
    if (!transition_active_to(JobState::CANCELLED)) return false;
    cancel_requested_.store(true, std::memory_order_relaxed);
    mark_terminal();
    return true;
}

void Job::mark_terminal() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (started_at_ != std::chrono::steady_clock::time_point{}) {
        elapsed_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - started_at_)
                          .count();
    }
}

// --- JobRegistry -------------------------------------------------------------

uint64_t JobRegistry::create(const std::string& kind) {
    std::lock_guard<std::mutex> lk(mtx_);
    // Politica: un solo job activo (QUEUED o RUNNING) a la vez.
    for (const auto& kv : jobs_) {
        const JobState s = kv.second->state();
        if (s == JobState::QUEUED || s == JobState::RUNNING) return 0;
    }
    const uint64_t id = next_id_++;
    jobs_.emplace(id, std::make_shared<Job>(id, kind));
    return id;
}

std::shared_ptr<Job> JobRegistry::get(uint64_t id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : it->second;
}

bool JobRegistry::start(uint64_t id) {
    const auto j = get(id);
    return j && j->start();
}

bool JobRegistry::complete(uint64_t id, uint64_t count) {
    const auto j = get(id);
    return j && j->complete(count);
}

bool JobRegistry::fail(uint64_t id, const std::string& error) {
    const auto j = get(id);
    return j && j->fail(error);
}

bool JobRegistry::request_cancel(uint64_t id) {
    const auto j = get(id);
    if (!j) return false;
    // QUEUED -> CANCELLED directamente (nunca llego a ejecutarse);
    // RUNNING -> solo el flag (el worker lo observa y terminara el job).
    if (j->state() == JobState::QUEUED) return j->cancel();
    j->request_cancel();
    return true;
}

bool JobRegistry::is_cancel_requested(uint64_t id) const {
    const auto j = get(id);
    return j && j->cancel_requested();
}

bool JobRegistry::remove(uint64_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    return jobs_.erase(id) > 0;
}

void JobRegistry::clear_finished() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const JobState s = it->second->state();
        if (s == JobState::COMPLETED || s == JobState::FAILED ||
            s == JobState::CANCELLED)
            it = jobs_.erase(it);
        else
            ++it;
    }
}

uint64_t JobRegistry::active_job_id() const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& kv : jobs_) {
        const JobState s = kv.second->state();
        if (s == JobState::QUEUED || s == JobState::RUNNING) return kv.first;
    }
    return 0;
}

bool JobRegistry::has_active_job() const {
    return active_job_id() != 0;
}

} // namespace web
} // namespace mt
