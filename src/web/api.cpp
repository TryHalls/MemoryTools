// api.cpp - Implementacion de la API JSON completa (FASE W-4).
#include "api.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

#include "../pattern.h"
#include "../pointer_resolver.h"
#include "../process.h"

namespace mt {
namespace web {

namespace {

// Direccion como string JSON (hex de 16 digitos).
std::string hex_addr(uint64_t a) {
    char b[32];
    snprintf(b, sizeof b, "0x%016llx", (unsigned long long)a);
    return b;
}

// PID: solo decimal representable (IMP-1: PIDs gigantes -> error normal).
bool parse_pid(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    if (v < 0 || v > INT_MAX) return false;
    out = (int)v;
    return true;
}

// Parsea "a=1&b=2" y devuelve el valor numerico de 'key' (o nullopt).
std::optional<uint64_t> query_uint(const std::string& query,
                                   const std::string& key) {
    size_t p = 0;
    while (p <= query.size()) {
        const size_t amp = query.find('&', p);
        const std::string item =
            query.substr(p, amp == std::string::npos ? std::string::npos
                                                     : amp - p);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == key) {
            const std::string v = item.substr(eq + 1);
            if (v.empty()) return std::nullopt;
            errno = 0;
            char* end = nullptr;
            // base 0: acepta decimal y 0x... (direcciones en la URL).
            const unsigned long long u = std::strtoull(v.c_str(), &end, 0);
            if (end == v.c_str() || *end != '\0' || errno == ERANGE)
                return std::nullopt;
            return (uint64_t)u;
        }
        if (amp == std::string::npos) break;
        p = amp + 1;
    }
    return std::nullopt;
}

// Nombre de archivo seguro para table save/load: solo [A-Za-z0-9._-], sin
// '.', '..', vacio, ni mas de 64 chars. Nunca rutas del cliente.
bool valid_table_name(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    if (n == "." || n == "..") return false;
    if (n[0] == '.') return false;
    for (char c : n) {
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' ||
              c == '.'))
            return false;
    }
    return true;
}

// Filtros numericos/dinamicos con el mismo vocabulario que la CLI.
bool parse_filter(const std::string& s, Filter& out) {
    if (s == "exact") { out = Filter::EXACT; return true; }
    if (s == "changed") { out = Filter::CHANGED; return true; }
    if (s == "unchanged") { out = Filter::UNCHANGED; return true; }
    if (s == "increased") { out = Filter::INCREASED; return true; }
    if (s == "decreased") { out = Filter::DECREASED; return true; }
    if (s == "greater") { out = Filter::GREATER; return true; }
    if (s == "less") { out = Filter::LESS; return true; }
    if (s == "ge" || s == ">=") { out = Filter::GE; return true; }
    if (s == "le" || s == "<=") { out = Filter::LE; return true; }
    if (s == "ne" || s == "!=") { out = Filter::NE; return true; }
    return false;
}

// Token de tipo dinamico (equivalente al de la CLI): string / bytes|pattern|aob.
DataType dynamic_type_token(const std::string& tok) {
    if (tok == "string") return DataType::STRING;
    if (tok == "bytes" || tok == "pattern" || tok == "aob")
        return DataType::BYTES;
    return DataType::I32;
}

// Construye el BytePattern de un valor dinamico (string -> texto exacto;
// bytes -> parse_pattern con wildcards).
BytePattern build_dynamic_pattern(DataType type, const std::string& text,
                                  std::string& err) {
    if (type == DataType::STRING) return pattern_from_text(text, err);
    BytePattern pat;
    if (!parse_pattern(text, pat)) err = pat.error;
    return pat;
}

// Resultado de Job a partir de un ScanOutcome (patron de tarea comun).
JobRunResult from_scan(const ScanOutcome& o) {
    JobRunResult r;
    r.ok = o.ok;
    r.cancelled = o.cancelled;
    r.count = o.count;
    r.error = o.error;
    return r;
}

json::JsonValue ptr_ref_json(const PointerChainRef& ref) {
    const bool persistent = ref.root.kind == PointerBaseKind::MODULE;
    json::JsonValue::Array offs;
    for (uint64_t o : ref.offsets) offs.push_back(json::JsonValue::make_string(hex_addr(o)));
    return json::JsonValue::make_object({
        {"kind", json::JsonValue::make_string(
                     persistent ? "MODULE" : "ABSOLUTE")},
        {"module", json::JsonValue::make_string(ref.root.module)},
        {"root_offset", json::JsonValue::make_string(hex_addr(ref.root.offset))},
        {"offsets", json::JsonValue::make_array(std::move(offs))},
        {"value_type", json::JsonValue::make_string(type_name(ref.value_type))},
        {"persistent", json::JsonValue::make_bool(persistent)},
    });
}

} // namespace

// --- helpers de respuesta ---------------------------------------------------

ApiResponse Api::ok(json::JsonValue v) {
    if (v.type() == json::JsonValue::Type::Object) {
        json::JsonValue::Object obj = *v.as_object();
        obj.insert(obj.begin(),
                   {"ok", json::JsonValue::make_bool(true)});
        v = json::JsonValue::make_object(std::move(obj));
    }
    return {200, json::write(v)};
}

ApiResponse Api::err(int status, const std::string& code,
                     const std::string& msg) {
    const json::JsonValue v = json::JsonValue::make_object({
        {"ok", json::JsonValue::make_bool(false)},
        {"error", json::JsonValue::make_string(msg)},
        {"code", json::JsonValue::make_string(code)},
    });
    return {status, json::write(v)};
}

