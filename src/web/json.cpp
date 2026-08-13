// json.cpp - Implementacion del JSON minimo (FASE W-1).
#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mt {
namespace json {

// --- JsonValue --------------------------------------------------------------

JsonValue JsonValue::make_null() { return JsonValue{}; }
JsonValue JsonValue::make_bool(bool b) {
    JsonValue v;
    v.v_ = b;
    return v;
}
JsonValue JsonValue::make_int(int64_t i) {
    JsonValue v;
    v.v_ = i;
    return v;
}
JsonValue JsonValue::make_uint(uint64_t u) {
    JsonValue v;
    v.v_ = u;
    return v;
}
JsonValue JsonValue::make_double(double d) {
    JsonValue v;
    v.v_ = d;
    return v;
}
JsonValue JsonValue::make_string(std::string s) {
    JsonValue v;
    v.v_ = std::move(s);
    return v;
}
JsonValue JsonValue::make_array(Array a) {
    JsonValue v;
    v.v_ = std::move(a);
    return v;
}
JsonValue JsonValue::make_object(Object o) {
    JsonValue v;
    v.v_ = std::move(o);
    return v;
}

JsonValue::Type JsonValue::type() const {
    return static_cast<Type>(v_.index());
}

bool JsonValue::as_bool(bool def) const {
    const bool* p = std::get_if<bool>(&v_);
    return p ? *p : def;
}

int64_t JsonValue::as_int(int64_t def) const {
    const int64_t* p = std::get_if<int64_t>(&v_);
    return p ? *p : def;
}

uint64_t JsonValue::as_uint(uint64_t def) const {
    const uint64_t* p = std::get_if<uint64_t>(&v_);
    return p ? *p : def;
}

double JsonValue::as_double(double def) const {
    const double* p = std::get_if<double>(&v_);
    if (p) return *p;
    const int64_t* i = std::get_if<int64_t>(&v_);
    if (i) return (double)*i;
    const uint64_t* u = std::get_if<uint64_t>(&v_);
    if (u) return (double)*u;
    return def;
}

const std::string* JsonValue::as_string() const {
    return std::get_if<std::string>(&v_);
}

const JsonValue::Array* JsonValue::as_array() const {
    return std::get_if<Array>(&v_);
}

const JsonValue::Object* JsonValue::as_object() const {
    return std::get_if<Object>(&v_);
}

const JsonValue* JsonValue::get(const std::string& key) const {
    const Object* o = as_object();
    if (!o) return nullptr;
    const JsonValue* found = nullptr;
    for (const auto& kv : *o) {
        if (kv.first == key) found = &kv.second; // claves duplicadas: ultima gana
    }
    return found;
}

// --- Escritor ---------------------------------------------------------------

static void write_escaped(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04X", c);
                    out += b;
                } else {
                    out += (char)c; // UTF-8 passthrough (>= 0x20)
                }
        }
    }
    out += '"';
}

static void write_value(const JsonValue& v, std::string& out) {
    switch (v.type()) {
        case JsonValue::Type::Null:
            out += "null";
            return;
        case JsonValue::Type::Bool:
            out += v.as_bool() ? "true" : "false";
            return;
        case JsonValue::Type::Int:
            out += std::to_string(v.as_int());
            return;
        case JsonValue::Type::UInt:
            out += std::to_string(v.as_uint());
            return;
        case JsonValue::Type::Double: {
            const double d = v.as_double();
            if (std::isnan(d) || std::isinf(d)) {
                out += "null"; // JSON no admite NaN/Inf
                return;
            }
            // Formato corto con round-trip exacto.
            char b1[64], b2[64];
            snprintf(b1, sizeof b1, "%.17g", d);
            snprintf(b2, sizeof b2, "%.15g", d);
            if (std::strtod(b2, nullptr) == d)
                out += b2;
            else
                out += b1;
            return;
        }
        case JsonValue::Type::String:
            write_escaped(*v.as_string(), out);
            return;
        case JsonValue::Type::Array: {
            out += '[';
            const auto& a = *v.as_array();
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) out += ',';
                write_value(a[i], out);
            }
            out += ']';
            return;
        }
        case JsonValue::Type::Object: {
            out += '{';
            const auto& o = *v.as_object();
            for (size_t i = 0; i < o.size(); ++i) {
                if (i) out += ',';
                write_escaped(o[i].first, out);
                out += ':';
                write_value(o[i].second, out);
            }
            out += '}';
            return;
        }
    }
}

