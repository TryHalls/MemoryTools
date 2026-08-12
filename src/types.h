// types.h - Tipos de datos soportados por el escaner.
//
// Un "Value" guarda los bytes crudos de la variable en el orden del host
// (little-endian en x86_64, que es tambien el orden del proceso objetivo).
// La interpretacion (int con signo, unsigned, float...) se hace bajo demanda
// segun el DataType. Asi el escaner es agnostico al tipo y solo maneja bytes.
#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace mt {

enum class DataType {
    I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, PTR,
    STRING, // valor dinamico: secuencia de bytes de un texto (longitud variable)
    BYTES,  // valor dinamico: patron de bytes con wildcards (longitud variable)
};

// true para los tipos de longitud variable (string / bytes). Estos NO usan
// el camino numerico (Value de 8 bytes): el escaner First/Next los trata en
// un camino separado (ver scanner.h) con longitud propia.
inline bool type_is_dynamic(DataType t) {
    return t == DataType::STRING || t == DataType::BYTES;
}

inline size_t type_size(DataType t) {
    switch (t) {
        case DataType::I8:
        case DataType::U8:  return 1;
        case DataType::I16:
        case DataType::U16: return 2;
        case DataType::I32:
        case DataType::U32:
        case DataType::F32: return 4;
        case DataType::I64:
        case DataType::U64:
        case DataType::F64:
        case DataType::PTR: return 8;
        // Los dinamicos no tienen anchura fija: longitud variable por escaneo.
        case DataType::STRING:
        case DataType::BYTES: return 1; // nunca usado por el camino numerico
    }
    return 8;
}

inline bool type_is_float(DataType t) {
    return t == DataType::F32 || t == DataType::F64;
}

inline const char* type_name(DataType t) {
    switch (t) {
        case DataType::I8:  return "int8";
        case DataType::U8:  return "uint8";
        case DataType::I16: return "int16";
        case DataType::U16: return "uint16";
        case DataType::I32: return "int32";
        case DataType::U32: return "uint32";
        case DataType::I64: return "int64";
        case DataType::U64: return "uint64";
        case DataType::F32: return "float";
        case DataType::F64: return "double";
        case DataType::PTR: return "pointer";
        case DataType::STRING: return "string";
        case DataType::BYTES: return "bytes";
    }
    return "?";
}

inline bool parse_type(const std::string& s, DataType& out) {
    struct Alias { const char* name; DataType t; };
    static const Alias kAliases[] = {
        {"i8", DataType::I8},    {"int8", DataType::I8},   {"s8", DataType::I8},
        {"u8", DataType::U8},    {"uint8", DataType::U8},  {"byte", DataType::U8},
        {"i16", DataType::I16},  {"int16", DataType::I16}, {"s16", DataType::I16},
        {"u16", DataType::U16},  {"uint16", DataType::U16},
        {"i32", DataType::I32},  {"int32", DataType::I32}, {"int", DataType::I32},
        {"u32", DataType::U32},  {"uint32", DataType::U32},{"uint", DataType::U32},
        {"i64", DataType::I64},  {"int64", DataType::I64}, {"long", DataType::I64},
        {"u64", DataType::U64},  {"uint64", DataType::U64},{"ulong", DataType::U64},
        {"f32", DataType::F32},  {"float", DataType::F32},
        {"f64", DataType::F64},  {"double", DataType::F64},
        {"ptr", DataType::PTR},  {"pointer", DataType::PTR},
    };
    for (const auto& a : kAliases)
        if (s == a.name) { out = a.t; return true; }
    return false;
}

// Valor crudo: los bytes de la variable en little-endian dentro de bits.
struct Value {
    uint64_t bits = 0;
};

// Copia los primeros w bytes de p a v.bits (host little-endian == objetivo).
inline Value value_from_bytes(const uint8_t* p, size_t w) {
    Value v;
    if (w > sizeof(v.bits)) w = sizeof(v.bits);
    std::memcpy(&v.bits, p, w);
    return v;
}

inline int64_t value_as_int(Value v, DataType t) {
    switch (t) {
        case DataType::I8:  return (int8_t)(uint8_t)v.bits;
        case DataType::U8:  return (uint8_t)v.bits;
        case DataType::I16: return (int16_t)(uint16_t)v.bits;
        case DataType::U16: return (uint16_t)v.bits;
        case DataType::I32: return (int32_t)(uint32_t)v.bits;
        case DataType::U32: return (uint32_t)v.bits;
        case DataType::I64:
        case DataType::U64:
        case DataType::PTR: return (int64_t)v.bits;
        default:            return 0;
    }
}