ApiResponse Api::busy() {
    return err(409, "busy", "Another operation is running");
}

bool Api::body_json(const HttpRequest& req, json::JsonValue& out,
                    ApiResponse& resp) {
    if (req.body.empty()) {
        resp = err(400, "bad_request", "cuerpo JSON vacio");
        return false;
    }
    json::ParseError pe;
    if (!json::parse(req.body, out, pe)) {
        resp = err(400, "bad_request", pe.message);
        return false;
    }
    return true;
}

json::JsonValue Api::job_json(const Job& j) {
    return json::JsonValue::make_object({
        {"id", json::JsonValue::make_string(std::to_string(j.id()))},
        {"kind", json::JsonValue::make_string(j.kind())},
        {"state", json::JsonValue::make_string(job_state_name(j.state()))},
        {"progress", json::JsonValue::make_double(j.progress_percent())},
        {"bytes_scanned",
         json::JsonValue::make_string(std::to_string(j.bytes_scanned()))},
        {"total_bytes",
         json::JsonValue::make_string(std::to_string(j.total_bytes()))},
        {"count", json::JsonValue::make_string(std::to_string(j.count()))},
        {"elapsed_ms", json::JsonValue::make_int(j.elapsed_ms())},
        {"cancelled", json::JsonValue::make_bool(j.cancel_requested())},
        {"error", json::JsonValue::make_string(j.error())},
    });
}

bool Api::start_job(const std::string& kind, JobTask task, ApiResponse& out) {
    const uint64_t id = jobs_.create(kind);
    if (id == 0) {
        out = busy();
        return false;
    }
    if (!runner_.run(id, task)) {
        // No se pudo lanzar: liberar el slot del registry.
        jobs_.request_cancel(id);
        out = busy();
        return false;
    }
    out = ok(json::JsonValue::make_object({
        {"job_id", json::JsonValue::make_string(std::to_string(id))},
        {"state", json::JsonValue::make_string("queued")},
    }));
    return true;
}

// --- dispatch ---------------------------------------------------------------

ApiResponse Api::handle(const HttpRequest& req) {
    try {
        // Jobs: GET /api/jobs/<id> y POST /api/jobs/<id>/cancel
        if (req.path.rfind("/api/jobs/", 0) == 0) {
            std::string rest = req.path.substr(10);
            bool want_cancel = false;
            if (rest.size() > 7 &&
                rest.compare(rest.size() - 7, 7, "/cancel") == 0) {
                want_cancel = true;
                rest = rest.substr(0, rest.size() - 7);
            }
            if (rest.empty()) return err(404, "not_found", "job no encontrado");
            for (char c : rest)
                if (c < '0' || c > '9')
                    return err(404, "not_found", "job no encontrado");
            errno = 0;
            char* end = nullptr;
            const unsigned long long id = std::strtoull(rest.c_str(), &end, 10);
            if (errno == ERANGE || end == rest.c_str() || *end != '\0')
                return err(404, "not_found", "job no encontrado");
            if (want_cancel) {
                if (req.method != "POST")
                    return err(405, "method_not_allowed", "metodo no soportado");
                return h_job_cancel(id);
            }
            if (req.method != "GET")
                return err(405, "method_not_allowed", "metodo no soportado");
            return h_job_get(id);
        }

        // Tabla
        if (req.path == "/api/table" && req.method == "GET") return h_table();
        if (req.path == "/api/table/add" && req.method == "POST")
            return h_table_add(req);
        if (req.path == "/api/table/add-result" && req.method == "POST")
            return h_table_add_result(req);
        if (req.path == "/api/table/remove" && req.method == "POST")
            return h_table_remove(req);
        if (req.path == "/api/table/clear" && req.method == "POST")
            return h_table_clear();
        if (req.path == "/api/table/toggle" && req.method == "POST")
            return h_table_toggle(req);
        if (req.path == "/api/table/read" && req.method == "POST")
            return h_table_read(req);
        if (req.path == "/api/table/set" && req.method == "POST")
            return h_table_set(req);
        if (req.path == "/api/table/save" && req.method == "POST")
            return h_table_save(req);
        if (req.path == "/api/table/load" && req.method == "POST")
            return h_table_load(req);
        if (req.path.rfind("/api/table/", 0) == 0)
            return err(404, "not_found", "ruta no encontrada");

        // Pointer
        if (req.path == "/api/pointer/scan" && req.method == "POST")
            return h_pointer_scan(req);
        if (req.path == "/api/pointer/add" && req.method == "POST")
            return h_pointer_add(req);
        if (req.path == "/api/pointer/resolve" && req.method == "POST")
            return h_pointer_resolve(req);
        if (req.path == "/api/pointer/results" && req.method == "GET") {
            const uint64_t off = query_uint(req.query, "offset").value_or(0);
            const uint64_t lim = query_uint(req.query, "limit").value_or(50);
            return h_pointer_results(off, lim);
        }
        if (req.path.rfind("/api/pointer/", 0) == 0)
            return err(404, "not_found", "ruta no encontrada");

        // Escaneos / resultados / memoria
        if (req.path == "/api/status" && req.method == "GET") return h_status();
        if (req.path == "/api/processes" && req.method == "GET")
            return h_processes();
        if (req.path == "/api/attach" && req.method == "POST")
            return h_attach(req);
        if (req.path == "/api/detach" && req.method == "POST")
            return h_detach();
        if (req.path == "/api/scan/first" && req.method == "POST")
            return h_scan_first(req);
        if (req.path == "/api/scan/next" && req.method == "POST")
            return h_scan_next(req);
        if (req.path == "/api/pattern" && req.method == "POST")
            return h_pattern(req);
        if (req.path == "/api/write" && req.method == "POST")
            return h_write(req);
        if (req.path == "/api/results" && req.method == "GET") {
            const uint64_t off = query_uint(req.query, "offset").value_or(0);
            const uint64_t lim = query_uint(req.query, "limit").value_or(100);
            return h_results(off, lim);
        }
        if (req.path == "/api/pattern/results" && req.method == "GET") {
            const uint64_t off = query_uint(req.query, "offset").value_or(0);
            const uint64_t lim = query_uint(req.query, "limit").value_or(100);
            return h_pattern_results(off, lim);
        }
        if (req.path == "/api/memory" && req.method == "GET") {
            const uint64_t addr = query_uint(req.query, "address").value_or(0);
            const uint64_t len = query_uint(req.query, "length").value_or(256);
            return h_memory(addr, len);
        }

        // Metodo no permitido en rutas conocidas.
        if (req.path == "/api/status" || req.path == "/api/processes" ||
            req.path == "/api/attach" || req.path == "/api/detach" ||
            req.path == "/api/scan/first" || req.path == "/api/scan/next" ||
            req.path == "/api/pattern" || req.path == "/api/write" ||
            req.path == "/api/results" || req.path == "/api/memory")
            return err(405, "method_not_allowed", "metodo no soportado");

        return err(404, "not_found", "ruta no encontrada");
    } catch (...) {
        return err(500, "internal_error", "error interno del servidor");
    }
}

