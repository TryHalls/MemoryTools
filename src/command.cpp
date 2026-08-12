// command.cpp - Implementacion de la capa de comandos de MemoryTool.
//
// Los handlers reproducen el comportamiento de la antigua CLI monolitica
// (main.cpp): misma sintaxis, mismos mensajes y mismos resultados. Solo se
// reestructura donde vive cada responsabilidad.
#include "command.h"

#include "session.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unistd.h>

#include "address_table.h"
#include "memory.h"
#include "pattern.h"
#include "pointer.h"
#include "pointer_resolver.h"
#include "process.h"
#include "types.h"

namespace mt {

// ---------------------------------------------------------------------------
// Utilidades internas (solo usadas por los handlers)

static std::vector<std::string> split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string t;
    while (iss >> t) out.push_back(t);
    return out;
}

static bool parse_addr(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    int base = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    out = strtoull(s.c_str(), &end, base);
    return end != s.c_str() && *end == '\0' && errno != ERANGE;
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

static CommandResult cmd_first_dynamic(DataType type, const std::string& text,
                                       Session& s) {
    std::string err;
    BytePattern pat = build_dynamic_pattern(type, text, err);
    if (!pat.valid) {
        printf("Patron invalido (%s): %s\n", type_name(type), err.c_str());
        return {};
    }
    const DynamicScanSpec spec = make_dynamic_spec(type, std::move(pat));
    s.set_scan_type(type);

    std::string err2;
    bool ok = s.with_memory([&](Memory& mem) {
        auto regions = parse_maps(s.pid());
        s.scanner().first_scan_dynamic(mem, regions, spec);
    }, err2);
    if (!ok) {
        printf("Error: %s\n", err2.c_str());
        return {};
    }
    printf("First Scan: %zu coincidencias (%s, len %zu)\n",
           s.scanner().count(), type_name(type), spec.length());
    if (s.scanner().truncated())
        printf("AVISO: se alcanzo el limite de candidatos; el resultado esta truncado.\n");
    if (s.scanner().warned())
        printf("AVISO: la lista de candidatos es muy grande; el escaneo puede consumir mucha RAM.\n");
    return {};
}

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
    // Comprobar si el cambio de proceso obliga a descartar resultados.
    const bool switching = s.has_pid() && s.pid() != *target;
    std::string err;
    if (!s.attach(*target, err)) {
        printf("No se pudo acceder al proceso %d: %s\n", *target, err.c_str());
        return {};
    }
    if (switching)
        printf("Resultados anteriores descartados (cambio de proceso).\n");
    printf("Proceso objetivo: %d\n", *target);
    return {};
}

