// command.cpp - Capa de comandos de MemoryTool (frontend CLI).
//
// Los handlers reproducen el comportamiento de la antigua CLI monolitica
// (main.cpp): misma sintaxis, mismos mensajes y mismos resultados. La LOGICA
// de cada operacion vive en Application (application.cpp): aqui solo se
// parsea texto, se llama a Application y se formatea/imprime el resultado.
//
//   CLI (este archivo) -> Application -> Session -> Core
#include "command.h"

#include "application.h"
#include "session.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unistd.h>

#include "memory.h"
#include "pointer.h"
#include "pointer_resolver.h"
#include "process.h"
#include "types.h"

namespace mt {

// ---------------------------------------------------------------------------
// Utilidades de la CLI (parsing de texto y formateo; sin logica de core)

static std::vector<std::string> split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string t;
    while (iss >> t) out.push_back(t);
    return out;
}

// Valor legible (con sufijo hex) para los mensajes de la CLI.
static std::string display_value(Value v, DataType t) {
    std::string s = value_to_string(v, t);
    // PTR ya se muestra como 0x... (una direccion); no anadir el sufijo hex.
    if (!type_is_float(t) && t != DataType::PTR) {
        char h[32];
        snprintf(h, sizeof h, "  (0x%0*llx)", (int)(type_size(t) * 2),
                 (unsigned long long)v.bits);
        s += h;
    }
    return s;
}

static void hexdump(uint64_t addr, const uint8_t* p, size_t n) {
    for (size_t off = 0; off < n; off += 16) {
        printf("%016llx  ", (unsigned long long)(addr + off));
        const size_t row = std::min<size_t>(16, n - off);
        for (size_t i = 0; i < 16; ++i) {
            if (i < row)
                printf("%02x ", p[off + i]);
            else
                printf("   ");
            if (i == 7) printf(" ");
        }
        printf(" |");
        for (size_t i = 0; i < row; ++i) {
            uint8_t c = p[off + i];
            printf("%c", (c >= 0x20 && c < 0x7f) ? (char)c : '.');
        }
        printf("|\n");
    }
}

// Mensaje comun cuando un comando necesita proceso objetivo. Devuelve true
// si la sesion tiene proceso seleccionado.
static bool has_target(const Session& s) {
    if (s.has_pid()) return true;
    printf("Primero selecciona un proceso (attach <pid>).\n");
    return false;
}

// Parsea un indice decimal (para 'results'/'table').
static bool parse_index(const std::string& s, size_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = (size_t)v;
    return true;
}

// Une tokens como descripcion. Si el resultado va entre comillas dobles
// ("descripcion con espacios"), las quita: la CLI divide por espacios, asi
// que las comillas llegan como parte del primer/ultimo token.
static std::string join_desc(const CommandArgs& args, size_t from) {
    std::string d;
    for (size_t i = from; i < args.size(); ++i) {
        if (i > from) d += ' ';
        d += args[i];
    }
    if (d.size() >= 2 && d.front() == '"' && d.back() == '"') {
        d = d.substr(1, d.size() - 2);
    }
    return d;
}

// --- Valores dinamicos (string / bytes) -----------------------------------

// ¿El ultimo token es un nombre de tipo dinamico? Devuelve el DataType o
// I32 (no dinamico). 'string' busca un texto; 'bytes'/'pattern'/'aob' un
// patron hex con wildcards (coherente con el comando pattern/aob).
static DataType dynamic_type_token(const std::string& tok) {
    if (tok == "string") return DataType::STRING;
    if (tok == "bytes" || tok == "pattern" || tok == "aob") return DataType::BYTES;
    return DataType::I32;
}

// Une tokens [0, n) con espacios y quita comillas envolventes si las hay
// (la CLI divide por espacios, asi que "hola memorytool" llega en varios
// tokens con las comillas en el primero/ultimo).
static std::string join_strip_quotes(const CommandArgs& args, size_t n) {
    std::string t;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) t += ' ';
        t += args[i];
    }
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = t.substr(1, t.size() - 2);
    return t;
}

// Convierte el texto del valor a un BytePattern segun el tipo dinamico
// (string -> bytes exactos; bytes -> parse_pattern con wildcards).
static BytePattern build_dynamic_pattern(DataType type, const std::string& text,
                                         std::string& err) {
    if (type == DataType::STRING) return pattern_from_text(text, err);
    BytePattern pat;
    if (!parse_pattern(text, pat)) err = pat.error;
    return pat;
}

// Texto legible del patron de un escaneo dinamico (para 'results').
static std::string dynamic_pattern_text(const DynamicScanSpec& spec) {
    std::string s;
    if (spec.type == DataType::STRING) {
        s = "\"";
        char h[8];
        for (uint8_t b : spec.pattern.bytes) {
            if (b >= 0x20 && b < 0x7f) {
                s += (char)b;
            } else {
                snprintf(h, sizeof h, "\\x%02x", b);
                s += h;
            }
        }
        s += "\"";
        return s;
    }
    char h[8];
    for (size_t i = 0; i < spec.pattern.bytes.size(); ++i) {
        if (i > 0) s += ' ';
        if (spec.pattern.mask[i]) {
            snprintf(h, sizeof h, "%02X", spec.pattern.bytes[i]);
            s += h;
        } else {
            s += "??";
        }
    }
    return "[" + s + "]";
}

// Mensajes comunes de un ScanOutcome (AVISOS de limites).
static void print_scan_warnings(const ScanOutcome& o) {
    if (o.truncated)
        printf("AVISO: se alcanzo el limite de candidatos; el resultado esta truncado.\n");
    if (o.warned)
        printf("AVISO: la lista de candidatos es muy grande; el escaneo puede consumir mucha RAM.\n");
}