// --- status / processes ------------------------------------------------------

ApiResponse Api::h_status() {
    json::JsonValue job = json::JsonValue::make_null();
    const uint64_t jid = jobs_.active_job_id();
    if (jid) {
        if (const auto j = jobs_.get(jid)) job = job_json(*j);
    }
    return ok(json::JsonValue::make_object({
        {"attached", json::JsonValue::make_bool(app_.has_pid())},
        {"pid", json::JsonValue::make_string(std::to_string(app_.pid()))},
        {"runner_busy", json::JsonValue::make_bool(runner_.busy())},
        {"job", std::move(job)},
    }));
}

ApiResponse Api::h_processes() {
    json::JsonValue::Array items;
    for (const auto& p : list_processes()) {
        items.push_back(json::JsonValue::make_object({
            {"pid", json::JsonValue::make_string(std::to_string(p.pid))},
            {"name", json::JsonValue::make_string(p.name)},
            {"user", json::JsonValue::make_string(p.user)},
            {"state", json::JsonValue::make_string(std::string(1, p.state))},
            {"rss_kb", json::JsonValue::make_int(p.rss_kb)},
            {"accessible", json::JsonValue::make_bool(p.accessible)},
        }));
    }
    return ok(json::JsonValue::make_object(
        {{"processes", json::JsonValue::make_array(std::move(items))}}));
}

// --- attach / detach ----------------------------------------------------------

ApiResponse Api::h_attach(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    const json::JsonValue* pv = b.get("pid");
    if (!pv) return err(400, "bad_request", "falta el campo pid");
    std::string pid_s;
    if (pv->is_string())
        pid_s = *pv->as_string();
    else if (pv->type() == json::JsonValue::Type::Int)
        pid_s = std::to_string(pv->as_int());
    else
        return err(400, "bad_request", "pid invalido");
    int pid = 0;
    if (!parse_pid(pid_s, pid))
        return err(400, "bad_request", "pid invalido o fuera de rango");

    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const AttachOutcome o = app_.attach(pid);
    if (!o.ok) return err(400, "failed", o.error);
    return ok(json::JsonValue::make_object({
        {"attached", json::JsonValue::make_bool(true)},
        {"pid", json::JsonValue::make_string(std::to_string(pid))},
        {"switched", json::JsonValue::make_bool(o.switched)},
    }));
}

ApiResponse Api::h_detach() {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    app_.detach();
    return ok(json::JsonValue::make_object(
        {{"attached", json::JsonValue::make_bool(false)}}));
}

// --- jobs ---------------------------------------------------------------------

ApiResponse Api::h_job_get(uint64_t id) {
    const auto j = jobs_.get(id);
    if (!j) return err(404, "not_found", "job no encontrado");
    return ok(job_json(*j));
}

ApiResponse Api::h_job_cancel(uint64_t id) {
    const auto j = jobs_.get(id);
    if (!j) return err(404, "not_found", "job no encontrado");
    const bool did = jobs_.request_cancel(id);
    return ok(json::JsonValue::make_object({
        {"cancelled", json::JsonValue::make_bool(did)},
        {"job", job_json(*j)},
    }));
}

// --- scans ---------------------------------------------------------------------