std::string write(const JsonValue& v) {
    std::string out;
    out.reserve(128);
    write_value(v, out);
    return out;
}

std::string escape_string(const std::string& s) {
    std::string out;
    write_escaped(s, out);
    return out;
}

// --- Parser -----------------------------------------------------------------

namespace {

struct Parser {
    const char* begin;
    const char* p;
    const char* end;
    ParseError err;
    size_t depth = 0;

    size_t off(const char* q) const { return (size_t)(q - begin); }

    bool fail(const std::string& msg, const char* q) {
        err.ok = false;
        err.message = msg;
        err.offset = off(q);
        return false;
    }

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    bool parse_hex4(uint32_t& out) {
        if (end - p < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= (uint32_t)(c - 'A' + 10);
            else
                return false;
        }
        p += 4;
        out = v;
        return true;
    }

    void append_utf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s += (char)cp;
        } else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool parse_literal(const char* lit, JsonValue val, JsonValue& out) {
        const size_t n = std::strlen(lit);
        if ((size_t)(end - p) < n || std::memcmp(p, lit, n) != 0)
            return fail("literal JSON invalido", p);
        const char* q = p + n;
        if (q < end && (std::isalnum((unsigned char)*q) || *q == '_'))
            return fail("literal JSON invalido", p);
        p = q;
        out = std::move(val);
        return true;
    }

    bool parse_string(JsonValue& out) {
        const char* start = p;
        ++p; // consumir '"'
        std::string s;
        while (p < end) {
            const unsigned char c = (unsigned char)*p;
            if (c == '"') {
                ++p;
                out = JsonValue::make_string(std::move(s));
                return true;
            }
            if (c == '\\') {
                ++p; // consumir '\'
                if (p >= end) return fail("escape JSON incompleto", p);
                const char e = *p++;
                switch (e) {
                    case '"':  s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        uint32_t cp;
                        if (!parse_hex4(cp))
                            return fail("escape \\uXXXX invalido", p);
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // par de suplentes: \uD800.. \uDC00..
                            if (end - p < 2 || p[0] != '\\' || p[1] != 'u')
                                return fail("par de suplentes incompleto", p);
                            p += 2;
                            uint32_t lo;
                            if (!parse_hex4(lo))
                                return fail("escape \\uXXXX invalido", p);
                            if (lo < 0xDC00 || lo > 0xDFFF)
                                return fail("par de suplentes invalido", p);
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return fail("suplente bajo sin suplente alto", p);
                        }
                        append_utf8(s, cp);
                        break;
                    }
                    default:
                        return fail("escape JSON desconocido", p);
                }
                if (s.size() > kMaxString)
                    return fail("string demasiado larga (maximo 4096)", p);
                continue;
            }
            if (c < 0x20)
                return fail("caracter de control sin escapar", p);
            s += (char)c;
            if (s.size() > kMaxString)
                return fail("string demasiado larga (maximo 4096)", p);
            ++p;
        }
        return fail("string sin cerrar", start);
    }

    bool parse_number(JsonValue& out) {
        const char* start = p;
        if (p < end && *p == '-') ++p;
        if (p >= end) return fail("numero JSON invalido", start);
        if (*p == '0') {
            ++p;
            // JSON no permite ceros a la izquierda: "01" es invalido.
            if (p < end && std::isdigit((unsigned char)*p))
                return fail("numero JSON invalido (cero a la izquierda)", start);
        } else if (*p >= '1' && *p <= '9') {
            while (p < end && std::isdigit((unsigned char)*p)) ++p;
        } else {
            return fail("numero JSON invalido", start);
        }
        bool is_double = false;
        if (p < end && *p == '.') {
            is_double = true;
            ++p;
            if (p >= end || !std::isdigit((unsigned char)*p))
                return fail("fraccion JSON invalida", start);
            while (p < end && std::isdigit((unsigned char)*p)) ++p;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            is_double = true;
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            if (p >= end || !std::isdigit((unsigned char)*p))
                return fail("exponente JSON invalido", start);
            while (p < end && std::isdigit((unsigned char)*p)) ++p;
        }
        const size_t len = (size_t)(p - start);
        if (len > kMaxNumberChars)
            return fail("numero demasiado largo", start);
        const std::string tok(start, len);
        if (is_double) {
            errno = 0;
            char* endp = nullptr;
            const double d = std::strtod(tok.c_str(), &endp);
            if (errno == ERANGE || endp != tok.c_str() + len)
                return fail("numero fuera de rango", start);
            out = JsonValue::make_double(d);
            return true;
        }
        // Entero: intentar int64 primero (natural para numeros pequenos).
        errno = 0;
        char* endp = nullptr;
        const long long v = std::strtoll(tok.c_str(), &endp, 10);
        if (endp == tok.c_str() + len && errno != ERANGE) {
            out = JsonValue::make_int((int64_t)v);
            return true;
        }
        // Negativo que no cabe en int64: invalido (strtoull aceptaria el
        // signo y envolveria, produciendo un uint64 incorrecto).
        if (tok[0] == '-') return fail("numero fuera de rango", start);
        // Positivo fuera de int64: uint64 (hasta UINT64_MAX).
        errno = 0;
        endp = nullptr;
        const unsigned long long u = std::strtoull(tok.c_str(), &endp, 10);
        if (endp != tok.c_str() + len || errno == ERANGE)
            return fail("numero fuera de rango", start);
        out = JsonValue::make_uint((uint64_t)u);
        return true;
    }

    bool parse_object(JsonValue& out);
    bool parse_array(JsonValue& out);

    bool parse_value(JsonValue& out) {
        skip_ws();
        if (p >= end) return fail("JSON vacio o inesperado", p);
        const char c = *p;
        switch (c) {
            case '{': return parse_object(out);
            case '[': return parse_array(out);
            case '"': return parse_string(out);
            case 't': return parse_literal("true", JsonValue::make_bool(true), out);
            case 'f': return parse_literal("false", JsonValue::make_bool(false), out);
            case 'n': return parse_literal("null", JsonValue::make_null(), out);
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                    return parse_number(out);
                return fail("valor JSON inesperado", p);
        }
    }
};