// ---------------------------------------------------------------------------
// Handlers

static CommandResult cmd_quit(const CommandArgs&, Session&) {
    return {true, true}; // termina el REPL
}

static CommandResult cmd_help(const CommandArgs&, Session&) {
    printf("Comandos:\n");
    for (const auto& c : commands())
        if (c.usage) printf("  %s\n", c.usage);
    printf("Tipos: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 ptr (alias: int, float, double, byte, pointer...)\n");
    return {};
}

static CommandResult cmd_list(const CommandArgs&, Session&) {
    auto procs = list_processes();
    if (procs.empty()) {
        printf("No se pudo listar /proc\n");
        return {};
    }
    printf("%-7s %-12s %-6s %-9s %s\n", "PID", "USUARIO", "EST", "RSS(KB)", "NOMBRE");
    for (const auto& p : procs) {
        printf("%-7d %-12s %-6c %-9ld %s%s\n", p.pid, p.user.c_str(), p.state,
               p.rss_kb, p.name.c_str(), p.accessible ? "" : "  [no accesible]");
    }
    printf("\nSolo los procesos del mismo usuario (UID %d) que concedan ptrace son accesibles.\n",
           (int)geteuid());
    return {};
}

static CommandResult cmd_attach(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: attach <pid|nombre>\n");
        return {};
    }
    auto target = resolve_target(args[0]);
    if (!target) return {};
    Application app(s);
    AttachOutcome o = app.attach(*target);
    if (!o.ok) {
        printf("No se pudo acceder al proceso %d: %s\n", *target, o.error.c_str());
        return {};
    }
    if (o.switched)
        printf("Resultados anteriores descartados (cambio de proceso).\n");
    printf("Proceso objetivo: %d\n", *target);
    return {};
}

static CommandResult cmd_detach(const CommandArgs&, Session& s) {
    Application app(s);
    app.detach();
    printf("Proceso objetivo eliminado.\n");
    return {};
}

static CommandResult cmd_maps(const CommandArgs& args, Session& s) {
    if (!args.empty()) {
        auto r = resolve_target(args[0]);
        if (!r) return {};
        print_maps(*r);
        return {};
    }
    if (!has_target(s)) return {};
    print_maps(s.pid());
    return {};
}

static CommandResult cmd_first(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.empty()) {
        printf("Uso: first <valor> [tipo] | first unknown [tipo]\n"
               "     first \"<texto>\" string | first <hex...> bytes|pattern\n");
        return {};
    }
    Application app(s);

    // first unknown [tipo]: escaneo numerico de valor desconocido (nunca
    // dinamico, aunque el ultimo token parezca un tipo).
    if (args[0] == "unknown") {
        DataType type = DataType::I32;
        if (args.size() >= 2 && parse_type(args[1], type)) {}
        s.set_scan_type(type);
        ScanOutcome o = app.first_scan(type, std::nullopt);
        if (!o.ok) {
            printf("Error: %s\n", o.error.c_str());
            return {};
        }
        printf("Escaneo 'unknown' completado (%zu posiciones legibles).\n",
               s.scanner().count());
        print_scan_warnings(o);
        return {};
    }

    // Valores dinamicos: first "<texto>" string | first <hex...> bytes|pattern.
    const DataType dynt = dynamic_type_token(args.back());
    if (type_is_dynamic(dynt)) {
        const std::string text = join_strip_quotes(args, args.size() - 1);
        std::string err;
        BytePattern pat = build_dynamic_pattern(dynt, text, err);
        if (!pat.valid) {
            printf("Patron invalido (%s): %s\n", type_name(dynt), err.c_str());
            return {};
        }
        const DynamicScanSpec spec = make_dynamic_spec(dynt, std::move(pat));
        s.set_scan_type(dynt);
        ScanOutcome o = app.first_scan_dynamic(spec);
        if (!o.ok) {
            printf("Error: %s\n", o.error.c_str());
            return {};
        }
        printf("First Scan: %zu coincidencias (%s, len %zu)\n",
               s.scanner().count(), type_name(dynt), spec.length());
        print_scan_warnings(o);
        return {};
    }

    // Camino numerico (sin cambios): first <valor> [tipo].
    DataType type = DataType::I32;
    if (args.size() >= 2 && parse_type(args.back(), type)) {}
    Value v;
    if (!parse_value(args[0], type, v)) {
        printf("Valor invalido: %s (tipo %s)\n", args[0].c_str(), type_name(type));
        return {};
    }
    s.set_scan_type(type);
    ScanOutcome o = app.first_scan(type, v);
    if (!o.ok) {
        printf("Error: %s\n", o.error.c_str());
        return {};
    }
    printf("First Scan: %zu coincidencias (%s = %s)\n", s.scanner().count(),
           type_name(type), display_value(v, type).c_str());
    print_scan_warnings(o);
    return {};
}