ApiResponse Api::h_scan_first(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    const json::JsonValue* tv = b.get("type");
    if (!tv || !tv->is_string())
        return err(400, "scan_invalid", "falta el campo type");
    const std::string type_s = *tv->as_string();
    // Tipos dinamicos: string / bytes|pattern|aob (parse_type no los conoce).
    const DataType dyn = dynamic_type_token(type_s);
    DataType type = dyn;
    if (!type_is_dynamic(type)) {
        if (!parse_type(type_s, type))
            return err(400, "scan_invalid", "tipo invalido: " + type_s);
    }

    const json::JsonValue* val = b.get("value");

    if (type_is_dynamic(type)) {
        if (!val || !val->is_string())
            return err(400, "scan_invalid",
                       "value debe ser un string para " +
                           std::string(type_name(type)));
        std::string perr;
        BytePattern pat = build_dynamic_pattern(type, *val->as_string(), perr);
        if (!pat.valid)
            return err(400, "scan_invalid",
                       perr.empty() ? pat.error : perr);
        const DynamicScanSpec spec = make_dynamic_spec(type, std::move(pat));
        if (!app_.has_pid())
            return err(409, "no_process", "No hay un proceso seleccionado");
        JobTask task = [spec](Application& a, Job& j) -> JobRunResult {
            return from_scan(a.first_scan_dynamic(spec, j.cancel_flag(),
                                                  j.make_progress_callback()));
        };
        return start_job("first", std::move(task), r), r;
    }

    std::optional<Value> target;
    if (!val || val->is_null()) {
        // Escaneo unknown: sin valor de comparacion.
    } else {
        if (!val->is_string())
            return err(400, "scan_invalid",
                       "value debe ser un string o null");
        Value v;
        if (!parse_value(*val->as_string(), type, v))
            return err(400, "scan_invalid",
                       "valor invalido: " + *val->as_string() + " (tipo " +
                           type_name(type) + ")");
        target = v;
    }
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    JobTask task = [type, target](Application& a, Job& j) -> JobRunResult {
        return from_scan(a.first_scan(type, target, j.cancel_flag(),
                                      j.make_progress_callback()));
    };
    return start_job("first", std::move(task), r), r;
}

ApiResponse Api::h_scan_next(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    const json::JsonValue* fv = b.get("filter");
    const std::string filter_s =
        (fv && fv->is_string()) ? *fv->as_string() : "";
    if (filter_s.empty())
        return err(400, "scan_invalid", "falta el campo filter");

    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    Scanner& sc = app_.session().scanner();
    if (!sc.has_results())
        return err(400, "scan_invalid",
                   "No hay resultados previos; usa 'first' primero.");

    // Escaneo dinamico (string/bytes): changed/unchanged o patron nuevo.
    if (sc.is_dynamic()) {
        Filter filter;
        if (filter_s == "changed")
            filter = Filter::CHANGED;
        else if (filter_s == "unchanged")
            filter = Filter::UNCHANGED;
        else if (filter_s == "exact")
            filter = Filter::EXACT;
        else
            return err(400, "scan_invalid",
                       "los filtros numericos no aplican a string/bytes; usa "
                       "changed, unchanged o un patron nuevo");
        std::optional<DynamicScanSpec> newspec;
        if (filter == Filter::EXACT) {
            const json::JsonValue* val = b.get("value");
            if (!val || !val->is_string())
                return err(400, "scan_invalid",
                           "value requerido para exact");
            // Hereda el tipo del scan dinamico previo (dyn_spec_), igual que
            // el camino numerico hereda sc.first_type(). El type del body es
            // opcional: acepta los tokens dinamicos de la CLI (string/bytes/
            // pattern/aob) o un tipo numerico no dinamico -> rechazado.
            DataType dt = sc.dyn_spec().type;
            const json::JsonValue* typ = b.get("type");
            if (typ && typ->is_string()) {
                const std::string ts = *typ->as_string();
                const DataType tok = dynamic_type_token(ts);
                if (type_is_dynamic(tok))
                    dt = tok;
                else if (!parse_type(ts, dt) || !type_is_dynamic(dt))
                    return err(400, "scan_invalid",
                               "tipo invalido para escaneo dinamico");
            }
            std::string perr;
            BytePattern pat = build_dynamic_pattern(dt, *val->as_string(), perr);
            if (!pat.valid)
                return err(400, "scan_invalid",
                           perr.empty() ? pat.error : perr);
            newspec = make_dynamic_spec(dt, std::move(pat));
        }
        JobTask task = [filter, newspec](Application& a, Job& j) -> JobRunResult {
            return from_scan(a.next_scan_dynamic(filter, newspec,
                                                 j.cancel_flag(),
                                                 j.make_progress_callback()));
        };
        return start_job("next", std::move(task), r), r;
    }

    // Numerico: hereda el tipo del first; tipo distinto -> rechazado.
    Filter filter;
    if (!parse_filter(filter_s, filter))
        return err(400, "scan_invalid", "filtro desconocido: " + filter_s);
    DataType type = sc.first_type();
    const json::JsonValue* typ = b.get("type");
    if (typ && typ->is_string()) {
        if (!parse_type(*typ->as_string(), type))
            return err(400, "scan_invalid", "tipo invalido: " + *typ->as_string());
        const std::string mismatch =
            next_type_mismatch_message(sc.first_type(), type);
        if (!mismatch.empty())
            return err(400, "scan_invalid", mismatch);
    }
    std::optional<Value> target;
    const bool need_val = filter == Filter::EXACT || filter == Filter::GREATER ||
                          filter == Filter::LESS || filter == Filter::GE ||
                          filter == Filter::LE || filter == Filter::NE;
    if (need_val) {
        const json::JsonValue* val = b.get("value");
        if (!val || !val->is_string())
            return err(400, "scan_invalid",
                       "value requerido para el filtro " + filter_s);
        Value v;
        if (!parse_value(*val->as_string(), type, v))
            return err(400, "scan_invalid",
                       "valor invalido: " + *val->as_string() + " (tipo " +
                           type_name(type) + ")");
        target = v;
    }
    JobTask task = [type, filter, target](Application& a, Job& j) -> JobRunResult {
        return from_scan(a.next_scan(type, filter, target, j.cancel_flag(),
                                     j.make_progress_callback()));
    };
    return start_job("next", std::move(task), r), r;
}

