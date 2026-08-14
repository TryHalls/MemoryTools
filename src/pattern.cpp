// pattern.cpp - Pattern / AOB scanner.
//
// El recorrido por bloques de scan_pattern vive en pattern.h como template
// (igual que for_each_window) para poder testearlo con memoria fake; aqui
// queda unicamente el parseo de patrones.
#include "pattern.h"

#include <string>
#include <vector>

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

    // Limite de seguridad compartido con los strings del escaner First/Next
    // (ver kMaxDynamicLength): un patron mas largo no tiene sentido en un
    // analizador de memoria de bajo consumo y encarece cada candidato.
    if (clean.size() > kMaxDynamicLength * 2) {
        out.error = "patron demasiado largo (maximo " +
                    std::to_string(kMaxDynamicLength) + " bytes)";
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

} // namespace mt