static CommandResult cmd_next(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (!s.scanner().has_results()) {
        printf("No hay resultados previos; usa 'first' primero.\n");
        return {};
    }
    if (args.empty()) {
        printf("Uso: next <valor> [tipo] | next changed|... | next <op> <valor> [tipo]\n"
               "     next \"<texto>\" string | next <hex...> bytes (escaneo dinamico)\n");
        return {};
    }
    Application app(s);

    // Refinamiento de un escaneo dinamico (string/bytes): changed, unchanged
    // o un patron nuevo (coincidencia exacta). Los filtros numericos no
    // tienen sentido para longitud variable.
    if (s.scanner().is_dynamic()) {
        Filter filter = Filter::EXACT;
        std::optional<DynamicScanSpec> newspec;
        const std::string& tok = args[0];
        if (tok == "changed") {
            filter = Filter::CHANGED;
        } else if (tok == "unchanged") {
            filter = Filter::UNCHANGED;
        } else if (tok == "increased" || tok == "increase" ||
                   tok == "decreased" || tok == "decrease" ||
                   tok == ">" || tok == "<" || tok == ">=" ||
                   tok == "<=" || tok == "!=" || tok == "=") {
            printf("Los filtros numericos (> < >= <= != increased/decreased) no aplican "
                   "a string/bytes; usa 'next changed'/'next unchanged' o un patron nuevo.\n");
            return {};
        } else {
            // Patron nuevo: hereda el tipo dinamico previo si no se indica.
            DataType dt = s.scan_type();
            const DataType toktype = dynamic_type_token(args.back());
            if (type_is_dynamic(toktype)) dt = toktype;
            const std::string text = join_strip_quotes(args, args.size() - 1);
            std::string err;
            BytePattern pat = build_dynamic_pattern(dt, text, err);
            if (!pat.valid) {
                printf("Patron invalido (%s): %s\n", type_name(dt), err.c_str());
                return {};
            }
            newspec = make_dynamic_spec(dt, std::move(pat));
            s.set_scan_type(dt);
        }
        ScanOutcome o = app.next_scan_dynamic(filter, newspec);
        if (!o.ok) {
            printf("Error: %s\n", o.error.c_str());
            return {};
        }
        printf("Next Scan: %zu coincidencias\n", s.scanner().count());
        return {};
    }

    // Refinamiento numerico: filtro + (en algunos casos) valor de comparacion.
    DataType type = s.scan_type();
    Filter filter = Filter::EXACT;
    std::optional<Value> target;

    const std::string& tok = args[0];
    if (tok == "changed") {
        filter = Filter::CHANGED;
    } else if (tok == "unchanged") {
        filter = Filter::UNCHANGED;
    } else if (tok == "increased" || tok == "increase") {
        filter = Filter::INCREASED;
    } else if (tok == "decreased" || tok == "decrease") {
        filter = Filter::DECREASED;
    } else if (tok == ">" || tok == "<" || tok == ">=" || tok == "<=" || tok == "!=" || tok == "=") {
        if (args.size() < 2) {
            printf("Falta el valor de comparacion.\n");
            return {};
        }
        if (args.size() >= 3 && parse_type(args.back(), type)) {}
        filter = (tok == ">") ? Filter::GREATER
               : (tok == "<") ? Filter::LESS
               : (tok == ">=") ? Filter::GE
               : (tok == "<=") ? Filter::LE
               : (tok == "!=") ? Filter::NE
                               : Filter::EXACT;
        Value v;
        if (!parse_value(args[1], type, v)) {
            printf("Valor invalido: %s (tipo %s)\n", args[1].c_str(), type_name(type));
            return {};
        }
        target = v;
    } else {
        if (args.size() >= 2 && parse_type(args.back(), type)) {}
        Value v;
        if (!parse_value(tok, type, v)) {
            printf("Valor invalido: %s (tipo %s)\n", tok.c_str(), type_name(type));
            return {};
        }
        target = v;
    }
    // IMP-3 (auditoria de estabilizacion): el next numerico debe usar el
    // mismo tipo que el first (los valores y filtros se interpretan con el
    // tipo original del escaneo). Un tipo distinto se rechaza con mensaje
    // claro en lugar de reinterpretar silenciosamente los candidatos.
    const std::string type_err =
        next_type_mismatch_message(s.scanner().first_type(), type);
    if (!type_err.empty()) {
        printf("%s\n", type_err.c_str());
        return {};
    }
    s.set_scan_type(type);
    ScanOutcome o = app.next_scan(type, filter, target);
    if (!o.ok) {
        printf("Error: %s\n", o.error.c_str());
        return {};
    }
    printf("Next Scan: %zu coincidencias\n", s.scanner().count());
    return {};
}

static CommandResult cmd_count(const CommandArgs&, Session& s) {
    if (!s.scanner().has_results())
        printf("No hay resultados previos.\n");
    else
        printf("%zu coincidencias\n", s.scanner().count());
    return {};
}

static CommandResult cmd_results(const CommandArgs& args, Session& s) {
    if (!s.scanner().has_results()) {
        printf("No hay resultados previos.\n");
        return {};
    }
    // Resultados dinamicos: address + longitud + patron (el valor no se
    // copia; el patron vive compartido en la especificacion del escaneo).
    if (s.scanner().is_dynamic()) {
        const DynamicScanSpec& spec = s.scanner().dyn_spec();
        const auto& res = s.scanner().dynamic_results();
        size_t n = 20;
        if (!args.empty()) n = (size_t)strtoull(args[0].c_str(), nullptr, 10);
        n = std::min(n, res.size());
        const std::string ptxt = dynamic_pattern_text(spec);
        for (size_t i = 0; i < n; ++i)
            printf("[%4zu] 0x%016llx  len %zu  %s (%s)\n", i,
                   (unsigned long long)res[i].addr, spec.length(),
                   ptxt.c_str(), type_name(spec.type));
        if (n < res.size())
            printf("... y %zu mas. Usa 'results %zu' para verlas todas.\n",
                   res.size() - n, res.size());
        return {};
    }
    size_t n = 20;
    if (!args.empty()) n = (size_t)strtoull(args[0].c_str(), nullptr, 10);
    const auto& res = s.scanner().results();
    const DataType type = s.scan_type();
    n = std::min(n, res.size());
    for (size_t i = 0; i < n; ++i) {
        printf("[%4zu] 0x%016llx = %s (%s)\n", i,
               (unsigned long long)res[i].addr,
               display_value(res[i].prev, type).c_str(),
               type_name(type));
    }
    if (n < res.size())
        printf("... y %zu mas. Usa 'results %zu' para verlas todas.\n",
               res.size() - n, res.size());
    return {};
}

