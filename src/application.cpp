// application.cpp - Implementacion de la capa de aplicacion headless.
//
// Orquesta las operaciones sobre Session/Core y devuelve resultados
// estructurados (datos + errores). NINGUNA funcion imprime: la presentacion
// (texto de la CLI o widgets de la GUI) vive en el frontend. La futura GUI
// reutilizara esta misma capa en lugar de parsear comandos de texto.
#include "application.h"

#include <cstdio>
#include <string>

#include "address_table.h"
#include "memory.h"
#include "pointer_resolver.h"
#include "scanner.h"
#include "session.h"

namespace mt {

// --- Resultados de escritura (nucleo unico, sin presentacion) --------------

// Escribe 'value' (tipo 'type') en 'addr' con la memoria YA abierta y las
// regiones dadas: comprueba region (existencia + escribible), lee el valor
// actual, escribe y relee para verificar. Devuelve los datos estructurados;
// el frontend formatea el mensaje (Actual:/Nuevo:) a partir de ellos.
// Es el nucleo unico de escritura: lo usan 'set' y 'table set' (absoluta y
// dinamica) sin duplicar logica.
static WriteOutcome write_value_at(Memory& mem, const std::vector<Region>& regions,
                                   uint64_t addr, DataType type,
                                   const Value& value) {
    WriteOutcome o;
    o.address = addr;
    o.type = type;

    auto r = region_at(regions, addr);
    if (!r) {
        char b[128];
        snprintf(b, sizeof b, "La direccion 0x%llx no pertenece a ninguna region.",
                 (unsigned long long)addr);
        o.error = b;
        return o;
    }
    if (!r->writable()) {
        o.error = "La region no es escribible (" + r->perms + ").";
        return o;
    }

    const size_t w = type_size(type);
    uint8_t cur[8] = {0};
    ssize_t got = mem.read(addr, cur, w);
    if (got != (ssize_t)w) {
        char b[128];
        snprintf(b, sizeof b, "No se pudo leer el valor actual en 0x%llx",
                 (unsigned long long)addr);
        o.error = b;
        return o;
    }
    o.had_old = true;
    o.old_value = value_from_bytes(cur, w);

    ssize_t wr = mem.write(addr, &value.bits, w);
    if (wr != (ssize_t)w) {
        char b[128];
        snprintf(b, sizeof b, "Error de escritura (%zd bytes escritos)", wr);
        o.error = b;
        return o;
    }
    uint8_t ver[8] = {0};
    if (mem.read(addr, ver, w) == (ssize_t)w) {
        o.wrote = true;
        o.new_value = value_from_bytes(ver, w);
        o.verified = value_equal(o.new_value, value, type);
        o.ok = true;
    }
    return o;
}

// --- Application ------------------------------------------------------------

AttachOutcome Application::attach(int pid) {
    AttachOutcome o;
    const bool switching = session_.has_pid() && session_.pid() != pid;
    std::string err;
    if (!session_.attach(pid, err)) {
        o.ok = false;
        o.error = err;
        return o;
    }
    o.ok = true;
    o.switched = switching;
    return o;
}

OperationResult Application::detach() {
    session_.detach();
    return {};
}

ScanOutcome Application::first_scan(DataType type,
                                    const std::optional<Value>& target,
                                    const std::atomic<bool>* cancel,
                                    const ProgressFn& progress) {
    ScanOutcome o;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        auto regions = parse_maps(session_.pid());
        bool done =
            session_.scanner().first_scan(mem, regions, type, target, cancel, progress);
        if (!done) o.cancelled = true;
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    if (o.cancelled) return o; // ok=false, count=0: nunca se publica parcial
    o.ok = true;
    o.count = session_.scanner().count();
    o.truncated = session_.scanner().truncated();
    o.warned = session_.scanner().warned();
    return o;
}

ScanOutcome Application::next_scan(DataType type, Filter filter,
                                   const std::optional<Value>& target,
                                   const std::atomic<bool>* cancel,
                                   const ProgressFn& progress) {
    ScanOutcome o;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        bool done = session_.scanner().next_scan(mem, type, filter, target, cancel,
                                                 progress);
        if (!done) o.cancelled = true;
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    if (o.cancelled) return o; // conserva exactamente los candidatos anteriores
    o.ok = true;
    o.count = session_.scanner().count();
    o.truncated = session_.scanner().truncated();
    o.warned = session_.scanner().warned();
    return o;
}

ScanOutcome Application::first_scan_dynamic(const DynamicScanSpec& spec,
                                            const std::atomic<bool>* cancel,
                                            const ProgressFn& progress) {
    ScanOutcome o;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        auto regions = parse_maps(session_.pid());
        bool done = session_.scanner().first_scan_dynamic(mem, regions, spec,
                                                          cancel, progress);
        if (!done) o.cancelled = true;
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    if (o.cancelled) return o;
    o.ok = true;
    o.count = session_.scanner().count();
    o.truncated = session_.scanner().truncated();
    o.warned = session_.scanner().warned();
    return o;
}

ScanOutcome Application::next_scan_dynamic(
    Filter filter, const std::optional<DynamicScanSpec>& newspec,
    const std::atomic<bool>* cancel, const ProgressFn& progress) {
    ScanOutcome o;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        bool done = session_.scanner().next_scan_dynamic(mem, filter, newspec,
                                                         cancel, progress);
        if (!done) o.cancelled = true;
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    if (o.cancelled) return o;
    o.ok = true;
    o.count = session_.scanner().count();
    o.truncated = session_.scanner().truncated();
    o.warned = session_.scanner().warned();
    return o;
}

PatternOutcome Application::pattern_scan(const BytePattern& pat,
                                         const std::atomic<bool>* cancel,
                                         const ProgressFn& progress) {
    PatternOutcome o;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        auto regions = parse_maps(session_.pid());
        auto res = scan_pattern(mem, regions, pat, cancel, progress);
        if (res.cancelled) {
            o.cancelled = true;
            return; // nunca se publican hits parciales
        }
        o.hits = std::move(res.hits);
        o.truncated = res.truncated;
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    if (o.cancelled) return o;
    o.ok = true;
    return o;
}

ReadBytesOutcome Application::read_bytes(uint64_t addr, size_t len) {
    ReadBytesOutcome o;
    o.address = addr;
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        std::vector<uint8_t> buf(len);
        ssize_t got = mem.read(addr, buf.data(), len);
        o.got = got;
        if (got > 0) o.bytes.assign(buf.begin(), buf.begin() + got);
    }, err);
    if (!ok) {
        o.ok = false;
        o.error = err;
        return o;
    }
    o.ok = true;
    return o;
}

