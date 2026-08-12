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
        printf("Uso: first <valor> [tipo] | first unknown [tipo]\n");
        return {};
    }

    DataType type = DataType::I32;
    std::optional<Value> target;
    if (args[0] == "unknown") {
        if (args.size() >= 2 && parse_type(args[1], type)) {}
    } else {
        if (args.size() >= 2 && parse_type(args.back(), type)) {}
        Value v;
        if (!parse_value(args[0], type, v)) {
            printf("Valor invalido: %s (tipo %s)\n", args[0].c_str(), type_name(type));
            return {};
        }
        target = v;
    }
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

    if (!target)
        printf("Escaneo 'unknown' completado (%zu posiciones legibles).\n",
               s.scanner().count());
    else
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
        printf("Uso: next <valor> [tipo] | next changed|... | next <op> <valor> [tipo]\n");
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

// Escribe 'value' (tipo 'type') en 'addr' a traves de Session: comprueba la
// region (existencia + escribible), adjunta con las garantias de Session,
// lee el valor actual, escribe y relee para verificar. Es el mecanismo
// unico de escritura; lo usan 'set' y 'table set' (no se duplica logica).
static WriteOutcome write_value(Session& s, uint64_t addr, DataType type,
                                const Value& value) {
    WriteOutcome o;
    char b[512];

    auto regions = parse_maps(s.pid());
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
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        uint8_t cur[8] = {0};
        ssize_t got = mem.read(addr, cur, w);
        if (got != (ssize_t)w) {
            snprintf(b, sizeof b, "No se pudo leer el valor actual en 0x%llx\n",
                     (unsigned long long)addr);
            o.msg = b;
            return;
        }
        Value old = value_from_bytes(cur, w);
        snprintf(b, sizeof b, "Actual: 0x%llx = %s\n", (unsigned long long)addr,
                 display_value(old, type).c_str());
        o.msg = b;

        ssize_t wr = mem.write(addr, &value.bits, w);
        if (wr != (ssize_t)w) {
            snprintf(b, sizeof b, "Error de escritura (%zd bytes escritos)\n", wr);
            o.msg += b;
            return;
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
    }, err);
    if (!ok) {
        o.msg = "Error: " + err + "\n";
        return o;
    }
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
    printf("%-4s %-20s %-9s %-24s %s\n", "ID", "Address", "Type", "Description", "Enabled");
    for (size_t i = 0; i < s.table().size(); ++i) {
        const AddressEntry& e = *s.table().get(i);
        printf("%-4zu 0x%016llx %-9s %-24s %s%s\n", i,
               (unsigned long long)e.address, type_name(e.type),
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
// actual).
static bool read_entry_print(Memory& mem, const std::vector<Region>& regions,
                             size_t idx, AddressEntry& e) {
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
        out.error = "Uso: pointer scan <direccion> [depth=N] [code]";
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

    PointerScanResult res;
    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        res = pointer_scan(mem, regions, opts);
    }, err);
    if (!ok) {
        printf("Error: %s\n", err.c_str());
        return {};
    }
    s.set_pointer_result(std::move(res));

    const PointerScanResult& r2 = s.pointer_result().value();
    printf("Pointer Scan:\n");
    printf("target: 0x%016llx\n", (unsigned long long)r2.target);
    printf("depth: %d\n", opts.max_depth);
    printf("levels: %d\n", r2.levels);
    printf("chains: %zu\n", r2.chains.size());
    if (r2.edges_truncated || r2.chains_truncated) {
        printf("WARNING: pointer scan truncado\n");
        if (r2.edges_truncated)
            printf("- limite de aristas alcanzado (max_edges_per_level)\n");
        if (r2.chains_truncated)
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
    size_t n = 10;
    if (!args.empty()) n = (size_t)strtoull(args[0].c_str(), nullptr, 10);
    n = std::min(n, res.chains.size());
    for (size_t i = 0; i < n; ++i) {
        printf("[%zu] depth %d:\n", i, res.chains[i].depth);
        printf("%s\n", pointer_chain_description(res.chains[i].nodes).c_str());
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
    const uint64_t root = c.nodes[0];
    const std::string desc = pointer_chain_description(c.nodes);
    const size_t nidx = s.table().add(root, DataType::PTR, desc);
    printf("Entrada %zu anadida desde la cadena %zu:\n", nidx, idx);
    printf("  0x%016llx (pointer)\n", (unsigned long long)root);
    printf("  %s\n", desc.c_str());
    return {};
}

static CommandResult cmd_pointer(const CommandArgs& args, Session& s) {
    if (args.empty()) {
        printf("Uso: pointer scan <direccion> [depth=N] [code] | "
               "pointer results [n] | pointer add <indice>\n");
        return {};
    }
    const std::string& sub = args[0];
    CommandArgs rest(args.begin() + 1, args.end());
    if (sub == "scan") return cmd_pointer_scan(rest, s);
    if (sub == "results" || sub == "chains") return cmd_pointer_results(rest, s);
    if (sub == "add") return cmd_pointer_add(rest, s);
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
     "  first unknown [tipo]                 Primer escaneo (valor desconocido)",
     cmd_first},
    {"next",
     "next <valor> [tipo]                  Refinar resultados (igual)\n"
     "  next changed|unchanged|increased|decreased   Refinar por cambio\n"
     "  next >|<|>=|<=|!= <valor> [tipo]     Refinar por comparacion",
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
     "pointer scan <dir> [depth=N] [code]     Buscar cadenas de punteros hacia una direccion\n"
     "  pointer results [n]                    Mostrar cadenas del ultimo escaneo\n"
     "  pointer chains [n]                     Alias de 'pointer results'\n"
     "  pointer add <idx>                      Anadir la base de una cadena a la tabla",
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
