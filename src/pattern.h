// pattern.h - Pattern / AOB scanner (busqueda de secuencias de bytes).
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "chunk.h"
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
    bool cancelled = false;     // true si el recorrido fue cancelado
};

// Busca el patron en las regiones legibles del proceso.
//  - mem:    memoria abierta del proceso objetivo (attach previo).
//  - regions: regiones de /proc/PID/maps (solo se recorren las legibles).
//  - cancel: flag atomico opcional; si esta activo se detiene el recorrido y
//    se devuelve un resultado con cancelled = true y hits vacios (nunca se
//    publican hits parciales).
//  - progress: callback opcional de progreso por bloque (monotonico, nunca
//    supera total y llega a 100% al completar el recorrido completo).
// 'Mem' es generico (igual que for_each_window) para poder testear el
// recorrido con un fake en los tests unitarios; los llamadores reales pasan
// un Memory&.
template <typename Mem>
PatternScanResult scan_pattern(Mem& mem, const std::vector<Region>& regions,
                               const BytePattern& pat,
                               const std::atomic<bool>* cancel = nullptr,
                               const ProgressFn& progress = {}) {
    PatternScanResult res;
    if (!pat.valid || pat.size() == 0) return res;

    constexpr size_t kMaxHits = 5u * 1000u * 1000u;
    const size_t w = pat.size();

    // Recorrido por bloques con solapamiento: ver chunk.h. El callback
    // devuelve false para detener el recorrido al alcanzar el limite de
    // resultados. El orden de las direcciones visitadas es identico al de
    // la implementacion anterior.
    for_each_window(mem, regions, w, [&](const uint8_t* win, uint64_t addr) {
        if (pattern_window_matches(win, pat)) {
            res.hits.push_back(addr);
            if (res.hits.size() >= kMaxHits) {
                res.truncated = true;
                return false; // detener todo el recorrido
            }
        }
        return true;
    }, cancel, progress);

    if (cancel && cancel->load(std::memory_order_relaxed)) {
        // Cancelado: no se publican hits parciales como resultado final.
        res.hits.clear();
        res.truncated = false;
        res.cancelled = true;
    }
    return res;
}

} // namespace mt