InfoOutcome Application::region_info(uint64_t addr) {
    InfoOutcome o;
    auto regions = parse_maps(session_.pid());
    auto r = region_at(regions, addr);
    if (!r) {
        o.ok = false;
        char b[128];
        snprintf(b, sizeof b, "La direccion 0x%llx no pertenece a ninguna region.",
                 (unsigned long long)addr);
        o.error = b;
        return o;
    }
    o.ok = true;
    o.region = *r;
    return o;
}

WriteOutcome Application::write(uint64_t addr, DataType type,
                                const Value& value) {
    WriteOutcome o;
    auto regions = parse_maps(session_.pid());
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        o = write_value_at(mem, regions, addr, type, value);
    }, err);
    if (!ok) {
        o.error = "Error: " + err;
    }
    return o;
}

// --- Address Table ----------------------------------------------------------

size_t Application::add_entry(uint64_t addr, DataType type,
                              const std::string& desc) {
    return session_.table().add(addr, type, desc);
}

size_t Application::add_result_entry(size_t result_index,
                                     const std::string& desc) {
    const auto& res = session_.scanner().results();
    return session_.table().add(res[result_index].addr, session_.scan_type(),
                                desc);
}

bool Application::remove_entry(size_t idx) {
    return session_.table().remove(idx);
}

void Application::clear_entries() {
    session_.table().clear();
}

bool Application::save_table(const std::string& path, std::string& err) {
    return session_.table().save(path, err);
}

bool Application::load_table(const std::string& path, std::string& err) {
    return session_.table().load(path, err);
}

