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

#include "memory.h"
#include "pattern.h"
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
    if (!type_is_float(t)) {
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

// ---------------------------------------------------------------------------
// Handlers

static CommandResult cmd_quit(const CommandArgs&, Session&) {
    return {true, true}; // termina el REPL
}

static CommandResult cmd_help(const CommandArgs&, Session&) {
    printf("Comandos:\n");
    for (const auto& c : commands())
        if (c.usage) printf("  %s\n", c.usage);
    printf("Tipos: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 (alias: int, float, double...)\n");
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

    auto regions = parse_maps(s.pid());
    auto r = region_at(regions, addr);
    if (!r) {
        printf("La direccion 0x%llx no pertenece a ninguna region.\n",
               (unsigned long long)addr);
        return {};
    }
    if (!r->writable()) {
        printf("La region no es escribible (%s).\n", r->perms.c_str());
        return {};
    }

    std::string err;
    bool ok = s.with_memory([&](Memory& mem) {
        const size_t w = type_size(type);
        uint8_t cur[8] = {0};
        ssize_t got = mem.read(addr, cur, w);
        if (got != (ssize_t)w) {
            printf("No se pudo leer el valor actual en 0x%llx\n",
                   (unsigned long long)addr);
            return;
        }
        Value old = value_from_bytes(cur, w);
        printf("Actual: 0x%llx = %s\n", (unsigned long long)addr,
               display_value(old, type).c_str());

        ssize_t wr = mem.write(addr, &v.bits, w);
        if (wr != (ssize_t)w) {
            printf("Error de escritura (%zd bytes escritos)\n", wr);
            return;
        }
        uint8_t ver[8] = {0};
        if (mem.read(addr, ver, w) == (ssize_t)w) {
            Value nv = value_from_bytes(ver, w);
            printf("Nuevo:  0x%llx = %s %s\n", (unsigned long long)addr,
                   display_value(nv, type).c_str(),
                   value_equal(nv, v, type) ? "(verificado)" : "(NO verificado)");
        }
    }, err);
    if (!ok) printf("Error: %s\n", err.c_str());
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
