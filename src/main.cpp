// main.cpp - Interfaz de terminal de MemoryTool.
//
// Uso:
//   memorytool            sesion interactiva
//   memorytool <pid>      sesion interactiva con proceso objetivo
//   memorytool list       listar procesos
//   memorytool maps <pid> mostrar regiones de memoria
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "memory.h"
#include "pattern.h"
#include "process.h"
#include "scanner.h"
#include "types.h"

using namespace mt;

// ---------------------------------------------------------------------------
// Utilidades

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

// ---------------------------------------------------------------------------
// Comandos

static void print_banner() {
    std::cout
        << "MemoryTool 0.1 - Analizador de memoria para Linux/ChromeOS\n"
        << "Motor propio, sin dependencias externas. Uso educativo y de depuracion.\n\n";
}

static void print_usage() {
    std::cout
        << "Uso:\n"
        << "  memorytool                     Sesion interactiva\n"
        << "  memorytool <pid>               Sesion interactiva con proceso objetivo\n"
        << "  memorytool list                Listar procesos\n"
        << "  memorytool maps <pid>          Mostrar regiones de memoria\n"
        << "  memorytool help                Esta ayuda\n";
}

static void print_help() {
    std::cout
        << "Comandos:\n"
        << "  list                                 Listar procesos\n"
        << "  attach <pid|nombre>                  Seleccionar proceso objetivo\n"
        << "  detach                               Quitar proceso objetivo\n"
        << "  maps [pid]                           Mostrar regiones de memoria\n"
        << "  first <valor> [tipo]                 Primer escaneo (valor exacto)\n"
        << "  first unknown [tipo]                 Primer escaneo (valor desconocido)\n"
        << "  next <valor> [tipo]                  Refinar resultados (igual)\n"
        << "  next changed|unchanged|increased|decreased   Refinar por cambio\n"
        << "  next >|<|>=|<=|!= <valor> [tipo]     Refinar por comparacion\n"
        << "  count                                Numero de coincidencias\n"
        << "  results [n]                          Mostrar las primeras n coincidencias\n"
        << "  pattern <bytes>                      Buscar secuencia de bytes (AOB)\n"
        << "                                       Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0\n"
        << "  view <direccion> [len]               Visor hexadecimal\n"
        << "  set <direccion> <valor> [tipo]       Escribir valor en memoria\n"
        << "  info <direccion>                     Informacion de la region\n"
        << "  help | quit\n"
        << "Tipos: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 (alias: int, float, double...)\n";
}

static void do_list() {
    auto procs = list_processes();
    if (procs.empty()) {
        printf("No se pudo listar /proc\n");
        return;
    }
    printf("%-7s %-12s %-6s %-9s %s\n", "PID", "USUARIO", "EST", "RSS(KB)", "NOMBRE");
    for (const auto& p : procs) {
        printf("%-7d %-12s %-6c %-9ld %s%s\n", p.pid, p.user.c_str(), p.state,
               p.rss_kb, p.name.c_str(), p.accessible ? "" : "  [no accesible]");
    }
    printf("\nSolo los procesos del mismo usuario (UID %d) que concedan ptrace son accesibles.\n",
           (int)geteuid());
}