// Lee una entrada con la memoria YA abierta y las regiones dadas. Actualiza
// el estado stale de la entrada (relectura exitosa en el proceso actual).
// Rellena el error de la entrada (sin prefijo de indice; el frontend lo
// formatea). Para entradas dinamicas (kind 'pointer') resuelve la cadena con
// PointerResolver en CADA lectura (nunca se guarda la direccion resuelta).
static void read_entry_into(Memory& mem, const std::vector<Region>& regions,
                            size_t idx, AddressEntry& e, EntryReadOutcome& o) {
    o.index = idx;
    o.type = e.type;
    o.was_stale = e.stale;
    if (e.ptr) {
        ResolveResult r = resolve_chain(*e.ptr, mem, regions);
        if (!r.ok) {
            o.error = r.error;
            return;
        }
        o.ok = true;
        o.address = r.address;
        o.value = r.value;
        o.have_value = true;
        e.stale = false;
        return;
    }
    auto r = region_at(regions, e.address);
    if (!r) {
        char b[128];
        snprintf(b, sizeof b, "0x%016llx: no pertenece a ninguna region.",
                 (unsigned long long)e.address);
        o.error = b;
        return;
    }
    if (!r->readable()) {
        char b[160];
        snprintf(b, sizeof b, "0x%016llx: region no legible (%s).",
                 (unsigned long long)e.address, r->perms.c_str());
        o.error = b;
        return;
    }
    const size_t w = type_size(e.type);
    uint8_t buf[8] = {0};
    ssize_t got = mem.read(e.address, buf, w);
    if (got != (ssize_t)w) {
        char b[160];
        snprintf(b, sizeof b, "0x%016llx: no se pudo leer (%zd bytes).",
                 (unsigned long long)e.address, got);
        o.error = b;
        return;
    }
    o.ok = true;
    o.address = e.address;
    o.value = value_from_bytes(buf, w);
    o.have_value = true;
    e.stale = false;
}

EntryReadOutcome Application::read_entry(size_t idx) {
    EntryReadOutcome o;
    o.index = idx;
    AddressEntry* e = session_.table().get(idx);
    if (!e) {
        o.error = "No existe la entrada " + std::to_string(idx) + ".";
        return o;
    }
    if (!e->enabled) {
        o.error = "La entrada " + std::to_string(idx) +
                  " esta desactivada (usa 'table toggle " + std::to_string(idx) +
                  "').";
        return o;
    }
    o.attempted = true;
    auto regions = parse_maps(session_.pid());
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        read_entry_into(mem, regions, idx, *e, o);
    }, err);
    if (!ok) {
        o.error = err;
    }
    return o;
}

void Application::read_all_entries(std::vector<EntryReadOutcome>& out) {
    auto regions = parse_maps(session_.pid());
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        for (size_t i = 0; i < session_.table().size(); ++i) {
            AddressEntry* e = session_.table().get(i);
            if (!e || !e->enabled) continue;
            EntryReadOutcome o;
            read_entry_into(mem, regions, i, *e, o);
            out.push_back(std::move(o));
        }
    }, err);
    if (!ok) {
        EntryReadOutcome o;
        o.error = err;
        out.push_back(std::move(o));
    }
}

WriteOutcome Application::write_entry(size_t idx, const Value& value) {
    WriteOutcome o;
    AddressEntry* e = session_.table().get(idx);
    if (!e) {
        o.error = "No existe la entrada " + std::to_string(idx) + ".";
        return o;
    }
    if (!e->enabled) {
        o.error = "La entrada " + std::to_string(idx) +
                  " esta desactivada (usa 'table toggle " + std::to_string(idx) +
                  "').";
        return o;
    }
    const std::vector<Region> regions = parse_maps(session_.pid());
    if (e->ptr) {
        // Entrada dinamica: resolver la cadena y escribir en la direccion
        // resultante (mismo mecanismo que 'set', sin duplicar logica).
        std::string err;
        bool ok = session_.with_memory([&](Memory& mem) {
            ResolveResult r = resolve_chain(*e->ptr, mem, regions);
            if (!r.ok) {
                o.error = "Error al resolver la entrada " + std::to_string(idx) +
                          ": " + r.error;
                return;
            }
            o = write_value_at(mem, regions, r.address, e->type, value);
        }, err);
        if (!ok) o.error = "Error: " + err;
        if (o.ok) e->stale = false;
        return o;
    }
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        o = write_value_at(mem, regions, e->address, e->type, value);
    }, err);
    if (!ok) o.error = "Error: " + err;
    if (o.ok) e->stale = false;
    return o;
}

AddressEntry* Application::entry(size_t idx) {
    return session_.table().get(idx);
}

// --- Pointer Scanner --------------------------------------------------------