static CommandResult cmd_detach(const CommandArgs&, Session& s) {
    s.detach();
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

    // first unknown [tipo]: escaneo numerico de valor desconocido (nunca
    // dinamico, aunque el ultimo token parezca un tipo).
    if (args[0] == "unknown") {
        DataType type = DataType::I32;
        if (args.size() >= 2 && parse_type(args[1], type)) {}
        s.set_scan_type(type);
        std::string err;
        bool ok = s.with_memory([&](Memory& mem) {
            auto regions = parse_maps(s.pid());
            s.scanner().first_scan(mem, regions, type, std::nullopt);
        }, err);
        if (!ok) {
            printf("Error: %s\n", err.c_str());
            return {};
        }
        printf("Escaneo 'unknown' completado (%zu posiciones legibles).\n",
               s.scanner().count());
        if (s.scanner().truncated())
            printf("AVISO: se alcanzo el limite de candidatos; el resultado esta truncado.\n");
        if (s.scanner().warned())
            printf("AVISO: la lista de candidatos es muy grande; el escaneo puede consumir mucha RAM.\n");
        return {};
    }

    // Valores dinamicos: first "<texto>" string | first <hex...> bytes|pattern.
    const DataType dynt = dynamic_type_token(args.back());
    if (type_is_dynamic(dynt)) {
        const std::string text = join_strip_quotes(args, args.size() - 1);
        return cmd_first_dynamic(dynt, text, s);
    }

    // Camino numerico (sin cambios): first <valor> [tipo].
    DataType type = DataType::I32;
    std::optional<Value> target;
    if (args.size() >= 2 && parse_type(args.back(), type)) {}
    Value v;
    if (!parse_value(args[0], type, v)) {
        printf("Valor invalido: %s (tipo %s)\n", args[0].c_str(), type_name(type));
        return {};
    }
    target = v;
    s.set_scan_type(type);

    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        auto regions = parse_maps(s.pid());
        s.scanner().first_scan(mem, regions, type, target);
    }, err);
    if (!ok) {
        printf("Error: %s\n", err.c_str());
        return {};
    }

    printf("First Scan: %zu coincidencias (%s = %s)\n", s.scanner().count(),
           type_name(type), display_value(*target, type).c_str());
    if (s.scanner().truncated())
        printf("AVISO: se alcanzo el limite de candidatos; el resultado esta truncado.\n");
    if (s.scanner().warned())
        printf("AVISO: la lista de candidatos es muy grande; el escaneo puede consumir mucha RAM.\n");
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

        std::string err;
        bool ok = s.with_memory([&](Memory& mem) {
            s.scanner().next_scan_dynamic(mem, filter, newspec);
        }, err);
        if (!ok) {
            printf("Error: %s\n", err.c_str());
            return {};
        }
        printf("Next Scan: %zu coincidencias\n", s.scanner().count());
        return {};
    }

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
    s.set_scan_type(type);

    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        s.scanner().next_scan(mem, type, filter, target);
    }, err);
    if (!ok) {
        printf("Error: %s\n", err.c_str());
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

    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        auto regions = parse_maps(s.pid());
        auto res = scan_pattern(mem, regions, pat);
        printf("Pattern Scan (%zu bytes%s): %zu coincidencias\n",
               pat.size(), pat.has_wildcards() ? ", con wildcards" : "",
               res.hits.size());
        const size_t n = std::min<size_t>(res.hits.size(), 20);
        for (size_t i = 0; i < n; ++i)
            printf("[%4zu] 0x%016llx\n", i,
                   (unsigned long long)res.hits[i]);
        if (n < res.hits.size())
            printf("... y %zu mas.\n", res.hits.size() - n);
        if (res.truncated)
            printf("AVISO: resultado truncado (limite de coincidencias).\n");
    }, err);
    if (!ok) printf("Error: %s\n", err.c_str());
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

    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        std::vector<uint8_t> buf(len);
        ssize_t got = mem.read(addr, buf.data(), len);
        if (got < 0) {
            printf("Error de lectura en 0x%llx\n", (unsigned long long)addr);
            return;
        }
        printf("Memoria en 0x%llx (%zd bytes):\n", (unsigned long long)addr, got);
        hexdump(addr, buf.data(), (size_t)got);
    }, err);
    if (!ok) printf("Error: %s\n", err.c_str());
    return {};
}

// Resultado de una escritura de memoria.
struct WriteOutcome {
    bool ok = false;   // true si la escritura se completo y verifico
    std::string msg;   // texto completo a mostrar (o el error)
};

// Escribe 'value' (tipo 'type') en 'addr' con la memoria YA abierta y las
// regiones dadas: comprueba region (existencia + escribible), lee el valor
// actual, escribe y relee para verificar. Es el nucleo unico de escritura;
// lo usan 'set', 'table set' (absoluta y dinamica) sin duplicar logica.
static WriteOutcome write_value_at(Memory& mem, const std::vector<Region>& regions,
                                   uint64_t addr, DataType type,
                                   const Value& value) {
    WriteOutcome o;
    char b[512];

    auto r = region_at(regions, addr);
    if (!r) {
        snprintf(b, sizeof b, "La direccion 0x%llx no pertenece a ninguna region.\n",
                 (unsigned long long)addr);
        o.msg = b;
        return o;
    }
    if (!r->writable()) {
        o.msg = "La region no es escribible (" + r->perms + ").\n";
        return o;
    }

    const size_t w = type_size(type);
    uint8_t cur[8] = {0};
    ssize_t got = mem.read(addr, cur, w);
    if (got != (ssize_t)w) {
        snprintf(b, sizeof b, "No se pudo leer el valor actual en 0x%llx\n",
                 (unsigned long long)addr);
        o.msg = b;
        return o;
    }
    Value old = value_from_bytes(cur, w);
    snprintf(b, sizeof b, "Actual: 0x%llx = %s\n", (unsigned long long)addr,
             display_value(old, type).c_str());
    o.msg = b;

    ssize_t wr = mem.write(addr, &value.bits, w);
    if (wr != (ssize_t)w) {
        snprintf(b, sizeof b, "Error de escritura (%zd bytes escritos)\n", wr);
        o.msg += b;
        return o;
    }
    uint8_t ver[8] = {0};
    if (mem.read(addr, ver, w) == (ssize_t)w) {
        Value nv = value_from_bytes(ver, w);
        snprintf(b, sizeof b, "Nuevo:  0x%llx = %s %s\n", (unsigned long long)addr,
                 display_value(nv, type).c_str(),
                 value_equal(nv, value, type) ? "(verificado)" : "(NO verificado)");
        o.msg += b;
        o.ok = true;
    }
    return o;
}

