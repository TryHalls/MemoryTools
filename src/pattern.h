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

// Limite de seguridad para la longitud de un valor dinamico (string o patron
// de bytes) en el escaner First/Next. Un patron mas largo se rechaza: evita
// estructuras enormes por candidato y escaneos lentos en el Chromebook.
inline constexpr size_t kMaxDynamicLength = 4096;

// true si una ventana de bytes coincide con el patron (mask[j] = true ->
// byte exacto; mask[j] = false -> wildcard). Funcion pura, compartida por
// scan_pattern y por el escaner First/Next de valores dinamicos.
inline bool pattern_window_matches(const uint8_t* win, const BytePattern& pat) {
    for (size_t j = 0; j < pat.bytes.size(); ++j)
        if (pat.mask[j] && win[j] != pat.bytes[j]) return false;
    return true;
}

// Convierte un texto a un patron de bytes exactos (sin wildcards). Se
// codifica como bytes crudos del texto (ASCII / UTF-8 de un solo byte, tal
// cual se almacena en memoria); no se hace ninguna conversion Unicode.
// Rellena err si el texto esta vacio o excede kMaxDynamicLength.
inline BytePattern pattern_from_text(const std::string& text, std::string& err) {
    BytePattern p;
    if (text.empty()) {
        err = "string vacia";
        return p;
    }
    if (text.size() > kMaxDynamicLength) {
        err = "string demasiado larga (maximo " +
              std::to_string(kMaxDynamicLength) + " bytes)";
        return p;
    }
    p.bytes.assign(text.begin(), text.end());
    p.mask.assign(text.size(), true);
    p.valid = true;
    return p;
}

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