static CommandResult cmd_pattern(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.empty()) {
        printf("Uso: pattern <bytes>  (Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0)\n");
        return {};
    }

    std::string pat_text;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) pat_text += ' ';
        pat_text += args[i];
    }
    BytePattern pat;
    if (!parse_pattern(pat_text, pat)) {
        printf("Patron invalido: %s\n", pat.error.c_str());
        return {};
    }

    Application app(s);
    PatternOutcome o = app.pattern_scan(pat);
    if (!o.ok) {
        printf("Error: %s\n", o.error.c_str());
        return {};
    }
    printf("Pattern Scan (%zu bytes%s): %zu coincidencias\n",
           pat.size(), pat.has_wildcards() ? ", con wildcards" : "",
           o.hits.size());
    const size_t n = std::min<size_t>(o.hits.size(), 20);
    for (size_t i = 0; i < n; ++i)
        printf("[%4zu] 0x%016llx\n", i, (unsigned long long)o.hits[i]);
    if (n < o.hits.size())
        printf("... y %zu mas.\n", o.hits.size() - n);
    if (o.truncated)
        printf("AVISO: resultado truncado (limite de coincidencias).\n");
    return {};
}

static CommandResult cmd_view(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.empty()) {
        printf("Uso: view <direccion> [len]\n");
        return {};
    }
    uint64_t addr = 0;
    if (!parse_addr(args[0], addr)) {
        printf("Direccion invalida: %s\n", args[0].c_str());
        return {};
    }
    size_t len = 64;
    if (args.size() >= 2) len = (size_t)strtoull(args[1].c_str(), nullptr, 10);
    len = std::min<size_t>(len, 4096);

    Application app(s);
    ReadBytesOutcome o = app.read_bytes(addr, len);
    if (!o.ok) {
        printf("Error: %s\n", o.error.c_str());
        return {};
    }
    if (o.got < 0) {
        printf("Error de lectura en 0x%llx\n", (unsigned long long)addr);
        return {};
    }
    printf("Memoria en 0x%llx (%zd bytes):\n", (unsigned long long)addr, o.got);
    hexdump(addr, o.bytes.data(), (size_t)o.got);
    return {};
}

// Formatea un WriteOutcome (nucleo unico de escritura) como texto de la CLI.
static void print_write_outcome(const WriteOutcome& o) {
    if (!o.had_old) {
        // Fallo antes de leer el valor actual: o.error tiene el motivo.
        printf("%s\n", o.error.c_str());
        return;
    }
    printf("Actual: 0x%llx = %s\n", (unsigned long long)o.address,
           display_value(o.old_value, o.type).c_str());
    if (!o.wrote) {
        if (!o.error.empty()) printf("%s\n", o.error.c_str());
        return;
    }
    printf("Nuevo:  0x%llx = %s %s\n", (unsigned long long)o.address,
           display_value(o.new_value, o.type).c_str(),
           o.verified ? "(verificado)" : "(NO verificado)");
}

static CommandResult cmd_set(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.size() < 2) {
        printf("Uso: set <direccion> <valor> [tipo]\n");
        return {};
    }
    uint64_t addr = 0;
    if (!parse_addr(args[0], addr)) {
        printf("Direccion invalida: %s\n", args[0].c_str());
        return {};
    }
    if (args.size() >= 3 &&
        (args[2] == "string" || args[2] == "bytes" ||
         args[2] == "pattern" || args[2] == "aob")) {
        printf("La escritura de strings/bytes no esta soportada; "
               "usa 'set' con un tipo numerico.\n");
        return {};
    }
    DataType type = DataType::I32;
    if (args.size() >= 3 && parse_type(args[2], type)) {}
    Value v;
    if (!parse_value(args[1], type, v)) {
        printf("Valor invalido: %s (tipo %s)\n", args[1].c_str(), type_name(type));
        return {};
    }

    Application app(s);
    print_write_outcome(app.write(addr, type, v));
    return {};
}

static CommandResult cmd_info(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.empty()) {
        printf("Uso: info <direccion>\n");
        return {};
    }
    uint64_t addr = 0;
    if (!parse_addr(args[0], addr)) {
        printf("Direccion invalida: %s\n", args[0].c_str());
        return {};
    }
    Application app(s);
    InfoOutcome o = app.region_info(addr);
    if (!o.ok) {
        printf("%s\n", o.error.c_str());
        return {};
    }
    const Region& r = *o.region;
    printf("Direccion:    0x%llx\n", (unsigned long long)addr);
    printf("Region:       0x%llx - 0x%llx (%llu bytes)\n",
           (unsigned long long)r.start, (unsigned long long)r.end,
           (unsigned long long)r.size());
    printf("Permisos:     %s\n", r.perms.c_str());
    printf("Offset:       0x%llx\n", (unsigned long long)r.offset);
    printf("Archivo:      %s\n", r.path.empty() ? "(anonimo)" : r.path.c_str());
    return {};
}

// ---------------------------------------------------------------------------
// Comando 'table': Address Table de la sesion.
//
//   table                       listar entradas
//   table add <dir> [tipo] [desc...]
//   table add-result <idx> [desc...]
//   table remove <idx> | clear | toggle <idx>
//   table read [idx] | set <idx> <valor>
//   table save <archivo> | load <archivo>