ApiResponse Api::h_pattern(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    const json::JsonValue* pv = b.get("pattern");
    if (!pv || !pv->is_string())
        return err(400, "scan_invalid", "falta el campo pattern");
    BytePattern pat;
    if (!parse_pattern(*pv->as_string(), pat))
        return err(400, "scan_invalid", pat.error);
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");

    JobTask task = [this, pat](Application& a, Job& j) -> JobRunResult {
        PatternOutcome o = a.pattern_scan(pat, j.cancel_flag(),
                                          j.make_progress_callback());
        JobRunResult r;
        r.ok = o.ok;
        r.cancelled = o.cancelled;
        r.count = o.hits.size();
        r.error = o.error;
        if (o.ok) { // guardar para GET /api/pattern/results
            std::lock_guard<std::mutex> lk(pat_mtx_);
            last_pattern_ = PatternScanResult{std::move(o.hits), o.truncated,
                                              false};
        }
        return r;
    };
    return start_job("pattern", std::move(task), r), r;
}

// --- resultados ----------------------------------------------------------------

ApiResponse Api::h_results(uint64_t offset, uint64_t limit) {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const Scanner& sc = app_.session().scanner();
    json::JsonValue::Array rows;

    if (sc.is_dynamic()) {
        const auto& dyn = sc.dynamic_results();
        const size_t total = dyn.size();
        const size_t begin = std::min<size_t>(offset, total);
        const size_t end = std::min<size_t>(total, begin + std::min<uint64_t>(limit, 1000ull));
        const size_t len = sc.dyn_spec().length();
        for (size_t i = begin; i < end; ++i) {
            rows.push_back(json::JsonValue::make_object({
                {"address", json::JsonValue::make_string(hex_addr(dyn[i].addr))},
                {"type", json::JsonValue::make_string(
                             type_name(sc.dyn_spec().type))},
                {"length", json::JsonValue::make_int((int64_t)len)},
            }));
        }
        return ok(json::JsonValue::make_object({
            {"total", json::JsonValue::make_string(std::to_string(total))},
            {"offset", json::JsonValue::make_int((int64_t)offset)},
            {"rows", json::JsonValue::make_array(std::move(rows))},
        }));
    }

    const auto& res = sc.results();
    const size_t total = res.size();
    const size_t begin = std::min<size_t>(offset, total);
    const size_t end = std::min<size_t>(total, begin + std::min<uint64_t>(limit, 1000ull));
    const DataType t = sc.first_type();
    for (size_t i = begin; i < end; ++i) {
        rows.push_back(json::JsonValue::make_object({
            {"address", json::JsonValue::make_string(hex_addr(res[i].addr))},
            {"type", json::JsonValue::make_string(type_name(t))},
            {"value", json::JsonValue::make_string(value_to_string(res[i].prev, t))},
        }));
    }
    return ok(json::JsonValue::make_object({
        {"total", json::JsonValue::make_string(std::to_string(total))},
        {"offset", json::JsonValue::make_int((int64_t)offset)},
        {"rows", json::JsonValue::make_array(std::move(rows))},
    }));
}

ApiResponse Api::h_pattern_results(uint64_t offset, uint64_t limit) {
    std::unique_lock<std::mutex> lock(pat_mtx_);
    if (!last_pattern_) {
        return ok(json::JsonValue::make_object({
            {"total", json::JsonValue::make_string("0")},
            {"offset", json::JsonValue::make_int((int64_t)offset)},
            {"truncated", json::JsonValue::make_bool(false)},
            {"rows", json::JsonValue::make_array({})},
        }));
    }
    const size_t total = last_pattern_->hits.size();
    const size_t begin = std::min<size_t>(offset, total);
    const size_t end =
        std::min<size_t>(total, begin + std::min<uint64_t>(limit, 1000ull));
    json::JsonValue::Array rows;
    for (size_t i = begin; i < end; ++i)
        rows.push_back(json::JsonValue::make_object(
            {{"address", json::JsonValue::make_string(
                             hex_addr(last_pattern_->hits[i]))}}));
    return ok(json::JsonValue::make_object({
        {"total", json::JsonValue::make_string(std::to_string(total))},
        {"offset", json::JsonValue::make_int((int64_t)offset)},
        {"truncated", json::JsonValue::make_bool(last_pattern_->truncated)},
        {"rows", json::JsonValue::make_array(std::move(rows))},
    }));
}

// --- memoria / escritura ---------------------------------------------------------

ApiResponse Api::h_memory(uint64_t addr, size_t len) {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    len = std::min<size_t>(std::max<size_t>(len, 1), 4096);

    const ReadBytesOutcome o = app_.read_bytes(addr, len);
    if (!o.ok) return err(500, "failed", o.error);
    if (o.got <= 0)
        return err(400, "failed", "no se pudo leer la direccion " + hex_addr(addr));

    std::string hex, ascii;
    for (size_t i = 0; i < o.bytes.size(); ++i) {
        char h[4];
        snprintf(h, sizeof h, "%02x", o.bytes[i]);
        hex += h;
        ascii += (o.bytes[i] >= 0x20 && o.bytes[i] < 0x7f) ? (char)o.bytes[i]
                                                           : '.';
    }

    json::JsonValue region = json::JsonValue::make_null();
    const auto regions = parse_maps(app_.pid());
    if (const auto r = region_at(regions, addr)) {
        region = json::JsonValue::make_object({
            {"start", json::JsonValue::make_string(hex_addr(r->start))},
            {"end", json::JsonValue::make_string(hex_addr(r->end))},
            {"perms", json::JsonValue::make_string(r->perms)},
            {"path", json::JsonValue::make_string(r->path)},
        });
    }
    return ok(json::JsonValue::make_object({
        {"address", json::JsonValue::make_string(hex_addr(addr))},
        {"hex", json::JsonValue::make_string(hex)},
        {"ascii", json::JsonValue::make_string(ascii)},
        {"region", std::move(region)},
    }));
}

