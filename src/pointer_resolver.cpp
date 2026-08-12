// pointer_resolver.cpp - Implementacion del resolver de PointerChainRef.
#include "pointer_resolver.h"

#include <cstdio>

namespace mt {

PointerBase make_base_from_address(const std::vector<Region>& regions,
                                   uint64_t address) {
    PointerBase b;
    auto r = region_at(regions, address);
    if (r && !r->path.empty() && r->path[0] != '[') {
        b.kind = PointerBaseKind::MODULE;
        b.module = r->path;
        b.offset = r->offset + (address - r->start);
    } else {
        b.kind = PointerBaseKind::ABSOLUTE;
        b.address = address;
    }
    return b;
}

bool resolve_root(const PointerBase& root, const std::vector<Region>& regions,
                  uint64_t& out, std::string& err) {
    if (root.kind == PointerBaseKind::ABSOLUTE) {
        out = root.address;
        return true;
    }
    bool module_exists = false;
    for (const Region& r : regions) {
        if (r.path != root.module) continue;
        module_exists = true;
        if (root.offset >= r.offset && root.offset < r.offset + r.size()) {
            out = r.start + (root.offset - r.offset);
            return true;
        }
    }
    if (!module_exists) {
        err = "modulo no encontrado: " + root.module;
    } else {
        char b[48];
        snprintf(b, sizeof b, "offset fuera del modulo: 0x%llx",
                 (unsigned long long)root.offset);
        err = b;
    }
    return false;
}

ResolveResult follow_chain(const PointerChainRef& chain, uint64_t root_addr,
                           const std::vector<Region>& regions,
                           const std::function<ssize_t(uint64_t, void*, size_t)>& read_fn) {
    ResolveResult r;
    uint64_t addr = root_addr;

    for (size_t i = 0; i < chain.offsets.size(); ++i) {
        auto reg = region_at(regions, addr);
        if (!reg || !reg->readable()) {
            r.error = "cadena rota (direccion no legible)";
            return r;
        }
        uint64_t p = 0;
        ssize_t got = read_fn(addr, &p, sizeof(p));
        if (got != (ssize_t)sizeof(p)) {
            r.error = "puntero no legible";
            return r;
        }
        addr = p + chain.offsets[i];
    }

    const size_t w = type_size(chain.value_type);
    auto reg = region_at(regions, addr);
    if (!reg || !reg->readable()) {
        r.error = "no se pudo leer el valor final (direccion no legible)";
        return r;
    }
    uint8_t buf[8] = {0};
    ssize_t got = read_fn(addr, buf, w);
    if (got != (ssize_t)w) {
        r.error = "no se pudo leer el valor final";
        return r;
    }
    r.ok = true;
    r.address = addr;
    r.value = value_from_bytes(buf, w);
    return r;
}

ResolveResult resolve_chain(const PointerChainRef& chain, Memory& mem,
                            const std::vector<Region>& regions) {
    uint64_t root_addr = 0;
    std::string err;
    if (!resolve_root(chain.root, regions, root_addr, err)) {
        ResolveResult r;
        r.error = err;
        return r;
    }
    auto read_fn = [&](uint64_t a, void* b, size_t n) -> ssize_t {
        return mem.read(a, b, n);
    };
    return follow_chain(chain, root_addr, regions, read_fn);
}

PointerChainRef make_chain_ref(const std::vector<Region>& regions,
                               const std::vector<uint64_t>& nodes,
                               DataType value_type) {
    return make_chain_ref(regions, nodes, std::vector<uint64_t>{}, value_type);
}

PointerChainRef make_chain_ref(const std::vector<Region>& regions,
                               const std::vector<uint64_t>& nodes,
                               const std::vector<uint64_t>& offsets,
                               DataType value_type) {
    PointerChainRef ref;
    ref.value_type = value_type;
    if (nodes.empty()) return ref;
    ref.root = make_base_from_address(regions, nodes[0]);
    const size_t depth = nodes.size() - 1; // derefs; nodes.back() es el target
    if (offsets.size() == depth) {
        ref.offsets = offsets;
    } else {
        ref.offsets.assign(depth, 0); // V1 / datos incompletos
    }
    return ref;
}

} // namespace mt
