// pattern.cpp - Pattern / AOB scanner.
//
// Busca una secuencia de bytes (con wildcards "??") en las regiones legibles
// del proceso objetivo. La busqueda es no alineada: se comprueba cada
// posicion de byte, igual que un AOB scanner clasico. Las regiones se leen
// por bloques con solapamiento para no perder coincidencias en los limites.
#include "pattern.h"

#include <algorithm>

namespace mt {

bool parse_pattern(const std::string& text, BytePattern& out) {
    out = BytePattern{};
    if (text.empty()) {
        out.error = "patron vacio";
        return false;
    }

    // Limpiar separadores: espacios, tabuladores, comas, puntos, guiones.
    std::string clean;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == ',' || c == '.' || c == '-' || c == '_')
            continue;
        clean += c;
    }
    if (clean.empty()) {
        out.error = "patron vacio";
        return false;
    }
    if (clean.size() % 2 != 0) {
        out.error = "el patron debe tener un numero par de caracteres "
                    "(pares hex o \?\?)";
        return false;
    }

    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < clean.size(); i += 2) {
        const char a = clean[i], b = clean[i + 1];
        if (a == '?' && b == '?') {
            out.bytes.push_back(0);
            out.mask.push_back(false);
        } else {
            const int ha = hexv(a), hb = hexv(b);
            if (ha < 0 || hb < 0) {
                out.error = std::string("caracter invalido en el patron: '") +
                            a + b + "' (usa pares hex o \?\?)";
                return false;
            }
            out.bytes.push_back((uint8_t)((ha << 4) | hb));
            out.mask.push_back(true);
        }
    }

    out.valid = true;
    return true;
}

PatternScanResult scan_pattern(Memory& mem, const std::vector<Region>& regions,
                               const BytePattern& pat) {
    PatternScanResult res;
    if (!pat.valid || pat.size() == 0) return res;

    constexpr size_t kChunk = 4u * 1024u * 1024u;
    constexpr size_t kMaxHits = 5u * 1000u * 1000u;
    const size_t w = pat.size();

    std::vector<uint8_t> buf(kChunk);
    for (const Region& r : regions) {
        if (!r.readable()) continue;
        uint64_t pos = r.start;
        while (pos < r.end && !res.truncated) {
            const size_t want =
                (size_t)std::min<uint64_t>(kChunk, r.end - pos);
            ssize_t got = mem.read(pos, buf.data(), want);
            if (got <= 0) {
                pos += want;
                continue;
            }
            const size_t n = (size_t)got;
            if (n >= w) {
                for (size_t i = 0; i + w <= n; ++i) {
                    bool ok = true;
                    for (size_t j = 0; j < w; ++j) {
                        if (pat.mask[j] && buf[i + j] != pat.bytes[j]) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        res.hits.push_back(pos + i);
                        if (res.hits.size() >= kMaxHits) {
                            res.truncated = true;
                            break;
                        }
                    }
                }
                pos += n - (w - 1); // solapamiento: no perder limites
            } else {
                pos += n;
            }
        }
    }
    return res;
}

} // namespace mt
