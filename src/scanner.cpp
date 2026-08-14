// scanner.cpp - Implementacion del escaneo progresivo.
//
// Estrategia:
//  - first_scan recorre las regiones legibles por bloques y guarda cada
//    posicion de byte que cumple el criterio. Las posiciones quedan
//    ordenadas por direccion (las regiones vienen ordenadas en /proc/maps).
//  - next_scan re-lee la memoria por bloques (en lugar de un pread por
//    direccion), comparando contra el valor anterior guardado. Asi un
//    "first unknown" seguido de "next changed" es rapido aunque haya
//    millones de candidatos.
//
// Cancelacion: todos los escaneos aceptan un flag atomico opcional. Si se
// solicita cancelacion, el recorrido se detiene y se devuelve false SIN
// publicar ningun resultado parcial: el escaneo se construye sobre vectores
// locales y solo se sustituye el estado del Scanner al terminar
// correctamente. Los resultados anteriores quedan intactos.
#include "scanner.h"

#include <algorithm>
#include <cstring>

#include "chunk.h"

namespace mt {

void Scanner::clear() {
    candidates_.clear();
    dyn_candidates_.clear();
    dyn_spec_.reset();
    truncated_ = false;
    warned_ = false;
}

bool Scanner::first_scan_dynamic(Memory& mem,
                                 const std::vector<Region>& regions,
                                 const DynamicScanSpec& spec,
                                 const std::atomic<bool>* cancel,
                                 const ProgressFn& progress) {
    const size_t L = spec.length();
    if (L == 0 || !spec.pattern.valid) {
        // Comportamiento identico al anterior: se fija la especificacion y se
        // dejan los resultados dinamicos vacios (no hay nada que escanear).
        dyn_spec_ = spec;
        dyn_candidates_.clear();
        candidates_.clear();
        truncated_ = false;
        warned_ = false;
        return true;
    }

    bool trunc = false;
    bool warn = false;
    std::vector<DynamicCandidate> found;

    auto emit = [&](uint64_t addr, const uint8_t* win) {
        if (found.size() >= kMaxCandidates) {
            trunc = true;
            return false;
        }
        if (!warn && found.size() >= kWarnCandidates) warn = true;
        DynamicCandidate c;
        c.addr = addr;
        if (!spec.wild_pos.empty()) {
            c.prev_wild.reserve(spec.wild_pos.size());
            for (size_t p : spec.wild_pos) c.prev_wild.push_back(win[p]);
        }
        found.push_back(std::move(c));
        return true;
    };

    // Busqueda no alineada por bloques con solapamiento (los patrones que
    // cruzan el limite de bloque se encuentran gracias al overlap).
    for_each_window(mem, regions, L, 1, [&](const uint8_t* win, uint64_t addr) {
        if (!pattern_window_matches(win, spec.pattern)) return true;
        return emit(addr, win);
    }, cancel, progress);

    if (cancel && cancel->load(std::memory_order_relaxed)) return false;

    if (progress && !trunc) {
        const uint64_t t = readable_total(regions);
        progress(t, t);
    }

    dyn_candidates_.swap(found);
    dyn_spec_ = spec;
    truncated_ = trunc;
    warned_ = warn;
    candidates_.clear();
    return true;
}

bool Scanner::next_scan_dynamic(Memory& mem, Filter filter,
                                const std::optional<DynamicScanSpec>& newspec,
                                const std::atomic<bool>* cancel,
                                const ProgressFn& progress) {
    if (!dyn_spec_) return true;
    const DynamicScanSpec& base = newspec ? *newspec : *dyn_spec_;
    const size_t L = base.length();
    if (L == 0 || !base.pattern.valid) return true;
    const BytePattern& pat = base.pattern;
    const bool compare_prev =
        (filter == Filter::CHANGED || filter == Filter::UNCHANGED);

    if (!std::is_sorted(dyn_candidates_.begin(), dyn_candidates_.end(),
                        [](const DynamicCandidate& a, const DynamicCandidate& b) {
                            return a.addr < b.addr;
                        })) {
        std::sort(dyn_candidates_.begin(), dyn_candidates_.end(),
                  [](const DynamicCandidate& a, const DynamicCandidate& b) {
                      return a.addr < b.addr;
                  });
    }

    std::vector<DynamicCandidate> next;
    next.reserve(dyn_candidates_.size());
    std::vector<uint8_t> buf(kChunk + L);

    size_t i = 0;
    const size_t n = dyn_candidates_.size();
    while (i < n) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        const uint64_t chunk_start = dyn_candidates_[i].addr;
        ssize_t got = mem.read(chunk_start, buf.data(), kChunk + L - 1);
        if (got <= 0) {
            // El bloque no es legible (proceso re-ejecutado, region
            // desmapeada): se descarta este candidato y se reintenta con el
            // siguiente.
            ++i;
            continue;
        }
        const size_t avail = (size_t)got;
        // Si la lectura fue parcial (cruzo un hueco hacia otra region), el
        // limite del bloque es lo realmente leido: los candidatos mas alla
        // quedan para un bloque nuevo que empiece en el siguiente (asi no se
        // pierden candidatos de regiones separadas por un hueco).
        const uint64_t limit =
            chunk_start + std::min<size_t>(kChunk, avail);

        while (i < n && dyn_candidates_[i].addr < limit) {
            DynamicCandidate& c = dyn_candidates_[i];
            const size_t off = (size_t)(c.addr - chunk_start);
            if (off + L > avail) {
                // Ya no es legible (proceso re-ejecutado, region desmapeada).
                ++i;
                continue;
            }
            const uint8_t* win = buf.data() + off;
            bool keep = false;
            if (compare_prev) {
                const bool changed = dynamic_window_changed(win, base, c.prev_wild);
                keep = (filter == Filter::CHANGED) ? changed : !changed;
            } else {
                // EXACT contra el patron nuevo (newspec obligatorio).
                keep = pattern_window_matches(win, pat);
            }
            if (keep) {
                // El valor 'anterior' pasa a ser el actual: recapturar los
                // bytes de las posiciones '?' (mismo criterio que el escaner
                // numerico, que guarda c.prev = cur).
                if (!base.wild_pos.empty()) {
                    c.prev_wild.clear();
                    c.prev_wild.reserve(base.wild_pos.size());
                    for (size_t p : base.wild_pos) c.prev_wild.push_back(win[p]);
                } else {
                    c.prev_wild.clear();
                }
                next.push_back(std::move(c));
            }
            ++i;
        }
        if (progress) progress((uint64_t)i * L, (uint64_t)n * L);
    }
    if (cancel && cancel->load(std::memory_order_relaxed)) return false;