inline uint64_t value_as_uint(Value v, DataType t) {
    switch (t) {
        case DataType::U8:  return (uint8_t)v.bits;
        case DataType::U16: return (uint16_t)v.bits;
        case DataType::U32: return (uint32_t)v.bits;
        case DataType::U64:
        case DataType::PTR: return v.bits;
        default:            return (uint64_t)value_as_int(v, t);
    }
}

inline double value_as_double(Value v, DataType t) {
    switch (t) {
        case DataType::F32: {
            float f;
            std::memcpy(&f, &v.bits, sizeof(f));
            return f;
        }
        case DataType::F64: {
            double d;
            std::memcpy(&d, &v.bits, sizeof(d));
            return d;
        }
        default:
            return (double)value_as_int(v, t);
    }
}

// Compara interpretando el tipo. Devuelve -1/0/1.
inline int value_compare(Value a, Value b, DataType t) {
    if (type_is_float(t)) {
        double x = value_as_double(a, t);
        double y = value_as_double(b, t);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    switch (t) {
        case DataType::I8:
        case DataType::I16:
        case DataType::I32:
        case DataType::I64: {
            int64_t x = value_as_int(a, t);
            int64_t y = value_as_int(b, t);
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        default: {
            uint64_t x = value_as_uint(a, t);
            uint64_t y = value_as_uint(b, t);
            return x < y ? -1 : (x > y ? 1 : 0);
        }
    }
}

inline bool value_equal(Value a, Value b, DataType t) {
    return value_compare(a, b, t) == 0;
}

// Parsea texto a un valor del tipo pedido. Numeros decimales, o 0x... en hex.
inline bool parse_value(const std::string& text, DataType t, Value& out) {
    if (text.empty()) return false;
    char* end = nullptr;

    if (type_is_float(t)) {
        double d = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') return false;
        if (t == DataType::F32) {
            float f = (float)d;
            std::memcpy(&out.bits, &f, sizeof(f));
        } else {
            std::memcpy(&out.bits, &d, sizeof(d));
        }
        return true;
    }

    int base = (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;
    long long v = std::strtoll(text.c_str(), &end, base);
    if (end == text.c_str() || *end != '\0') return false;

    switch (t) {
        case DataType::I8:
            if (v < INT8_MIN || v > INT8_MAX) return false;
            out.bits = (uint8_t)(int8_t)v;
            break;
        case DataType::U8:
            if (v < 0 || v > UINT8_MAX) return false;
            out.bits = (uint8_t)v;
            break;
        case DataType::I16:
            if (v < INT16_MIN || v > INT16_MAX) return false;
            out.bits = (uint16_t)(int16_t)v;
            break;
        case DataType::U16:
            if (v < 0 || v > UINT16_MAX) return false;
            out.bits = (uint16_t)v;
            break;
        case DataType::I32:
            if (v < INT32_MIN || v > INT32_MAX) return false;
            out.bits = (uint32_t)(int32_t)v;
            break;
        case DataType::U32:
            if (v < 0 || (uint64_t)v > UINT32_MAX) return false;
            out.bits = (uint32_t)v;
            break;
        case DataType::I64:
            out.bits = (uint64_t)v;
            break;
        case DataType::U64:
        case DataType::PTR:
            if (v < 0) return false;
            out.bits = (uint64_t)v;
            break;
        default:
            return false;
    }
    return true;
}

inline std::string value_to_string(Value v, DataType t) {
    char buf[64];
    if (type_is_float(t)) {
        snprintf(buf, sizeof buf, "%.6g", value_as_double(v, t));
        return buf;
    }
    switch (t) {
        case DataType::U8:
        case DataType::U16:
        case DataType::U32:
            snprintf(buf, sizeof buf, "%llu", (unsigned long long)value_as_uint(v, t));
            return buf;
        case DataType::U64:
            snprintf(buf, sizeof buf, "%llu", (unsigned long long)v.bits);
            return buf;
        case DataType::PTR:
            snprintf(buf, sizeof buf, "0x%016llx", (unsigned long long)v.bits);
            return buf;
        default:
            snprintf(buf, sizeof buf, "%lld", (long long)value_as_int(v, t));
            return buf;
    }
}

} // namespace mt
