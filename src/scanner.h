// scanner.h - Motor de escaneo progresivo (First Scan / Next Scan).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "chunk.h"
#include "memory.h"
#include "pattern.h"
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

// --- Valores dinamicos (string / bytes) -----------------------------------
//
// Los candidatos dinamicos NO guardan el valor completo: el patron (bytes +
// mask) vive una sola vez en DynamicScanSpec y se comparte entre todos los
// candidatos. Cada candidato guarda solo los bytes leidos en las posiciones
// '?' (wildcards) en el escaneo anterior, que son los unicos que no pueden
// reconstruirse desde el patron. Para strings (o patrones sin wildcards) ese
// vector queda vacio y el coste por candidato es una direccion + un vector
// vacio (sin asignaciones).
struct DynamicCandidate {
    uint64_t addr = 0;
    std::vector<uint8_t> prev_wild; // bytes en posiciones '?' del escaneo anterior
};

// Especificacion de un escaneo dinamico (first o next con patron nuevo).
struct DynamicScanSpec {
    DataType type = DataType::STRING; // STRING o BYTES
    BytePattern pattern;              // bytes + mask (una sola copia)
    std::vector<size_t> wild_pos;     // posiciones '?' precomputadas

    size_t length() const { return pattern.size(); }
};

// Construye la especificacion desde un patron ya validado: precomputa las
// posiciones wildcard y fija el tipo. Funcion pura.
inline DynamicScanSpec make_dynamic_spec(DataType type, BytePattern pat) {
    DynamicScanSpec s;
    s.type = type;
    s.pattern = std::move(pat);
    for (size_t i = 0; i < s.pattern.mask.size(); ++i)
        if (!s.pattern.mask[i]) s.wild_pos.push_back(i);
    return s;
}

// Funcion pura: true si la ventana actual difiere del valor 'anterior' de un
// candidato dinamico. El valor anterior se reconstruye desde el patron
// (bytes en posiciones exactas) + prev_wild (bytes en posiciones '?').
// Expuesta para los tests unitarios y usada por next_scan_dynamic.
inline bool dynamic_window_changed(const uint8_t* win,
                                   const DynamicScanSpec& spec,
                                   const std::vector<uint8_t>& prev_wild) {
    size_t wi = 0;
    for (size_t k = 0; k < spec.length(); ++k) {
        uint8_t pv;
        if (spec.pattern.mask[k]) {
            pv = spec.pattern.bytes[k];
        } else {
            pv = (wi < prev_wild.size()) ? prev_wild[wi] : 0;
            ++wi;
        }
        if (win[k] != pv) return true;
    }
    return false;
}

class Scanner {
public:
    void clear();
    bool has_results() const { return !candidates_.empty() || !dyn_candidates_.empty(); }
    size_t count() const {
        return dyn_spec_ ? dyn_candidates_.size() : candidates_.size();
    }
    bool truncated() const { return truncated_; }
    // true si la lista de candidatos supero el umbral de aviso (kWarnCandidates)
    bool warned() const { return warned_; }
    const std::vector<Candidate>& results() const { return candidates_; }

    // --- Valores dinamicos (string / bytes) -------------------------------
    // true si el ultimo escaneo (first o next) fue dinamico.
    bool is_dynamic() const { return dyn_spec_.has_value(); }
    const DynamicScanSpec& dyn_spec() const { return *dyn_spec_; }
    const std::vector<DynamicCandidate>& dynamic_results() const {
        return dyn_candidates_;
    }

    // Primer escaneo dinamico: busca el patron (string o bytes con
    // wildcards) en las regiones legibles. Es una busqueda no alineada que
    // usa for_each_window (los patrones que cruzan el limite entre bloques
    // se encuentran gracias al overlap).
    void first_scan_dynamic(Memory& mem, const std::vector<Region>& regions,
                            const DynamicScanSpec& spec);

    // Refina un escaneo dinamico previo.
    //  - Filter::EXACT: exige newspec (patron nuevo); compara contra el.
    //  - Filter::CHANGED / UNCHANGED: compara los bytes actuales con los
    //    'anteriores' (patron anterior en posiciones exactas + prev_wild en
    //    las posiciones '?'). Los demas filtros no tienen sentido aqui.
    void next_scan_dynamic(Memory& mem, Filter filter,
                           const std::optional<DynamicScanSpec>& newspec);

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
    std::vector<DynamicCandidate> dyn_candidates_;
    std::optional<DynamicScanSpec> dyn_spec_;
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