ApiResponse Api::h_write(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");

    const json::JsonValue* av = b.get("address");
    const json::JsonValue* tv = b.get("type");
    const json::JsonValue* vv = b.get("value");
    if (!av || !av->is_string() || !tv || !tv->is_string() || !vv ||
        !vv->is_string())
        return err(400, "bad_request",
                   "faltan address/type/value (strings)");
    uint64_t addr = 0;
    if (!parse_addr(*av->as_string(), addr))
        return err(400, "bad_request", "address invalido");
    DataType type;
    if (!parse_type(*tv->as_string(), type))
        return err(400, "scan_invalid", "tipo invalido: " + *tv->as_string());
    Value v;
    if (!parse_value(*vv->as_string(), type, v))
        return err(400, "scan_invalid",
                   "valor invalido: " + *vv->as_string() + " (tipo " +
                       type_name(type) + ")");

    const WriteOutcome o = app_.write(addr, type, v);
    if (!o.ok) return err(400, "failed", o.error);
    json::JsonValue::Object obj = {
        {"address", json::JsonValue::make_string(hex_addr(o.address))},
        {"type", json::JsonValue::make_string(type_name(o.type))},
        {"verified", json::JsonValue::make_bool(o.verified)},
    };
    if (o.had_old)
        obj.push_back({"old_value",
                       json::JsonValue::make_string(value_to_string(o.old_value, o.type))});
    if (o.wrote)
        obj.push_back({"new_value",
                       json::JsonValue::make_string(value_to_string(o.new_value, o.type))});
    return ok(json::JsonValue::make_object(std::move(obj)));
}

// --- tabla -----------------------------------------------------------------------

ApiResponse Api::h_table() {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    json::JsonValue::Array items;
    const size_t n = app_.table_size();
    for (size_t i = 0; i < n; ++i) {
        const AddressEntry* e = app_.entry(i);
        if (!e) continue;
        json::JsonValue::Object obj = {
            {"index", json::JsonValue::make_int((int64_t)i)},
            {"address", json::JsonValue::make_string(hex_addr(e->address))},
            {"type", json::JsonValue::make_string(type_name(e->type))},
            {"description", json::JsonValue::make_string(e->description)},
            {"enabled", json::JsonValue::make_bool(e->enabled)},
            {"stale", json::JsonValue::make_bool(e->stale)},
        };
        if (e->ptr)
            obj.push_back({"pointer", ptr_ref_json(*e->ptr)});
        items.push_back(json::JsonValue::make_object(std::move(obj)));
    }
    return ok(json::JsonValue::make_object({
        {"entries", json::JsonValue::make_array(std::move(items))},
        {"count", json::JsonValue::make_int((int64_t)n)},
    }));
}

ApiResponse Api::h_table_add(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* av = b.get("address");
    const json::JsonValue* tv = b.get("type");
    if (!av || !av->is_string() || !tv || !tv->is_string())
        return err(400, "bad_request", "faltan address/type");
    uint64_t addr = 0;
    if (!parse_addr(*av->as_string(), addr))
        return err(400, "bad_request", "address invalido");
    DataType type;
    if (!parse_type(*tv->as_string(), type))
        return err(400, "scan_invalid", "tipo invalido: " + *tv->as_string());
    std::string desc;
    const json::JsonValue* dv = b.get("description");
    if (dv && dv->is_string()) desc = *dv->as_string();
    const size_t idx = app_.add_entry(addr, type, desc);
    return ok(json::JsonValue::make_object({
        {"index", json::JsonValue::make_int((int64_t)idx)},
        {"address", json::JsonValue::make_string(hex_addr(addr))},
        {"type", json::JsonValue::make_string(type_name(type))},
    }));
}

ApiResponse Api::h_table_add_result(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* iv = b.get("result_index");
    if (!iv)
        return err(400, "bad_request", "falta result_index");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    const Scanner& sc = app_.session().scanner();
    if (idx >= sc.results().size())
        return err(400, "bad_request", "result index fuera de rango");
    std::string desc;
    const json::JsonValue* dv = b.get("description");
    if (dv && dv->is_string()) desc = *dv->as_string();
    const size_t ti = app_.add_result_entry((size_t)idx, desc);
    return ok(json::JsonValue::make_object({
        {"index", json::JsonValue::make_int((int64_t)ti)},
    }));
}

ApiResponse Api::h_table_remove(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* iv = b.get("index");
    if (!iv)
        return err(400, "bad_request", "falta index");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    if (!app_.remove_entry((size_t)idx))
        return err(404, "not_found", "no existe la entrada");
    return ok(json::JsonValue::make_object({{"removed", json::JsonValue::make_bool(true)}}));
}

ApiResponse Api::h_table_clear() {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    app_.clear_entries();
    return ok(json::JsonValue::make_object({{"count", json::JsonValue::make_int(0)}}));
}

ApiResponse Api::h_table_toggle(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* iv = b.get("index");
    if (!iv)
        return err(400, "bad_request", "falta index");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    if (!app_.toggle_entry((size_t)idx))
        return err(404, "not_found", "no existe la entrada");
    const AddressEntry* e = app_.entry((size_t)idx);
    return ok(json::JsonValue::make_object({
        {"index", json::JsonValue::make_int((int64_t)idx)},
        {"enabled", json::JsonValue::make_bool(e && e->enabled)},
    }));
}