bool Parser::parse_object(JsonValue& out) {
    if (depth >= kMaxDepth) return fail("anidamiento demasiado profundo", p);
    ++depth;
    ++p; // consumir '{'
    skip_ws();
    JsonValue::Object obj;
    if (p < end && *p == '}') {
        ++p;
        --depth;
        out = JsonValue::make_object(std::move(obj));
        return true;
    }
    while (true) {
        skip_ws();
        if (p >= end || *p != '"')
            return fail("se esperaba una clave de object", p);
        JsonValue key;
        if (!parse_string(key)) return false;
        skip_ws();
        if (p >= end || *p != ':')
            return fail("se esperaba ':'", p);
        ++p;
        JsonValue val;
        if (!parse_value(val)) return false;
        obj.emplace_back(*key.as_string(), std::move(val));
        skip_ws();
        if (p >= end) return fail("object sin cerrar", p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            ++p;
            break;
        }
        return fail("se esperaba ',' o '}'", p);
    }
    --depth;
    out = JsonValue::make_object(std::move(obj));
    return true;
}

bool Parser::parse_array(JsonValue& out) {
    if (depth >= kMaxDepth) return fail("anidamiento demasiado profundo", p);
    ++depth;
    ++p; // consumir '['
    skip_ws();
    JsonValue::Array arr;
    if (p < end && *p == ']') {
        ++p;
        --depth;
        out = JsonValue::make_array(std::move(arr));
        return true;
    }
    while (true) {
        skip_ws();
        if (p >= end) return fail("array sin cerrar", p);
        JsonValue val;
        if (!parse_value(val)) return false;
        arr.push_back(std::move(val));
        skip_ws();
        if (p >= end) return fail("array sin cerrar", p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == ']') {
            ++p;
            break;
        }
        return fail("se esperaba ',' o ']'", p);
    }
    --depth;
    out = JsonValue::make_array(std::move(arr));
    return true;
}

} // namespace

bool parse(const std::string& in, JsonValue& out, ParseError& err) {
    if (in.size() > kMaxInput) {
        err.ok = false;
        err.message = "entrada demasiado grande (maximo 64 KiB)";
        err.offset = 0;
        return false;
    }
    Parser ps{in.data(), in.data(), in.data() + in.size(), {}, 0};
    JsonValue v;
    if (!ps.parse_value(v)) {
        err = ps.err;
        return false;
    }
    ps.skip_ws();
    if (ps.p != ps.end) {
        err.ok = false;
        err.message = "datos extra despues del JSON";
        err.offset = ps.off(ps.p);
        return false;
    }
    out = std::move(v);
    return true;
}

} // namespace json
} // namespace mt