static CommandResult cmd_table_list(Session& s) {
    if (s.table().empty()) {
        printf("La tabla esta vacia. Usa 'table add' o 'table add-result'.\n");
        return {};
    }
    printf("%-4s %-20s %-12s %-24s %s\n", "ID", "Address", "Type", "Description", "Enabled");
    for (size_t i = 0; i < s.table().size(); ++i) {
        const AddressEntry& e = *s.table().get(i);
        std::string tcol;
        if (e.ptr) {
            tcol = (e.ptr->root.kind == PointerBaseKind::MODULE)
                       ? "pointer[module]"
                       : "pointer[abs]";
        } else {
            tcol = type_name(e.type);
        }
        printf("%-4zu 0x%016llx %-12s %-24s %s%s\n", i,
               (unsigned long long)e.address, tcol.c_str(),
               e.description.c_str(), e.enabled ? "yes" : "no",
               e.stale ? "  (stale)" : "");
    }
    return {};
}

static CommandResult cmd_table_add(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: table add <direccion> [tipo] [descripcion...]\n");
        return {};
    }
    uint64_t addr = 0;
    if (!parse_addr(args[0], addr)) {
        printf("Direccion invalida: %s\n", args[0].c_str());
        return {};
    }
    if (args.size() >= 2 &&
        (args[1] == "string" || args[1] == "bytes" ||
         args[1] == "pattern" || args[1] == "aob")) {
        printf("Los tipos string/bytes no se pueden guardar en la tabla; "
               "usa un tipo numerico.\n");
        return {};
    }
    DataType type = DataType::I32;
    size_t di = 1;
    if (args.size() >= 2 && parse_type(args[1], type)) di = 2;
    std::string desc = join_desc(args, di);

    Application app(s);
    size_t idx = app.add_entry(addr, type, desc);
    printf("Entrada %zu anadida: 0x%016llx (%s) \"%s\"\n", idx,
           (unsigned long long)addr, type_name(type), desc.c_str());
    return {};
}

static CommandResult cmd_table_add_result(const CommandArgs& args, Session& s) {
    if (!s.scanner().has_results()) {
        printf("No hay resultados previos; usa 'first' primero.\n");
        return {};
    }
    if (s.scanner().is_dynamic()) {
        printf("Los resultados de string/bytes no se pueden anadir a la tabla; "
               "usa un escaneo numerico.\n");
        return {};
    }
    if (args.empty()) {
        printf("Uso: table add-result <indice> [descripcion...]\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    const auto& res = s.scanner().results();
    if (idx >= res.size()) {
        printf("Indice %zu fuera de rango (hay %zu resultados).\n", idx, res.size());
        return {};
    }
    std::string desc = join_desc(args, 1);

    Application app(s);
    size_t nidx = app.add_result_entry(idx, desc);
    printf("Entrada %zu anadida desde results[%zu]: 0x%016llx (%s)\n", nidx, idx,
           (unsigned long long)res[idx].addr, type_name(s.scan_type()));
    return {};
}

static CommandResult cmd_table_remove(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: table remove <indice>\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    Application app(s);
    if (!app.remove_entry(idx)) {
        printf("No existe la entrada %zu.\n", idx);
        return {};
    }
    printf("Entrada %zu eliminada.\n", idx);
    return {};
}

static CommandResult cmd_table_clear(const CommandArgs&, Session& s) {
    Application app(s);
    app.clear_entries();
    printf("Tabla vaciada.\n");
    return {};
}

// Formatea un EntryReadOutcome (lectura de una entrada de la tabla).
static void print_entry_read(const EntryReadOutcome& o) {
    if (!o.attempted) {
        printf("[%zu] %s\n", o.index, o.error.c_str());
        return;
    }
    if (!o.ok) {
        printf("[%zu] %s\n", o.index, o.error.c_str());
        return;
    }
    printf("[%zu] 0x%016llx = %s (%s)%s\n", o.index,
           (unsigned long long)o.address,
           display_value(o.value, o.type).c_str(), type_name(o.type),
           o.was_stale ? "  (stale)" : "");
}

static CommandResult cmd_table_read(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (s.table().empty()) {
        printf("La tabla esta vacia.\n");
        return {};
    }
    Application app(s);

    // Indice especifico.
    if (!args.empty()) {
        size_t idx = 0;
        if (!parse_index(args[0], idx)) {
            printf("Indice invalido: %s\n", args[0].c_str());
            return {};
        }
        EntryReadOutcome o = app.read_entry(idx);
        if (!o.attempted && o.error.empty()) {
            printf("No existe la entrada %zu.\n", idx);
            return {};
        }
        print_entry_read(o);
        return {};
    }

    // Todas las entradas activas, en un solo attach.
    std::vector<EntryReadOutcome> outs;
    app.read_all_entries(outs);
    for (const auto& o : outs) print_entry_read(o);
    return {};
}

static CommandResult cmd_table_set(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (args.size() < 2) {
        printf("Uso: table set <indice> <valor>\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    AddressEntry* e = s.table().get(idx);
    if (!e) {
        printf("No existe la entrada %zu.\n", idx);
        return {};
    }
    if (!e->enabled) {
        printf("La entrada %zu esta desactivada (usa 'table toggle %zu').\n", idx, idx);
        return {};
    }
    Value v;
    if (!parse_value(args[1], e->type, v)) {
        printf("Valor invalido: %s (tipo %s)\n", args[1].c_str(), type_name(e->type));
        return {};
    }

    Application app(s);
    print_write_outcome(app.write_entry(idx, v));
    return {};
}

static CommandResult cmd_table_toggle(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: table toggle <indice>\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    AddressEntry* e = s.table().get(idx);
    if (!e) {
        printf("No existe la entrada %zu.\n", idx);
        return {};
    }
    e->enabled = !e->enabled;
    printf("Entrada %zu %s.\n", idx, e->enabled ? "activada" : "desactivada");
    return {};
}

static CommandResult cmd_table_save(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: table save <archivo>\n");
        return {};
    }
    Application app(s);
    std::string err;
    if (!app.save_table(args[0], err)) {
        printf("Error al guardar: %s\n", err.c_str());
        return {};
    }
    printf("Tabla guardada (%zu entradas) en %s\n", s.table().size(), args[0].c_str());
    return {};
}

static CommandResult cmd_table_load(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: table load <archivo>\n");
        return {};
    }
    Application app(s);
    std::string err;
    if (!app.load_table(args[0], err)) {
        printf("Error al cargar: %s\n", err.c_str());
        return {};
    }
    printf("Tabla cargada: %zu entradas desde %s\n", s.table().size(), args[0].c_str());
    return {};
}

// Dispatch de subcomandos de 'table'.
static CommandResult cmd_table(const CommandArgs& args, Session& s) {
    if (args.empty()) return cmd_table_list(s);

    const std::string& sub = args[0];
    CommandArgs rest(args.begin() + 1, args.end());
    if (sub == "add") return cmd_table_add(rest, s);
    if (sub == "add-result") return cmd_table_add_result(rest, s);
    if (sub == "remove") return cmd_table_remove(rest, s);
    if (sub == "clear") return cmd_table_clear(rest, s);
    if (sub == "read") return cmd_table_read(rest, s);
    if (sub == "set") return cmd_table_set(rest, s);
    if (sub == "toggle") return cmd_table_toggle(rest, s);
    if (sub == "save") return cmd_table_save(rest, s);
    if (sub == "load") return cmd_table_load(rest, s);

    printf("Subcomando de tabla desconocido: %s (usa 'help')\n", sub.c_str());
    return {};
}

// ---------------------------------------------------------------------------
// Comando 'pointer': Pointer Scanner integrado con Session y AddressTable.
//
//   pointer scan <dir> [depth=N] [code]   buscar cadenas hacia una direccion
//   pointer results [n]                   mostrar cadenas del ultimo escaneo
//   pointer chains [n]                    alias de 'pointer results'
//   pointer add <idx>                     anadir la base de una cadena a la tabla

std::string pointer_chain_description(const std::vector<uint64_t>& nodes) {
    std::string s;
    char b[32];
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) s += " -> ";
        snprintf(b, sizeof b, "0x%016llx", (unsigned long long)nodes[i]);
        s += b;
    }
    return s;
}