PointerScanOutcome Application::pointer_scan(const PointerScanInput& in,
                                             const std::atomic<bool>* cancel,
                                             const ProgressFn& progress) {
    PointerScanOutcome o;
    const std::vector<Region> regions = parse_maps(session_.pid());
    auto r = region_at(regions, in.opts.target);
    if (!r) {
        char b[128];
        snprintf(b, sizeof b, "La direccion 0x%llx no pertenece a ninguna region.",
                 (unsigned long long)in.opts.target);
        o.error = b;
        return o;
    }
    if (!r->readable()) {
        char b[128];
        snprintf(b, sizeof b, "La direccion 0x%llx esta en una region no legible (%s).",
                 (unsigned long long)in.opts.target, r->perms.c_str());
        o.error = b;
        return o;
    }

    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        o.result = mt::pointer_scan(mem, regions, in.opts, cancel, progress);
    }, err);
    if (!ok) {
        o.error = err;
        return o;
    }
    if (o.result.cancelled) {
        // No se publica nada: ni session_.pointer_result() ni filtros. El
        // resultado anterior de la sesion queda intacto.
        o.cancelled = true;
        return o;
    }
    o.result.value_type = in.value_type;

    if (in.module_only) {
        // Filtrar: solo cadenas cuya raiz (nodes[0]) esta en una region con
        // pathname de archivo (MODULE, persistente frente a ASLR).
        std::vector<PointerChain> kept;
        for (auto& c : o.result.chains) {
            if (c.nodes.empty()) continue;
            const PointerBase b = make_base_from_address(regions, c.nodes[0]);
            if (b.kind == PointerBaseKind::MODULE)
                kept.push_back(std::move(c));
        }
        o.result.chains = std::move(kept);
    }
    session_.set_pointer_result(o.result);
    o.ok = true;
    return o;
}

AddChainOutcome Application::add_pointer_chain(size_t chain_index,
                                               const std::string& description) {
    AddChainOutcome o;
    const auto& res = session_.pointer_result().value();
    const PointerChain& c = res.chains[chain_index];
    std::vector<Region> regions;
    if (session_.has_pid()) regions = parse_maps(session_.pid());
    o.ref = make_chain_ref(regions, c.nodes, c.offsets, res.value_type);
    o.table_index = session_.table().add(o.ref, description);
    o.ok = true;
    return o;
}

ResolveEntryOutcome Application::resolve_entry(size_t idx) {
    ResolveEntryOutcome o;
    AddressEntry* e = session_.table().get(idx);
    if (!e) {
        o.error = "No existe la entrada " + std::to_string(idx) + ".";
        return o;
    }
    if (!e->ptr) {
        o.error = "La entrada " + std::to_string(idx) +
                  " no es una cadena dinamica (pointer).";
        return o;
    }
    const std::vector<Region> regions = parse_maps(session_.pid());
    std::string err;
    bool ok = session_.with_memory([&](Memory& mem) {
        ResolveResult r = resolve_chain(*e->ptr, mem, regions);
        if (!r.ok) {
            o.error = r.error;
            return;
        }
        e->stale = false;
        o.ok = true;
        o.address = r.address;
        o.value = r.value;
        o.type = e->type;
    }, err);
    if (!ok) {
        o.error = err;
    }
    return o;
}

// --- Parsing reutilizable de 'pointer scan' --------------------------------

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

    // IMP-2 (auditoria de estabilizacion): limite combinado de la ventana de
    // offsets. floor(max_offset/offset_step)+1 offsets por objetivo; una
    // combinacion como max_offset=0x10000 con offset_step=1 generaria 65.537
    // posiciones por objetivo y, al multiplicarse por las fuentes del
    // siguiente nivel, puede agotar la RAM del Chromebook (sin swap).
    // (offset_step ya es >= 1 aqui: el 0 se rechazo antes.)
    const uint64_t n_offsets = out.max_offset / out.offset_step + 1;
    if (n_offsets > kMaxOffsetShifts) {
        out.error = "La ventana de offsets es demasiado grande: " +
                    std::to_string(n_offsets) + " offsets por objetivo "
                    "(maximo " + std::to_string(kMaxOffsetShifts) +
                    "). Reduce max_offset o aumenta offset_step.";
        return out;
    }
    return out;
}

} // namespace mt