ApiResponse Api::h_table_read(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    const json::JsonValue* iv = b.get("index");
    if (!iv)
        return err(400, "bad_request", "falta index");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    const EntryReadOutcome o = app_.read_entry((size_t)idx);
    if (!o.ok) return err(400, "failed", o.error);
    json::JsonValue::Object obj = {
        {"index", json::JsonValue::make_int((int64_t)o.index)},
        {"address", json::JsonValue::make_string(hex_addr(o.address))},
        {"type", json::JsonValue::make_string(type_name(o.type))},
        {"stale", json::JsonValue::make_bool(o.was_stale)},
    };
    if (o.have_value)
        obj.push_back({"value", json::JsonValue::make_string(
                                    value_to_string(o.value, o.type))});
    return ok(json::JsonValue::make_object(std::move(obj)));
}

ApiResponse Api::h_table_set(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    const json::JsonValue* iv = b.get("index");
    const json::JsonValue* vv = b.get("value");
    if (!iv || !vv || !vv->is_string())
        return err(400, "bad_request", "faltan index/value");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    const AddressEntry* e = app_.entry((size_t)idx);
    if (!e) return err(404, "not_found", "no existe la entrada");
    Value v;
    if (!parse_value(*vv->as_string(), e->type, v))
        return err(400, "scan_invalid",
                   "valor invalido: " + *vv->as_string() + " (tipo " +
                       type_name(e->type) + ")");
    const WriteOutcome o = app_.write_entry((size_t)idx, v);
    if (!o.ok) return err(400, "failed", o.error);
    json::JsonValue::Object obj = {
        {"index", json::JsonValue::make_int((int64_t)idx)},
        {"address", json::JsonValue::make_string(hex_addr(o.address))},
        {"type", json::JsonValue::make_string(type_name(o.type))},
        {"verified", json::JsonValue::make_bool(o.verified)},
    };
    if (o.had_old)
        obj.push_back({"old_value",
                       json::JsonValue::make_string(value_to_string(o.old_value, o.type))});
    if (o.wrote)
        obj.push_back({"new_value",
                       json::JsonValue::make_string(value_to_string(o.new_value, o.type))});
    return ok(json::JsonValue::make_object(std::move(obj)));
}

// Directorio donde se guardan las tablas de la API (relativo al cwd).
namespace {
const char* kTablesDir = "tables";
}

ApiResponse Api::h_table_save(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* nv = b.get("name");
    if (!nv || !nv->is_string() || !valid_table_name(*nv->as_string()))
        return err(403, "forbidden", "nombre de tabla invalido");
    const std::string name = *nv->as_string();
    ::mkdir(kTablesDir, 0755); // EEXIST se ignora
    const std::string path = std::string(kTablesDir) + "/" + name;
    std::string serr;
    if (!app_.save_table(path, serr))
        return err(500, "failed", serr);
    return ok(json::JsonValue::make_object(
        {{"path", json::JsonValue::make_string(path)}}));
}

ApiResponse Api::h_table_load(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    const json::JsonValue* nv = b.get("name");
    if (!nv || !nv->is_string() || !valid_table_name(*nv->as_string()))
        return err(403, "forbidden", "nombre de tabla invalido");
    const std::string name = *nv->as_string();
    const std::string path = std::string(kTablesDir) + "/" + name;
    std::string serr;
    if (!app_.load_table(path, serr))
        return err(400, "failed", serr);
    return ok(json::JsonValue::make_object({
        {"path", json::JsonValue::make_string(path)},
        {"count", json::JsonValue::make_int((int64_t)app_.table_size())},
    }));
}

// --- pointer -------------------------------------------------------------------

ApiResponse Api::h_pointer_scan(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    const json::JsonValue* tv = b.get("target");
    if (!tv || !tv->is_string())
        return err(400, "scan_invalid", "falta el campo target");
    uint64_t target = 0;
    if (!parse_addr(*tv->as_string(), target))
        return err(400, "scan_invalid", "target invalido");

    // Reutiliza las reglas/limites exactos de la CLI (depth 1..7,
    // MAX_OFFSET_SHIFTS, module-only, code, type=T).
    CommandArgs args;
    args.push_back(hex_addr(target));
    const json::JsonValue* dv = b.get("depth");
    if (dv && dv->type() == json::JsonValue::Type::Int)
        args.push_back("depth=" + std::to_string(dv->as_int()));
    const json::JsonValue* mo = b.get("max_offset");
    if (mo && mo->is_string())
        args.push_back("max_offset=" + *mo->as_string());
    else if (mo && mo->type() == json::JsonValue::Type::Int)
        args.push_back("max_offset=" + std::to_string(mo->as_int()));
    const json::JsonValue* os = b.get("offset_step");
    if (os && os->type() == json::JsonValue::Type::Int)
        args.push_back("offset_step=" + std::to_string(os->as_int()));
    const json::JsonValue* ml = b.get("module_only");
    if (ml && ml->as_bool(false)) args.push_back("module-only");
    const json::JsonValue* cd = b.get("code");
    if (cd && cd->as_bool(false)) args.push_back("code");
    const json::JsonValue* tp = b.get("type");
    if (tp && tp->is_string()) args.push_back("type=" + *tp->as_string());

    const PointerScanArgs pa = parse_pointer_scan_args(args);
    if (!pa.error.empty())
        return err(400, "scan_invalid", pa.error);
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");

    PointerScanInput in;
    in.opts.target = pa.target;
    in.opts.max_depth = pa.depth;
    in.opts.include_code = pa.include_code;
    in.opts.max_offset = pa.max_offset;
    in.opts.offset_step = pa.offset_step;
    in.value_type = pa.value_type.value_or(app_.session().scan_type());
    in.module_only = pa.module_only;

    JobTask task = [in](Application& a, Job& j) -> JobRunResult {
        const PointerScanOutcome o =
            a.pointer_scan(in, j.cancel_flag(), j.make_progress_callback());
        JobRunResult r;
        r.ok = o.ok;
        r.cancelled = o.cancelled;
        r.error = o.error;
        if (o.ok) r.count = o.result.chains.size();
        return r;
    };
    return start_job("pointer", std::move(task), r), r;
}