std::string pointer_chain_description(const std::vector<uint64_t>& nodes,
                                      const std::vector<uint64_t>& offsets) {
    // Intercala los offsets post-deref entre los nodos. Si la cantidad no
    // cuadra (cadenas V1 sin offsets), se comporta como la version basica.
    if (offsets.size() != (nodes.empty() ? 0 : nodes.size() - 1))
        return pointer_chain_description(nodes);
    std::string s;
    char b[32];
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) s += " -> ";
        snprintf(b, sizeof b, "0x%016llx", (unsigned long long)nodes[i]);
        s += b;
        // offset 0 no se muestra (comportamiento V1 para cadenas directas)
        if (i < offsets.size() && offsets[i] != 0) {
            snprintf(b, sizeof b, " -> +0x%llx", (unsigned long long)offsets[i]);
            s += b;
        }
    }
    return s;
}

// Describe la raiz de una cadena para 'pointer results': MODULE (path +
// offset) o ABSOLUTE, con su persistencia. Devuelve true si es MODULE.
static bool print_root_line(const std::vector<Region>& regions,
                            const PointerChain& c, char* buf, size_t n) {
    PointerBase b;
    if (c.nodes.empty()) {
        snprintf(buf, n, "[root ?]");
        return false;
    }
    b = make_base_from_address(regions, c.nodes[0]);
    if (b.kind == PointerBaseKind::MODULE) {
        snprintf(buf, n, "[root MODULE %s +0x%llx] (persistente)",
                 b.module.c_str(), (unsigned long long)b.offset);
        return true;
    }
    snprintf(buf, n, "[root ABSOLUTE 0x%016llx] (no persistente)",
             (unsigned long long)b.address);
    return false;
}

static CommandResult cmd_pointer_scan(const CommandArgs& args, Session& s) {
    const PointerScanArgs pa = parse_pointer_scan_args(args);
    if (!pa.error.empty()) {
        printf("%s\n", pa.error.c_str());
        return {};
    }
    if (!has_target(s)) return {};

    PointerScanInput in;
    in.opts.target = pa.target;
    in.opts.max_depth = pa.depth;
    in.opts.include_code = pa.include_code;
    in.opts.max_offset = pa.max_offset;
    in.opts.offset_step = pa.offset_step;
    in.value_type = pa.value_type.value_or(s.scan_type());
    in.module_only = pa.module_only;

    Application app(s);
    PointerScanOutcome o = app.pointer_scan(in);
    if (!o.ok) {
        printf("%s\n", o.error.c_str());
        return {};
    }
    const PointerScanResult& r = o.result;
    printf("Pointer Scan:\n");
    printf("target: 0x%016llx\n", (unsigned long long)r.target);
    printf("depth: %d\n", pa.depth);
    printf("levels: %d\n", r.levels);
    printf("chains: %zu\n", r.chains.size());
    printf("offsets: 0x0..0x%llx (step 0x%llx)%s\n",
           (unsigned long long)pa.max_offset,
           (unsigned long long)pa.offset_step,
           pa.module_only ? " (solo raices de modulo)" : "");
    if (r.edges_truncated || r.chains_truncated) {
        printf("WARNING: pointer scan truncado\n");
        if (r.edges_truncated)
            printf("- limite de aristas alcanzado (max_edges_per_level)\n");
        if (r.chains_truncated)
            printf("- limite de cadenas alcanzado (max_chains)\n");
    }
    return {};
}

