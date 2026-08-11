// scanner.h - Motor de escaneo progresivo (First Scan / Next Scan).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "chunk.h"
#include "memory.h"
#include "types.h"

namespace mt {

enum class Filter {
    EXACT,      // igual a un valor dado
    CHANGED,    // cambio respecto al escaneo anterior
    UNCHANGED,  // no cambio
    INCREASED,  // aumento
    DECREASED,  // disminuyo
    GREATER,    // > valor dado
    LESS,       // < valor dado
    GE,         // >= valor dado
    LE,         // <= valor dado
    NE,         // != valor dado
};

// Una coincidencia: direccion + valor que tenia al momento del escaneo.
struct Candidate {
    uint64_t addr = 0;
    Value prev;
};

class Scanner {
public:
    void clear();
    bool has_results() const { return !candidates_.empty(); }
    size_t count() const { return candidates_.size(); }
    bool truncated() const { return truncated_; }
    // true si la lista de candidatos supero el umbral de aviso (kWarnCandidates)
    bool warned() const { return warned_; }
    const std::vector<Candidate>& results() const { return candidates_; }

    // Primer escaneo sobre las regiones legibles.
    //  - target con valor: busca coincidencias exactas de ese valor.
    //  - target vacio ("unknown"): guarda TODAS las posiciones legibles con
    //    su valor actual, para luego usar filtros de cambio.
    // Las direcciones se guardan alineadas a byte, en orden de memoria.
    void first_scan(Memory& mem, const std::vector<Region>& regions,
                    DataType type, const std::optional<Value>& target);

    // Refina el resultado anterior aplicando un filtro.
    // target es obligatorio para EXACT/GREATER/LESS/GE/LE/NE.
    void next_scan(Memory& mem, DataType type, Filter filter,
                   const std::optional<Value>& target);

private:
    std::vector<Candidate> candidates_;
    DataType type_ = DataType::I32;
    bool truncated_ = false;
    bool warned_ = false;

    // CRIT-1: el limite baja de 50M a 20M (~320 MiB) para no agotar la RAM
    // del Chromebook (6.5 GiB, sin swap). A partir de kWarnCandidates se
    // advierte del consumo elevado.
    static constexpr size_t kMaxCandidates = 20u * 1000u * 1000u;
    static constexpr size_t kWarnCandidates = 10u * 1000u * 1000u;
    static constexpr size_t kChunk = kChunkBytes; // tamano de bloque compartido
};

} // namespace mt