static void do_maps(int pid) {
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

static std::optional<int> resolve_target(const std::string& s) {
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

// Ejecuta fn con la memoria del proceso abierta (attach + detach automatico).
static bool with_memory(int pid, const std::function<void(Memory&)>& fn,
                        std::string& err) {
    Memory mem;
    if (!mem.open(pid, err)) return false;
    fn(mem);
    mem.close();
    return true;
}

// ---------------------------------------------------------------------------
// Sesion interactiva

static void run_repl(std::optional<int> initial_pid) {
    std::optional<int> pid = initial_pid;
    Scanner scanner;
    DataType scan_type = DataType::I32;

    if (pid) {
        std::string err;
        Memory probe;
        if (!probe.open(*pid, err)) {
            printf("Aviso: no se pudo acceder al proceso %d: %s\n", *pid, err.c_str());
            pid.reset();
        } else {
            probe.close();
            printf("Proceso objetivo: %d\n", *pid);
        }
    }

    while (true) {
        if (pid)
            printf("mt(%d)> ", *pid);
        else
            printf("mt> ");
        fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) break;
        auto t = split(line);
        if (t.empty()) continue;
        const std::string& cmd = t[0];

        if (cmd == "quit" || cmd == "exit") break;
        if (cmd == "help" || cmd == "?") { print_help(); continue; }
        if (cmd == "list") { do_list(); continue; }

        if (cmd == "attach") {
            if (t.size() < 2) {
                printf("Uso: attach <pid|nombre>\n");
                continue;
            }
            auto target = resolve_target(t[1]);
            if (!target) continue;
            std::string err;
            Memory probe;
            if (!probe.open(*target, err)) {
                printf("No se pudo acceder al proceso %d: %s\n", *target, err.c_str());
                continue;
            }
            probe.close();
            if (pid && *pid != *target) {
                scanner.clear(); // los resultados pertenecen al PID anterior
                printf("Resultados anteriores descartados (cambio de proceso).\n");
            }
            pid = target;
            printf("Proceso objetivo: %d\n", *pid);
            continue;
        }
        if (cmd == "detach") {
            pid.reset();
            scanner.clear();
            printf("Proceso objetivo eliminado.\n");
            continue;
        }

        if (cmd == "maps") {
            int target_pid = 0;
            if (t.size() >= 2) {
                auto r = resolve_target(t[1]);
                if (!r) continue;
                target_pid = *r;
            } else if (pid) {
                target_pid = *pid;
            } else {
                printf("Primero selecciona un proceso (attach <pid>).\n");
                continue;
            }
            do_maps(target_pid);
            continue;
        }

        if (cmd == "first") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (t.size() < 2) { printf("Uso: first <valor> [tipo] | first unknown [tipo]\n"); continue; }

            DataType type = DataType::I32;
            std::optional<Value> target;
            if (t[1] == "unknown") {
                if (t.size() >= 3 && parse_type(t[2], type)) {}
            } else {
                if (t.size() >= 3 && parse_type(t.back(), type)) {}
                Value v;
                if (!parse_value(t[1], type, v)) {
                    printf("Valor invalido: %s (tipo %s)\n", t[1].c_str(), type_name(type));
                    continue;
                }
                target = v;
            }
            scan_type = type;

            std::string err;
            bool ok = with_memory(*pid, [&](Memory& mem) {
                auto regions = parse_maps(*pid);
                scanner.first_scan(mem, regions, type, target);
            }, err);
            if (!ok) { printf("Error: %s\n", err.c_str()); continue; }

            if (!target)
                printf("Escaneo 'unknown' completado (%zu posiciones legibles).\n", scanner.count());
            else
                printf("First Scan: %zu coincidencias (%s = %s)\n", scanner.count(),
                       type_name(type), display_value(*target, type).c_str());
            if (scanner.truncated())
                printf("AVISO: se alcanzo el limite de candidatos; el resultado esta truncado.\n");
            if (scanner.warned())
                printf("AVISO: la lista de candidatos es muy grande; el escaneo puede consumir mucha RAM.\n");
            continue;
        }

        if (cmd == "next") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (!scanner.has_results()) {
                printf("No hay resultados previos; usa 'first' primero.\n");
                continue;
            }
            if (t.size() < 2) { printf("Uso: next <valor> [tipo] | next changed|... | next <op> <valor> [tipo]\n"); continue; }

            DataType type = scan_type;
            Filter filter = Filter::EXACT;
            std::optional<Value> target;

            const std::string& tok = t[1];
            if (tok == "changed") {
                filter = Filter::CHANGED;
            } else if (tok == "unchanged") {
                filter = Filter::UNCHANGED;
            } else if (tok == "increased" || tok == "increase") {
                filter = Filter::INCREASED;
            } else if (tok == "decreased" || tok == "decrease") {
                filter = Filter::DECREASED;
            } else if (tok == ">" || tok == "<" || tok == ">=" || tok == "<=" || tok == "!=" || tok == "=") {
                if (t.size() < 3) { printf("Falta el valor de comparacion.\n"); continue; }
                if (t.size() >= 4 && parse_type(t.back(), type)) {}
                filter = (tok == ">") ? Filter::GREATER
                       : (tok == "<") ? Filter::LESS
                       : (tok == ">=") ? Filter::GE
                       : (tok == "<=") ? Filter::LE
                       : (tok == "!=") ? Filter::NE
                                       : Filter::EXACT;
                Value v;
                if (!parse_value(t[2], type, v)) {
                    printf("Valor invalido: %s (tipo %s)\n", t[2].c_str(), type_name(type));
                    continue;
                }
                target = v;
            } else {
                if (t.size() >= 3 && parse_type(t.back(), type)) {}
                Value v;
                if (!parse_value(tok, type, v)) {
                    printf("Valor invalido: %s (tipo %s)\n", tok.c_str(), type_name(type));
                    continue;
                }
                target = v;
            }
            scan_type = type;

            std::string err;
            bool ok = with_memory(*pid, [&](Memory& mem) {
                scanner.next_scan(mem, type, filter, target);
            }, err);
            if (!ok) { printf("Error: %s\n", err.c_str()); continue; }

            printf("Next Scan: %zu coincidencias\n", scanner.count());
            continue;
        }

        if (cmd == "count") {
            if (!scanner.has_results())
                printf("No hay resultados previos.\n");
            else
                printf("%zu coincidencias\n", scanner.count());
            continue;
        }

        if (cmd == "results") {
            if (!scanner.has_results()) {
                printf("No hay resultados previos.\n");
                continue;
            }
            size_t n = 20;
            if (t.size() >= 2) n = (size_t)strtoull(t[1].c_str(), nullptr, 10);
            const auto& res = scanner.results();
            n = std::min(n, res.size());
            for (size_t i = 0; i < n; ++i) {
                printf("[%4zu] 0x%016llx = %s (%s)\n", i,
                       (unsigned long long)res[i].addr,
                       display_value(res[i].prev, scan_type).c_str(),
                       type_name(scan_type));
            }
            if (n < res.size())
                printf("... y %zu mas. Usa 'results %zu' para verlas todas.\n",
                       res.size() - n, res.size());
            continue;
        }

        if (cmd == "pattern" || cmd == "aob") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (t.size() < 2) { printf("Uso: pattern <bytes>  (Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0)\n"); continue; }

            std::string pat_text;
            for (size_t i = 1; i < t.size(); ++i) {
                if (i > 1) pat_text += ' ';
                pat_text += t[i];
            }
            BytePattern pat;
            if (!parse_pattern(pat_text, pat)) {
                printf("Patron invalido: %s\n", pat.error.c_str());
                continue;
            }

            std::string err;
            bool ok = with_memory(*pid, [&](Memory& mem) {
                auto regions = parse_maps(*pid);
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
            continue;
        }

        if (cmd == "view" || cmd == "hexdump") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (t.size() < 2) { printf("Uso: view <direccion> [len]\n"); continue; }
            uint64_t addr = 0;
            if (!parse_addr(t[1], addr)) { printf("Direccion invalida: %s\n", t[1].c_str()); continue; }
            size_t len = 64;
            if (t.size() >= 3) len = (size_t)strtoull(t[2].c_str(), nullptr, 10);
            len = std::min<size_t>(len, 4096);

            std::string err;
            bool ok = with_memory(*pid, [&](Memory& mem) {
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
            continue;
        }

        if (cmd == "set" || cmd == "write") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (t.size() < 3) { printf("Uso: set <direccion> <valor> [tipo]\n"); continue; }
            uint64_t addr = 0;
            if (!parse_addr(t[1], addr)) { printf("Direccion invalida: %s\n", t[1].c_str()); continue; }
            DataType type = DataType::I32;
            if (t.size() >= 4 && parse_type(t[3], type)) {}
            Value v;
            if (!parse_value(t[2], type, v)) {
                printf("Valor invalido: %s (tipo %s)\n", t[2].c_str(), type_name(type));
                continue;
            }

            auto regions = parse_maps(*pid);
            auto r = region_at(regions, addr);
            if (!r) {
                printf("La direccion 0x%llx no pertenece a ninguna region.\n",
                       (unsigned long long)addr);
                continue;
            }
            if (!r->writable()) {
                printf("La region no es escribible (%s).\n", r->perms.c_str());
                continue;
            }

            std::string err;
            bool ok = with_memory(*pid, [&](Memory& mem) {
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
            continue;
        }

        if (cmd == "info") {
            if (!pid) { printf("Primero selecciona un proceso (attach <pid>).\n"); continue; }
            if (t.size() < 2) { printf("Uso: info <direccion>\n"); continue; }
            uint64_t addr = 0;
            if (!parse_addr(t[1], addr)) { printf("Direccion invalida: %s\n", t[1].c_str()); continue; }
            auto regions = parse_maps(*pid);
            auto r = region_at(regions, addr);
            if (!r) {
                printf("La direccion 0x%llx no pertenece a ninguna region.\n",
                       (unsigned long long)addr);
                continue;
            }
            printf("Direccion:    0x%llx\n", (unsigned long long)addr);
            printf("Region:       0x%llx - 0x%llx (%llu bytes)\n",
                   (unsigned long long)r->start, (unsigned long long)r->end,
                   (unsigned long long)r->size());
            printf("Permisos:     %s\n", r->perms.c_str());
            printf("Offset:       0x%llx\n", (unsigned long long)r->offset);
            printf("Archivo:      %s\n", r->path.empty() ? "(anonimo)" : r->path.c_str());
            continue;
        }

        printf("Comando desconocido: %s (usa 'help')\n", cmd.c_str());
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // salida inmediata (prompts/scripts)
    const char* arg1 = argc > 1 ? argv[1] : nullptr;

    if (argc >= 2 && (std::string(arg1) == "help" || std::string(arg1) == "--help" || std::string(arg1) == "-h")) {
        print_banner();
        print_usage();
        print_help();
        return 0;
    }
    if (argc >= 2 && std::string(arg1) == "list") {
        do_list();
        return 0;
    }
    if (argc >= 3 && std::string(arg1) == "maps") {
        auto r = resolve_target(argv[2]);
        if (!r) return 1;
        do_maps(*r);
        return 0;
    }

    if (argc >= 2 && std::string(arg1) == "attach") {
        if (argc < 3) {
            printf("Uso: memorytool attach <pid|nombre>\n");
            return 1;
        }
        auto r = resolve_target(argv[2]);
        if (!r) return 1;
        print_banner();
        run_repl(r);
        return 0;
    }

    if (argc >= 2) {
        auto r = resolve_target(arg1);
        if (!r) return 1;
        print_banner();
        run_repl(r);
        return 0;
    }

    print_banner();
    run_repl(std::nullopt);
    return 0;
}
