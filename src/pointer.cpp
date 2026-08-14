// pointer.cpp - Implementacion de las partes no-template del Pointer
// Scanner: clasificacion de regiones, FlatHashSet y la frontera incremental.
// El escaneo completo (pointer_scan, con cancelacion y progreso) vive en
// pointer.h como template (igual que for_each_window y scan_pattern) para
// poder testearlo con memoria fake sin proceso real.
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
#include <utility>

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
            // El offset de la arista se antepone: se aplica despues del deref
            // del nuevo head para alcanzar el anterior.
            nc.offsets.reserve(chain.offsets.size() + 1);
            nc.offsets.push_back(it->offset);
            nc.offsets.insert(nc.offsets.end(), chain.offsets.begin(),
                              chain.offsets.end());
            nc.depth = (int)nc.nodes.size() - 1;
            out.push_back(std::move(nc));
        }
        if (chains_truncated) break;
    }
    return out;
}

} // namespace mt
