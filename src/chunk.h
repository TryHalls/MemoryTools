// chunk.h - Recorrido por bloques con solapamiento de regiones de memoria.
//
// El escaner de valores (Scanner::first_scan) y el de patrones
// (scan_pattern) recorren las regiones legibles de la misma forma:
//
//   - las regiones legibles se leen en bloques de hasta kChunkBytes;
//   - cada ventana de window_size bytes totalmente contenida en la region
//     se entrega al callback una sola vez;
//   - los ultimos window_size - 1 bytes de cada bloque se releen en el
//     bloque siguiente (overlap), de modo que ninguna ventana que cruce el
//     limite entre bloques quede sin visitar;
//   - un bloque ilegible se salta entero y una lectura parcial avanza solo
//     lo leido, lo que permite tratar regiones parcialmente legibles.
//
// Este helper centraliza ese recorrido; cada escaner aporta su propia
// logica de procesamiento mediante un callback.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "memory.h"

namespace mt {

// Tamano unico de bloque para las lecturas por bloques con solapamiento.
inline constexpr size_t kChunkBytes = 4u * 1024u * 1024u;

// Recorre las regiones legibles de mem y llama a callback(bytes, addr) con
// cada ventana de window_size bytes que empieza en addr y cabe
// completamente dentro de una region. El callback devuelve true para
// continuar o false para detener todo el recorrido (p. ej. al alcanzar un
// limite de resultados).
template <typename Fn>
void for_each_window(Memory& mem, const std::vector<Region>& regions,
                     size_t window_size, Fn&& callback) {
    if (window_size == 0) return;
    const size_t w = window_size;
    std::vector<uint8_t> buf(kChunkBytes);

    for (const Region& r : regions) {
        if (!r.readable()) continue;
        uint64_t pos = r.start;
        while (pos < r.end) {
            const size_t want =
                (size_t)std::min<uint64_t>(kChunkBytes, r.end - pos);
            ssize_t got = mem.read(pos, buf.data(), want);
            if (got <= 0) { // bloque ilegible: se salta entero
                pos += want;
                continue;
            }
            const size_t n = (size_t)got;
            if (n >= w) {
                for (size_t i = 0; i + w <= n; ++i) {
                    if (!callback(buf.data() + i, pos + i)) return;
                }
                // Solapamiento: los ultimos w-1 bytes se releen en el
                // siguiente bloque para no perder ventanas en el limite.
                pos += n - (w - 1);
            } else {
                // Lectura parcial menor que la ventana: no hay ventanas
                // completas aqui; se avanza solo lo leido.
                pos += n;
            }
        }
    }
}

} // namespace mt
