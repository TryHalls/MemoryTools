// pointer.cpp - Implementacion del Pointer Scanner (level-scan inverso).
//
// Algoritmo (un nivel por iteracion):
//   1. Escanear las regiones fuente por posiciones alineadas a 8 bytes;
//      si el valor leido pertenece a current_set (hash plano), se guarda la
//      arista (source=addr, target=valor).
//   2. Ordenar las aristas por target y extender la frontera: cada cadena
//      cuyo head es el target de una arista se antepone [source] + chain.
//   3. current_set para el siguiente nivel = las fuentes de este nivel.
//
// RAM: solo vive UNA estructura hash (current_set) y las aristas del nivel
// actual; las cadenas completas se acumulan en la frontera (acotada por
// max_chains). Sin visited global: los ciclos se cortan por cadena.
#include "pointer.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "chunk.h"

namespace mt {

RegionKind classify_region(const Region& r) {
    if (!r.readable()) return RegionKind::OTHER;
    if (r.path == "[heap]") return RegionKind::HEAP;
    if (r.path.rfind("[stack", 0) == 0) return RegionKind::STACK;
    if (r.path == "[vdso]" || r.path == "[vsyscall]" || r.path == "[vvar]")
        return RegionKind::OTHER;
    if (r.path.empty())
        return r.writable() ? RegionKind::ANON_RW : RegionKind::OTHER;
    if (r.executable()) return RegionKind::CODE;
    if (r.writable()) return RegionKind::DATA; // data/bss con archivo (principal o libs)
    return RegionKind::OTHER;                  // archivos de solo lectura
}

std::vector<Region> select_pointer_regions(const std::vector<Region>& all,
                                           bool include_code) {
    std::vector<Region> out;
    for (const Region& r : all) {
        RegionKind k = classify_region(r);
        if (k == RegionKind::HEAP || k == RegionKind::STACK ||
            k == RegionKind::ANON_RW || k == RegionKind::DATA) {
            out.push_back(r);
        } else if (include_code && k == RegionKind::CODE) {
            out.push_back(r);
        }
    }
    return out;
}

// --- FlatHashSet -----------------------------------------------------------

static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

void FlatHashSet::build(std::vector<uint64_t> values) {
    slots_.clear();
    size_ = 0;
    mask_ = 0;
    if (values.empty()) return;
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    size_t cap = 8;
    while (cap < values.size() * 2) cap <<= 1; // factor de carga <= 0.5
    slots_.assign(cap, 0);
    mask_ = cap - 1;
    for (uint64_t v : values) insert(v);
}

void FlatHashSet::insert(uint64_t v) {
    size_t i = (size_t)mix64(v) & mask_;
    while (slots_[i] != 0 && slots_[i] != v) i = (i + 1) & mask_;
    if (slots_[i] == 0) ++size_;
    slots_[i] = v;
}

bool FlatHashSet::contains(uint64_t v) const {
    if (slots_.empty()) return false;
    size_t i = (size_t)mix64(v) & mask_;
    while (slots_[i] != 0) {
        if (slots_[i] == v) return true;
        i = (i + 1) & mask_;
    }
    return false;
}

// --- Frontera ---------------------------------------------------------------

static bool chain_contains(const std::vector<uint64_t>& nodes, uint64_t v) {
    for (uint64_t n : nodes)
        if (n == v) return true;
    return false;
}

std::vector<PointerChain> extend_chains(const std::vector<PointerChain>& frontier,
                                        const std::vector<PointerEdge>& edges_sorted,
                                        size_t max_chains, bool& chains_truncated) {
    chains_truncated = false;
    std::vector<PointerChain> out;
    if (max_chains == 0) {
        chains_truncated = true;
        return out;
    }

    for (const PointerChain& chain : frontier) {
        if (out.size() >= max_chains) {
            chains_truncated = true;
            break;
        }
        if (chain.nodes.empty()) continue;
        const uint64_t head = chain.nodes[0];

        // Grupo de aristas con target == head (edges ordenadas por target).
        auto it = std::lower_bound(edges_sorted.begin(), edges_sorted.end(), head,
                                   [](const PointerEdge& e, uint64_t t) {
                                       return e.target < t;
                                   });
        for (; it != edges_sorted.end() && it->target == head; ++it) {
            if (out.size() >= max_chains) {
                chains_truncated = true;
                break;
            }
            const uint64_t s = it->source;
            // Ciclo: no reinsertar un nodo ya presente en ESTA cadena.
            if (chain_contains(chain.nodes, s)) continue;
            PointerChain nc;
            nc.nodes.reserve(chain.nodes.size() + 1);
            nc.nodes.push_back(s);
            nc.nodes.insert(nc.nodes.end(), chain.nodes.begin(), chain.nodes.end());
            nc.depth = (int)nc.nodes.size() - 1;
            out.push_back(std::move(nc));
        }
        if (chains_truncated) break;
    }
    return out;
}

// --- Escaneo ----------------------------------------------------------------

PointerScanResult pointer_scan(Memory& mem, const std::vector<Region>& regions,
                               const PointerScanOptions& opts) {
    PointerScanResult res;
    res.target = opts.target;
    if (opts.target == 0 || opts.max_depth <= 0) return res;

    std::vector<Region> source = select_pointer_regions(regions, opts.include_code);
    if (opts.min_addr || opts.max_addr) {
        std::vector<Region> filtered;
        for (Region r : source) { // copia para recortar al rango
            if (opts.max_addr && r.start >= opts.max_addr) continue;
            if (opts.min_addr && r.end <= opts.min_addr) continue;
            if (opts.min_addr && r.start < opts.min_addr) r.start = opts.min_addr;
            if (opts.max_addr && r.end > opts.max_addr) r.end = opts.max_addr;
            if (r.end > r.start) filtered.push_back(r);
        }
        source = std::move(filtered);
    }

    FlatHashSet current_set(std::vector<uint64_t>{opts.target});

    std::vector<PointerChain> frontier;
    frontier.push_back(PointerChain{{opts.target}, 0});

    for (int d = 1; d <= opts.max_depth; ++d) {
        // 1) Escanear regiones fuente: posiciones alineadas a 8 bytes cuyo
        //    valor pertenece a current_set.
        std::vector<PointerEdge> edges;
        bool stopped = false;
        auto cb = [&](const uint8_t* win, uint64_t addr) -> bool {
            uint64_t v;
            std::memcpy(&v, win, sizeof(v));
            if (current_set.contains(v)) {
                edges.push_back(PointerEdge{addr, v});
                if (edges.size() >= opts.max_edges_per_level) {
                    res.edges_truncated = true;
                    stopped = true;
                    return false; // detener todo el recorrido
                }
            }
            return true;
        };
        for_each_window(mem, source, 8, 8, cb);
        if (edges.empty()) break; // sin referencias nuevas: fin del escaneo
        res.levels = d;
        res.total_edges += edges.size();

        // 2) Indice por target para la extension de la frontera.
        std::sort(edges.begin(), edges.end(),
                  [](const PointerEdge& a, const PointerEdge& b) {
                      if (a.target != b.target) return a.target < b.target;
                      return a.source < b.source;
                  });

        // 3) Extender la frontera un nivel hacia atras.
        bool ctrunc = false;
        std::vector<PointerChain> next =
            extend_chains(frontier, edges, opts.max_chains, ctrunc);
        if (ctrunc) res.chains_truncated = true;
        if (next.empty()) break;

        res.chains.insert(res.chains.end(), next.begin(), next.end());
        frontier = std::move(next);

        // 4) Siguiente nivel: S_d = fuentes de este nivel.
        std::vector<uint64_t> srcs;
        srcs.reserve(edges.size());
        for (const PointerEdge& e : edges) srcs.push_back(e.source);
        current_set.build(std::move(srcs));

        if (stopped) break; // el nivel se trunco: no tiene sentido continuar
    }
    return res;
}

} // namespace mt