// Escribe 'value' (tipo 'type') en 'addr' a traves de Session: regiones +
// attach + operacion + detach. Usado por 'set' y 'table set' (absoluta).
static WriteOutcome write_value(Session& s, uint64_t addr, DataType type,
                                const Value& value) {
    auto regions = parse_maps(s.pid());
    WriteOutcome o;
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        o = write_value_at(mem, regions, addr, type, value);
    }, err);
    if (!ok) o.msg = "Error: " + err + "\n";
    return o;
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

    WriteOutcome wo = write_value(s, addr, type, v);
    printf("%s", wo.msg.c_str());
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
    auto regions = parse_maps(s.pid());
    auto r = region_at(regions, addr);
    if (!r) {
        printf("La direccion 0x%llx no pertenece a ninguna region.\n",
               (unsigned long long)addr);
        return {};
    }
    printf("Direccion:    0x%llx\n", (unsigned long long)addr);
    printf("Region:       0x%llx - 0x%llx (%llu bytes)\n",
           (unsigned long long)r->start, (unsigned long long)r->end,
           (unsigned long long)r->size());
    printf("Permisos:     %s\n", r->perms.c_str());
    printf("Offset:       0x%llx\n", (unsigned long long)r->offset);
    printf("Archivo:      %s\n", r->path.empty() ? "(anonimo)" : r->path.c_str());
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

    size_t idx = s.table().add(addr, type, desc);
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
    size_t nidx = s.table().add(res[idx].addr, s.scan_type(), desc);
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
    if (!s.table().remove(idx)) {
        printf("No existe la entrada %zu.\n", idx);
        return {};
    }
    printf("Entrada %zu eliminada.\n", idx);
    return {};
}

static CommandResult cmd_table_clear(const CommandArgs&, Session& s) {
    s.table().clear();
    printf("Tabla vaciada.\n");
    return {};
}

// Lee y muestra una entrada. 'mem' ya debe estar abierta. Devuelve true si
// la lectura se completo (marca la entrada como verificada en el proceso
// actual). Para entradas dinamicas (kind 'pointer') resuelve la cadena con
// PointerResolver en CADA lectura (nunca se guarda la direccion resuelta).
static bool read_entry_print(Memory& mem, const std::vector<Region>& regions,
                             size_t idx, AddressEntry& e) {
    if (e.ptr) {
        ResolveResult r = resolve_chain(*e.ptr, mem, regions);
        if (!r.ok) {
            printf("[%zu] %s\n", idx, r.error.c_str());
            return false;
        }
        printf("[%zu] 0x%016llx = %s (%s)%s\n", idx,
               (unsigned long long)r.address,
               display_value(r.value, e.type).c_str(), type_name(e.type),
               e.stale ? "  (stale)" : "");
        e.stale = false;
        return true;
    }
    auto r = region_at(regions, e.address);
    if (!r) {
        printf("[%zu] 0x%016llx: no pertenece a ninguna region.\n", idx,
               (unsigned long long)e.address);
        return false;
    }
    if (!r->readable()) {
        printf("[%zu] 0x%016llx: region no legible (%s).\n", idx,
               (unsigned long long)e.address, r->perms.c_str());
        return false;
    }
    const size_t w = type_size(e.type);
    uint8_t buf[8] = {0};
    ssize_t got = mem.read(e.address, buf, w);
    if (got != (ssize_t)w) {
        printf("[%zu] 0x%016llx: no se pudo leer (%zd bytes).\n", idx,
               (unsigned long long)e.address, got);
        return false;
    }
    Value v = value_from_bytes(buf, w);
    printf("[%zu] 0x%016llx = %s (%s)%s\n", idx, (unsigned long long)e.address,
           display_value(v, e.type).c_str(), type_name(e.type),
           e.stale ? "  (stale)" : "");
    e.stale = false; // relectura exitosa en el proceso actual
    return true;
}