    if (progress) progress((uint64_t)n * L, (uint64_t)n * L);

    dyn_candidates_.swap(next);
    if (newspec) dyn_spec_ = *newspec;
    return true;
}

bool Scanner::first_scan(Memory& mem, const std::vector<Region>& regions,
                         DataType type, const std::optional<Value>& target,
                         const std::atomic<bool>* cancel,
                         const ProgressFn& progress) {
    const size_t w = type_size(type);

    bool trunc = false;
    bool warn = false;
    std::vector<Candidate> found;

    auto emit = [&](uint64_t addr, Value v) {
        if (found.size() >= kMaxCandidates) {
            trunc = true;
            return false;
        }
        if (!warn && found.size() >= kWarnCandidates) warn = true;
        found.push_back({addr, v});
        return true;
    };

    // Recorrido por bloques con solapamiento: ver chunk.h. El callback
    // devuelve false para detener (p. ej. al truncarse por el limite de
    // candidatos). El orden de las direcciones visitadas es identico al de
    // la implementacion anterior.
    for_each_window(mem, regions, w, [&](const uint8_t* win, uint64_t addr) {
        Value v = value_from_bytes(win, w);
        if (target && !value_equal(v, *target, type)) return true;
        return emit(addr, v);
    }, cancel, progress);

    if (cancel && cancel->load(std::memory_order_relaxed)) return false;

    if (progress && !trunc) {
        const uint64_t t = readable_total(regions);
        progress(t, t);
    }

    candidates_.swap(found);
    type_ = type;
    truncated_ = trunc;
    warned_ = warn;
    dyn_spec_.reset();
    dyn_candidates_.clear();
    return true;
}