static CommandResult cmd_pointer_results(const CommandArgs& args, Session& s) {
    if (!s.pointer_result()) {
        printf("No hay un pointer scan previo; usa 'pointer scan <direccion>' primero.\n");
        return {};
    }
    const PointerScanResult& res = s.pointer_result().value();
    if (res.chains.empty()) {
        printf("El ultimo pointer scan no encontro cadenas.\n");
        return {};
    }
    // Regiones para clasificar la raiz (sin proceso -> ABSOLUTE).
    std::vector<Region> regions;
    if (s.has_pid()) regions = parse_maps(s.pid());

    size_t n = 10;
    if (!args.empty()) n = (size_t)strtoull(args[0].c_str(), nullptr, 10);
    n = std::min(n, res.chains.size());
    for (size_t i = 0; i < n; ++i) {
        printf("[%zu] depth %d:\n", i, res.chains[i].depth);
        char rootline[512];
        print_root_line(regions, res.chains[i], rootline, sizeof rootline);
        printf("  %s\n", rootline);
        printf("  %s\n",
               pointer_chain_description(res.chains[i].nodes,
                                         res.chains[i].offsets).c_str());
    }
    if (n < res.chains.size())
        printf("... y %zu mas. Usa 'pointer results %zu' para verlas todas.\n",
               res.chains.size() - n, res.chains.size());
    return {};
}

static CommandResult cmd_pointer_add(const CommandArgs& args, Session& s) {
    if (!s.pointer_result()) {
        printf("No hay un pointer scan previo; usa 'pointer scan <direccion>' primero.\n");
        return {};
    }
    if (args.empty()) {
        printf("Uso: pointer add <indice>\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    const PointerScanResult& res = s.pointer_result().value();
    if (idx >= res.chains.size()) {
        printf("Indice %zu fuera de rango (hay %zu cadenas).\n", idx,
               res.chains.size());
        return {};
    }
    const PointerChain& c = res.chains[idx];
    if (c.nodes.empty()) {
        printf("La cadena %zu esta vacia.\n", idx);
        return {};
    }

    Application app(s);
    AddChainOutcome o = app.add_pointer_chain(
        idx, pointer_chain_description(c.nodes, c.offsets));
    if (!o.ok) {
        printf("%s\n", o.error.c_str());
        return {};
    }
    const size_t nidx = o.table_index;
    printf("Entrada %zu anadida desde la cadena %zu:\n", nidx, idx);
    if (o.ref.root.kind == PointerBaseKind::MODULE)
        printf("  pointer[module]  (persistente)\n");
    else
        printf("  pointer[abs]  (no persistente)\n");
    printf("  tipo: %s\n", type_name(res.value_type));
    printf("  %s\n", pointer_chain_description(c.nodes, c.offsets).c_str());
    return {};
}

static CommandResult cmd_pointer_resolve(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: pointer resolve <indice>\n");
        return {};
    }
    size_t idx = 0;
    if (!parse_index(args[0], idx)) {
        printf("Indice invalido: %s\n", args[0].c_str());
        return {};
    }
    AddressEntry* e = s.table().get(idx);
    if (!e) {
        printf("No existe la entrada %zu.\n", idx);
        return {};
    }
    if (!e->ptr) {
        printf("La entrada %zu no es una cadena dinamica (pointer).\n", idx);
        return {};
    }
    if (!has_target(s)) return {};

    Application app(s);
    ResolveEntryOutcome o = app.resolve_entry(idx);
    if (!o.ok) {
        printf("Error al resolver la entrada %zu: %s\n", idx, o.error.c_str());
        return {};
    }
    printf("Entrada %zu resuelta: 0x%016llx = %s (%s)\n", idx,
           (unsigned long long)o.address,
           display_value(o.value, o.type).c_str(),
           type_name(o.type));
    return {};
}

static CommandResult cmd_pointer(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: pointer scan <direccion> [depth=N] [max_offset=X] "
               "[offset_step=S] [module-only] [code] [type=T] | "
               "pointer results [n] | pointer add <indice> | "
               "pointer resolve <indice>\n");
        return {};
    }
    const std::string& sub = args[0];
    CommandArgs rest(args.begin() + 1, args.end());
    if (sub == "scan") return cmd_pointer_scan(rest, s);
    if (sub == "results" || sub == "chains") return cmd_pointer_results(rest, s);
    if (sub == "add") return cmd_pointer_add(rest, s);
    if (sub == "resolve") return cmd_pointer_resolve(rest, s);
    printf("Subcomando de pointer desconocido: %s (usa 'help')\n", sub.c_str());
    return {};
}

// ---------------------------------------------------------------------------
// Tabla de comandos y dispatch