static CommandResult cmd_table_read(const CommandArgs& args, Session& s) {
    if (!has_target(s)) return {};
    if (s.table().empty()) {
        printf("La tabla esta vacia.\n");
        return {};
    }
    auto regions = parse_maps(s.pid());

    // Indice especifico.
    if (!args.empty()) {
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
        std::string err;
        bool ok = s.with_memory([&](Memory& mem) {
            read_entry_print(mem, regions, idx, *e);
        }, err);
        if (!ok) printf("Error: %s\n", err.c_str());
        return {};
    }

    // Todas las entradas activas, en un solo attach.
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        for (size_t i = 0; i < s.table().size(); ++i) {
            AddressEntry* e = s.table().get(i);
            if (!e->enabled) continue;
            read_entry_print(mem, regions, i, *e);
        }
    }, err);
    if (!ok) printf("Error: %s\n", err.c_str());
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
    if (e->ptr) {
        // Entrada dinamica: resolver la cadena y escribir en la direccion
        // resultante (mismo mecanismo que 'set', sin duplicar logica).
        const std::vector<Region> regions = parse_maps(s.pid());
        WriteOutcome wo;
        std::string err;
        bool ok = s.with_memory([&](Memory& mem) {
            ResolveResult r = resolve_chain(*e->ptr, mem, regions);
            if (!r.ok) {
                wo.msg = "Error al resolver la entrada " +
                         std::to_string(idx) + ": " + r.error + "\n";
                return;
            }
            wo = write_value_at(mem, regions, r.address, e->type, v);
        }, err);
        if (!ok) wo.msg = "Error: " + err + "\n";
        if (wo.ok) e->stale = false;
        printf("%s", wo.msg.c_str());
        return {};
    }
    WriteOutcome wo = write_value(s, e->address, e->type, v);
    if (wo.ok) e->stale = false;
    printf("%s", wo.msg.c_str());
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
    std::string err;
    if (!s.table().save(args[0], err)) {
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
    std::string err;
    if (!s.table().load(args[0], err)) {
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

PointerScanArgs parse_pointer_scan_args(const CommandArgs& args) {
    PointerScanArgs out;
    if (args.empty()) {
        out.error = "Uso: pointer scan <direccion> [depth=N] [max_offset=X] "
                    "[offset_step=S] [module-only] [code] [type=T]";
        return out;
    }
    if (!parse_addr(args[0], out.target)) {
        out.error = "Direccion invalida: " + args[0];
        return out;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& tok = args[i];
        if (tok == "code") {
            out.include_code = true;
            continue;
        }
        if (tok == "module-only") {
            out.module_only = true;
            continue;
        }
        if (tok.rfind("depth=", 0) == 0) {
            const std::string d = tok.substr(6);
            char* end = nullptr;
            errno = 0;
            long v = std::strtol(d.c_str(), &end, 10);
            if (d.empty() || end == d.c_str() || *end != '\0' ||
                errno == ERANGE || v < 1 || v > 7) {
                out.error = "depth debe estar entre 1 y 7 (obtenido: " +
                            (d.empty() ? "(vacio)" : d) + ")";
                return out;
            }
            out.depth = (int)v;
            continue;
        }
        if (tok.rfind("max_offset=", 0) == 0) {
            const std::string v = tok.substr(11);
            char* end = nullptr;
            errno = 0;
            unsigned long long x = std::strtoull(v.c_str(), &end, 0);
            if (v.empty() || end == v.c_str() || *end != '\0' ||
                errno == ERANGE || x > 0x10000) {
                out.error = "max_offset invalido (maximo 0x10000): " +
                            (v.empty() ? "(vacio)" : v);
                return out;
            }
            out.max_offset = (uint64_t)x;
            continue;
        }
        if (tok.rfind("offset_step=", 0) == 0) {
            const std::string v = tok.substr(12);
            char* end = nullptr;
            errno = 0;
            unsigned long long x = std::strtoull(v.c_str(), &end, 0);
            if (v.empty() || end == v.c_str() || *end != '\0' ||
                errno == ERANGE || x == 0) {
                out.error = "offset_step debe ser mayor que 0: " +
                            (v.empty() ? "(vacio)" : v);
                return out;
            }
            out.offset_step = (uint64_t)x;
            continue;
        }
        if (tok.rfind("type=", 0) == 0) {
            DataType t;
            if (!parse_type(tok.substr(5), t) || t == DataType::PTR) {
                out.error = "Tipo invalido: " + tok.substr(5);
                return out;
            }
            out.value_type = t;
            continue;
        }
        out.error = "Opcion desconocida: " + tok;
        return out;
    }
    return out;
}

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

static CommandResult cmd_pointer_scan(const CommandArgs& args, Session& s) {
    const PointerScanArgs pa = parse_pointer_scan_args(args);
    if (!pa.error.empty()) {
        printf("%s\n", pa.error.c_str());
        return {};
    }
    if (!has_target(s)) return {};

    // El target debe pertenecer a una region legible (comprobacion previa
    // razonable; el escaneo real ocurre en un solo attach con with_memory).
    auto regions = parse_maps(s.pid());
    auto r = region_at(regions, pa.target);
    if (!r) {
        printf("La direccion 0x%llx no pertenece a ninguna region.\n",
               (unsigned long long)pa.target);
        return {};
    }
    if (!r->readable()) {
        printf("La direccion 0x%llx esta en una region no legible (%s).\n",
               (unsigned long long)pa.target, r->perms.c_str());
        return {};
    }

    PointerScanOptions opts;
    opts.target = pa.target;
    opts.max_depth = pa.depth;
    opts.include_code = pa.include_code;
    opts.max_offset = pa.max_offset;
    opts.offset_step = pa.offset_step;

    const DataType vtype = pa.value_type.value_or(s.scan_type());

    PointerScanResult res;
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        res = pointer_scan(mem, regions, opts);
    }, err);
    if (!ok) {
        printf("Error: %s\n", err.c_str());
        return {};
    }
    res.value_type = vtype;

    if (pa.module_only) {
        // Filtrar: solo cadenas cuya raiz (nodes[0]) esta en una region con
        // pathname de archivo (MODULE, persistente frente a ASLR).
        std::vector<PointerChain> kept;
        for (auto& c : res.chains) {
            if (c.nodes.empty()) continue;
            const PointerBase b = make_base_from_address(regions, c.nodes[0]);
            if (b.kind == PointerBaseKind::MODULE)
                kept.push_back(std::move(c));
        }
        res.chains = std::move(kept);
    }
    s.set_pointer_result(std::move(res));

    const PointerScanResult& r2 = s.pointer_result().value();
    printf("Pointer Scan:\n");
    printf("target: 0x%016llx\n", (unsigned long long)r2.target);
    printf("depth: %d\n", opts.max_depth);
    printf("levels: %d\n", r2.levels);
    printf("chains: %zu\n", r2.chains.size());
    printf("offsets: 0x0..0x%llx (step 0x%llx)%s\n",
           (unsigned long long)opts.max_offset,
           (unsigned long long)opts.offset_step,
           pa.module_only ? " (solo raices de modulo)" : "");
    if (r2.edges_truncated || r2.chains_truncated) {
        printf("WARNING: pointer scan truncado\n");
        if (r2.edges_truncated)
            printf("- limite de aristas alcanzado (max_edges_per_level)\n");
        if (r2.chains_truncated)
            printf("- limite de cadenas alcanzado (max_chains)\n");
    }
    return {};
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

    std::vector<Region> regions;
    if (s.has_pid()) regions = parse_maps(s.pid());
    const PointerChainRef ref =
        make_chain_ref(regions, c.nodes, c.offsets, res.value_type);
    const std::string desc =
        pointer_chain_description(c.nodes, c.offsets);
    const size_t nidx = s.table().add(ref, desc);

    printf("Entrada %zu anadida desde la cadena %zu:\n", nidx, idx);
    if (ref.root.kind == PointerBaseKind::MODULE)
        printf("  pointer[module]  (persistente)\n");
    else
        printf("  pointer[abs]  (no persistente)\n");
    printf("  tipo: %s\n", type_name(res.value_type));
    printf("  %s\n", desc.c_str());
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

    const std::vector<Region> regions = parse_maps(s.pid());
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        ResolveResult r = resolve_chain(*e->ptr, mem, regions);
        if (!r.ok) {
            printf("Error al resolver la entrada %zu: %s\n", idx,
                   r.error.c_str());
            return;
        }
        e->stale = false;
        printf("Entrada %zu resuelta: 0x%016llx = %s (%s)\n", idx,
               (unsigned long long)r.address,
               display_value(r.value, e->type).c_str(),
               type_name(e->type));
    }, err);
    if (!ok) printf("Error: %s\n", err.c_str());
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
    if (numeric) return std::stoi(s);

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