bool Scanner::next_scan(Memory& mem, DataType type, Filter filter,
                        const std::optional<Value>& target,
                        const std::atomic<bool>* cancel,
                        const ProgressFn& progress) {
    const size_t w = type_size(type);

    // Los candidatos deben estar ordenados por direccion para el recorrido
    // por bloques (normalmente ya lo estan).
    if (!std::is_sorted(candidates_.begin(), candidates_.end(),
                        [](const Candidate& a, const Candidate& b) {
                            return a.addr < b.addr;
                        })) {
        std::sort(candidates_.begin(), candidates_.end(),
                  [](const Candidate& a, const Candidate& b) {
                      return a.addr < b.addr;
                  });
    }

    std::vector<Candidate> next;
    next.reserve(candidates_.size());
    std::vector<uint8_t> buf(kChunk + 7);

    size_t i = 0;
    const size_t n = candidates_.size();
    while (i < n) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        const uint64_t chunk_start = candidates_[i].addr;
        const uint64_t window = kChunk + (w - 1);
        ssize_t got = mem.read(chunk_start, buf.data(), window);
        if (got <= 0) {
            // El bloque no es legible (el proceso re-ejecuto, ASLR cambio, o
            // la region se desmapeo): se descarta este candidato y se
            // reintenta con el siguiente.
            ++i;
            continue;
        }
        const size_t avail = (size_t)got;
        // Lectura parcial (hueco hacia otra region): el limite del bloque es
        // lo realmente leido; los candidatos mas alla se releen en un bloque
        // nuevo (no se pierden candidatos de regiones separadas por huecos).
        const uint64_t limit =
            chunk_start + std::min<size_t>(kChunk, avail);

        while (i < n && candidates_[i].addr < limit) {
            Candidate& c = candidates_[i];
            const size_t off = (size_t)(c.addr - chunk_start);
            if (off + w > avail) {
                // Ya no es legible (el proceso re-ejecuto, ASLR cambio, o la
                // region se desmapeo): se descarta.
                ++i;
                continue;
            }
            Value cur = value_from_bytes(buf.data() + off, w);
            bool keep = false;
            switch (filter) {
                case Filter::EXACT:     keep = target && value_equal(cur, *target, type); break;
                case Filter::CHANGED:   keep = !value_equal(cur, c.prev, type); break;
                case Filter::UNCHANGED: keep = value_equal(cur, c.prev, type); break;
                case Filter::INCREASED: keep = value_compare(cur, c.prev, type) > 0; break;
                case Filter::DECREASED: keep = value_compare(cur, c.prev, type) < 0; break;
                case Filter::GREATER:   keep = target && value_compare(cur, *target, type) > 0; break;
                case Filter::LESS:      keep = target && value_compare(cur, *target, type) < 0; break;
                case Filter::GE:        keep = target && value_compare(cur, *target, type) >= 0; break;
                case Filter::LE:        keep = target && value_compare(cur, *target, type) <= 0; break;
                case Filter::NE:        keep = target && !value_equal(cur, *target, type); break;
            }
            if (keep) {
                c.prev = cur;
                next.push_back(c);
            }
            ++i;
        }
        if (progress) progress((uint64_t)i * w, (uint64_t)n * w);
    }
    if (cancel && cancel->load(std::memory_order_relaxed)) return false;

    if (progress) progress((uint64_t)n * w, (uint64_t)n * w);

    candidates_.swap(next);
    type_ = type;
    return true;
}

} // namespace mt