static const CommandDef kCommands[] = {
    {"list", "list                                 Listar procesos", cmd_list},
    {"attach", "attach <pid|nombre>                  Seleccionar proceso objetivo", cmd_attach},
    {"detach", "detach                               Quitar proceso objetivo", cmd_detach},
    {"maps", "maps [pid]                           Mostrar regiones de memoria", cmd_maps},
    {"first",
     "first <valor> [tipo]                 Primer escaneo (valor exacto)\n"
     "  first unknown [tipo]                 Primer escaneo (valor desconocido)\n"
     "  first \"<texto>\" string              Buscar un texto (bytes ASCII)\n"
     "  first <hex...> bytes|pattern         Buscar bytes con wildcards ??",
     cmd_first},
    {"next",
     "next <valor> [tipo]                  Refinar resultados (igual)\n"
     "  next changed|unchanged|increased|decreased   Refinar por cambio\n"
     "  next >|<|>=|<=|!= <valor> [tipo]     Refinar por comparacion\n"
     "  next \"<texto>\" string | <hex...> bytes   Refinar un escaneo dinamico\n"
     "  next changed|unchanged               (string/bytes: cambio exacto)",
     cmd_next},
    {"count", "count                                Numero de coincidencias", cmd_count},
    {"results", "results [n]                          Mostrar las primeras n coincidencias", cmd_results},
    {"pattern",
     "pattern <bytes>                      Buscar secuencia de bytes (AOB)\n"
     "                                       Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0",
     cmd_pattern},
    {"view", "view <direccion> [len]               Visor hexadecimal", cmd_view},
    {"set", "set <direccion> <valor> [tipo]       Escribir valor en memoria", cmd_set},
    {"info", "info <direccion>                     Informacion de la region", cmd_info},
    {"table",
     "table [add|add-result|read|set|toggle|remove|clear|save|load]  Tabla de direcciones\n"
     "  table add <direccion> [tipo] [desc]    Anadir direccion manualmente\n"
     "  table add-result <idx> [desc]          Anadir entrada desde 'results'\n"
     "  table read [idx] | set <idx> <valor>   Leer / escribir valor actual\n"
     "  table toggle <idx> | remove <idx>      Activar-desactivar / eliminar\n"
     "  table clear | save <f> | load <f>      Vaciar / guardar / cargar",
     cmd_table},
    {"pointer",
     "pointer scan <dir> [depth=N] [max_offset=X] [offset_step=S] [module-only] [code] [type=T]\n"
     "                                       Buscar cadenas de punteros (con offsets)\n"
     "  pointer results [n]                    Mostrar cadenas del ultimo escaneo\n"
     "  pointer chains [n]                     Alias de 'pointer results'\n"
     "  pointer add <idx>                      Anadir la cadena (PointerChainRef) a la tabla\n"
     "  pointer resolve <idx>                  Resolver una entrada pointer a su direccion actual",
     cmd_pointer},
    {"help", "help | quit", cmd_help},
    // Alias ocultos en 'help' (misma funcion que el comando principal).
    {"quit", nullptr, cmd_quit},
    {"exit", nullptr, cmd_quit},
    {"?", nullptr, cmd_help},
    {"aob", nullptr, cmd_pattern},
    {"hexdump", nullptr, cmd_view},
    {"write", nullptr, cmd_set},
};

const std::vector<CommandDef>& commands() {
    static const std::vector<CommandDef> table(
        kCommands, kCommands + sizeof(kCommands) / sizeof(kCommands[0]));
    return table;
}

CommandResult execute(const std::string& line, Session& s) {
    auto t = split(line);
    if (t.empty()) return {};
    for (const auto& c : commands()) {
        if (c.name == t[0]) {
            CommandArgs args(t.begin() + 1, t.end());
            return c.fn(args, s);
        }
    }
    printf("Comando desconocido: %s (usa 'help')\n", t[0].c_str());
    return {false, false};
}

// ---------------------------------------------------------------------------
// Helpers compartidos con la CLI por argumentos

std::optional<int> resolve_target(const std::string& s) {
    bool numeric = !s.empty() &&
                   std::all_of(s.begin(), s.end(),
                               [](char c) { return c >= '0' && c <= '9'; });
    if (numeric) {
        // IMP-1 (auditoria de estabilizacion): un PID enorme antes crasheaba
        // con std::stoi (std::out_of_range no capturado -> abort). Ahora se
        // valida con strtol + ERANGE y el rango de int: solo se aceptan PIDs
        // representables (1..INT_MAX; 0 se deja pasar para que attach 0 de el
        // error normal "no existe el proceso").
        errno = 0;
        char* end = nullptr;
        long v = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0' || errno == ERANGE ||
            v > INT_MAX) {
            printf("PID invalido o fuera de rango: %s\n", s.c_str());
            return std::nullopt;
        }
        return (int)v;
    }
    // Parece un numero con signo (no un nombre de proceso): error claro.
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '+') &&
        std::all_of(s.begin() + 1, s.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) {
        printf("PID invalido: %s\n", s.c_str());
        return std::nullopt;
    }

    std::vector<int> matches;
    for (const auto& p : list_processes())
        if (p.name == s) matches.push_back(p.pid);
    if (matches.size() == 1) return matches[0];
    if (matches.empty())
        printf("No se encontro ningun proceso llamado '%s'.\n", s.c_str());
    else
        printf("Hay %zu procesos llamados '%s'; usa un PID.\n", matches.size(), s.c_str());
    return std::nullopt;
}

void print_maps(int pid) {
    auto regions = parse_maps(pid);
    if (regions.empty()) {
        printf("No se pudieron leer regiones del proceso %d (¿existe?)\n", pid);
        return;
    }
    printf("%-35s %-4s %-10s %-9s %s\n", "RANGO", "PERMS", "TAMANO", "OFFSET", "ARCHIVO");
    for (const auto& r : regions) {
        printf("%016llx-%016llx %-4s %10llu %-9llx %s\n",
               (unsigned long long)r.start, (unsigned long long)r.end,
               r.perms.c_str(), (unsigned long long)r.size(),
               (unsigned long long)r.offset, r.path.c_str());
    }
}

} // namespace mt
