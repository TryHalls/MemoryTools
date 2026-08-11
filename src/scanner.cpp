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
#include "scanner.h"

#include <algorithm>
#include <cstring>

#include "chunk.h"

namespace mt {

void Scanner::clear() {
    candidates_.clear();
    truncated_ = false;
    warned_ = false;
}

void Scanner::first_scan(Memory& mem, const std::vector<Region>& regions,
                         DataType type, const std::optional<Value>& target) {
    clear();
    type_ = type;
    const size_t w = type_size(type);

    auto emit = [&](uint64_t addr, Value v) {
        if (candidates_.size() >= kMaxCandidates) {
            truncated_ = true;
            return false;
        }
        if (!warned_ && candidates_.size() >= kWarnCandidates)
            warned_ = true;
        candidates_.push_back({addr, v});
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
    });
}

void Scanner::next_scan(Memory& mem, DataType type, Filter filter,
                        const std::optional<Value>& target) {
    type_ = type;
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
        const uint64_t chunk_start = candidates_[i].addr;
        const uint64_t window = kChunk + (w - 1);
        ssize_t got = mem.read(chunk_start, buf.data(), window);
        const size_t avail = got > 0 ? (size_t)got : 0;
        const uint64_t limit = chunk_start + kChunk;

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
    }

    candidates_.swap(next);
}

} // namespace mt
