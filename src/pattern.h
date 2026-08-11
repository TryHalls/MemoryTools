// pattern.h - Pattern / AOB scanner (busqueda de secuencias de bytes).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "memory.h"

namespace mt {

// Un patron de bytes con wildcards.
//  - bytes[i]: valor del byte (0 donde hay wildcard).
//  - mask[i]:  true = byte exacto, false = wildcard (cualquier valor).
struct BytePattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    bool valid = false;
    std::string error;

    size_t size() const { return bytes.size(); }
    bool has_wildcards() const {
        for (bool m : mask)
            if (!m) return true;
        return false;
    }
};

// Parsea "48 8B 05 ?? ?? ?? ?? 48 85 C0".
// Acepta separadores (espacios, comas, puntos...) y tambien sin separadores
// ("488B05????"). "??" = wildcard. Devuelve false y rellena error si falla.
bool parse_pattern(const std::string& text, BytePattern& out);

struct PatternScanResult {
    std::vector<uint64_t> hits; // direcciones donde empieza el patron
    bool truncated = false;     // true si se alcanzo el limite de resultados
};

// Busca el patron en las regiones legibles del proceso.
//  - mem:    memoria abierta del proceso objetivo (attach previo).
//  - regions: regiones de /proc/PID/maps (solo se recorren las legibles).
PatternScanResult scan_pattern(Memory& mem, const std::vector<Region>& regions,
                               const BytePattern& pat);

} // namespace mt