ApiResponse Api::h_pointer_results(uint64_t offset, uint64_t limit) {
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.session().pointer_result()) {
        return ok(json::JsonValue::make_object({
            {"total", json::JsonValue::make_string("0")},
            {"offset", json::JsonValue::make_int((int64_t)offset)},
            {"rows", json::JsonValue::make_array({})},
        }));
    }
    const PointerScanResult& res = *app_.session().pointer_result();
    std::vector<Region> regions;
    if (app_.has_pid()) regions = parse_maps(app_.pid());

    const size_t total = res.chains.size();
    const size_t begin = std::min<size_t>(offset, total);
    const size_t end = std::min<size_t>(total, begin + std::min<uint64_t>(limit, 200ull));
    json::JsonValue::Array rows;
    for (size_t i = begin; i < end; ++i) {
        const PointerChain& c = res.chains[i];
        bool persistent = false;
        std::string module;
        std::string root_offset;
        if (!c.nodes.empty()) {
            const PointerBase pb = make_base_from_address(regions, c.nodes[0]);
            persistent = pb.kind == PointerBaseKind::MODULE;
            if (persistent) {
                module = pb.module;
                root_offset = hex_addr(pb.offset);
            } else {
                root_offset = hex_addr(pb.address);
            }
        }
        json::JsonValue::Array nodes;
        for (uint64_t n : c.nodes)
            nodes.push_back(json::JsonValue::make_string(hex_addr(n)));
        json::JsonValue::Array offs;
        for (uint64_t o : c.offsets)
            offs.push_back(json::JsonValue::make_string(hex_addr(o)));
        rows.push_back(json::JsonValue::make_object({
            {"index", json::JsonValue::make_int((int64_t)i)},
            {"depth", json::JsonValue::make_int(c.depth)},
            {"nodes", json::JsonValue::make_array(std::move(nodes))},
            {"offsets", json::JsonValue::make_array(std::move(offs))},
            {"kind", json::JsonValue::make_string(persistent ? "MODULE"
                                                             : "ABSOLUTE")},
            {"persistent", json::JsonValue::make_bool(persistent)},
            {"module", json::JsonValue::make_string(module)},
            {"root_offset", json::JsonValue::make_string(root_offset)},
            {"value_type",
             json::JsonValue::make_string(type_name(res.value_type))},
        }));
    }
    return ok(json::JsonValue::make_object({
        {"total", json::JsonValue::make_string(std::to_string(total))},
        {"offset", json::JsonValue::make_int((int64_t)offset)},
        {"rows", json::JsonValue::make_array(std::move(rows))},
    }));
}

ApiResponse Api::h_pointer_add(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.session().pointer_result())
        return err(400, "failed", "No hay un pointer scan previo");
    const json::JsonValue* cv = b.get("chain_index");
    if (!cv)
        return err(400, "bad_request", "falta chain_index");
    const uint64_t ci =
        cv->is_string() ? strtoull(cv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)cv->as_int();
    if (ci >= app_.session().pointer_result()->chains.size())
        return err(400, "bad_request", "chain index fuera de rango");
    std::string desc;
    const json::JsonValue* dv = b.get("description");
    if (dv && dv->is_string()) desc = *dv->as_string();
    const AddChainOutcome o = app_.add_pointer_chain((size_t)ci, desc);
    if (!o.ok) return err(400, "failed", o.error);
    return ok(json::JsonValue::make_object({
        {"table_index", json::JsonValue::make_int((int64_t)o.table_index)},
        {"pointer", ptr_ref_json(o.ref)},
    }));
}

ApiResponse Api::h_pointer_resolve(const HttpRequest& req) {
    json::JsonValue b;
    ApiResponse r;
    if (!body_json(req, b, r)) return r;
    std::unique_lock<std::mutex> lock(runner_.session_mutex(),
                                      std::try_to_lock);
    if (!lock.owns_lock()) return busy();
    if (!app_.has_pid())
        return err(409, "no_process", "No hay un proceso seleccionado");
    const json::JsonValue* iv = b.get("index");
    if (!iv)
        return err(400, "bad_request", "falta index");
    const uint64_t idx =
        iv->is_string() ? strtoull(iv->as_string()->c_str(), nullptr, 10)
                        : (uint64_t)iv->as_int();
    const ResolveEntryOutcome o = app_.resolve_entry((size_t)idx);
    if (!o.ok) return err(400, "failed", o.error);
    return ok(json::JsonValue::make_object({
        {"index", json::JsonValue::make_int((int64_t)idx)},
        {"address", json::JsonValue::make_string(hex_addr(o.address))},
        {"value", json::JsonValue::make_string(value_to_string(o.value, o.type))},
        {"type", json::JsonValue::make_string(type_name(o.type))},
    }));
}

} // namespace web
} // namespace mt
