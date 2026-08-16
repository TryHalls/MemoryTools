// api.h - API JSON completa de MemoryTool (FASE W-4).
//
// Capa entre el servidor HTTP (routing) y Application/JobRunner. Ninguna
// funcion imprime: cada endpoint devuelve ApiResponse {status, body JSON}.
//
// Formato de respuesta:
//   exito: {"ok": true, ...}
//   error: {"ok": false, "error": "...", "code": "..."}
// con 'code' estable (bad_request, unauthorized, not_found, busy, no_process,
// scan_invalid, cancelled, failed, forbidden, payload_too_large, ...).
//
// Direcciones/PID/valores de 64 bits se serializan como STRING (JS pierde
// precision > 2^53).
//
// Ownership de Session: los endpoints que tocan la sesion usan
// runner_.session_mutex() con try_lock -> 409 busy si un job del worker esta
// escaneando. status/processes/jobs/cancel NO necesitan el mutex.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../application.h"
#include "http.h"
#include "job_runner.h"
#include "jobs.h"
#include "json.h"

namespace mt {
namespace web {

struct ApiResponse {
    int status = 200;
    std::string body; // JSON ya serializado
};

class Api {
public:
    Api(Application& app, JobRegistry& jobs, JobRunner& runner)
        : app_(app), jobs_(jobs), runner_(runner) {}

    // Dispatch completo de una request (el servidor ya valido token/Host).
    // Nunca deja escapar excepciones: -> 500 internal_error.
    ApiResponse handle(const HttpRequest& req);

private:
    // --- helpers de respuesta ---------------------------------------------
    ApiResponse ok(json::JsonValue v); // antepone {"ok": true}
    ApiResponse err(int status, const std::string& code, const std::string& msg);
    ApiResponse busy(); // 409 {"code":"busy","error":"Another operation is running"}

    // Parsea el body JSON de la request; false -> resp ya contiene 400.
    bool body_json(const HttpRequest& req, json::JsonValue& out,
                   ApiResponse& resp);

    // Serializacion de un Job (formato de GET /api/jobs/<id>).
    json::JsonValue job_json(const Job& j);

    // Crea el job, lo lanza en el runner y rellena {job_id, state:"queued"}.
    bool start_job(const std::string& kind, JobTask task, ApiResponse& out);

    // --- handlers ---------------------------------------------------------
    ApiResponse h_status();
    ApiResponse h_processes();
    ApiResponse h_attach(const HttpRequest& req);
    ApiResponse h_detach();
    ApiResponse h_job_get(uint64_t id);
    ApiResponse h_job_cancel(uint64_t id);
    ApiResponse h_scan_first(const HttpRequest& req);
    ApiResponse h_scan_next(const HttpRequest& req);
    ApiResponse h_pattern(const HttpRequest& req);
    ApiResponse h_results(uint64_t offset, uint64_t limit);
    ApiResponse h_pattern_results(uint64_t offset, uint64_t limit);
    ApiResponse h_memory(uint64_t addr, size_t len);
    ApiResponse h_write(const HttpRequest& req);
    ApiResponse h_table();
    ApiResponse h_table_add(const HttpRequest& req);
    ApiResponse h_table_add_result(const HttpRequest& req);
    ApiResponse h_table_remove(const HttpRequest& req);
    ApiResponse h_table_clear();
    ApiResponse h_table_toggle(const HttpRequest& req);
    ApiResponse h_table_read(const HttpRequest& req);
    ApiResponse h_table_set(const HttpRequest& req);
    ApiResponse h_table_save(const HttpRequest& req);
    ApiResponse h_table_load(const HttpRequest& req);
    ApiResponse h_pointer_scan(const HttpRequest& req);
    ApiResponse h_pointer_results(uint64_t offset, uint64_t limit);
    ApiResponse h_pointer_add(const HttpRequest& req);
    ApiResponse h_pointer_resolve(const HttpRequest& req);

    Application& app_;
    JobRegistry& jobs_;
    JobRunner& runner_;

    // Ultimo resultado de un job 'pattern' (para GET /api/pattern/results).
    // El worker escribe bajo pat_mtx_ al completar; el hilo HTTP lee.
    mutable std::mutex pat_mtx_;
    std::optional<PatternScanResult> last_pattern_;
};

} // namespace web
} // namespace mt
