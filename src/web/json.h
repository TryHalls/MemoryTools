// json.h - Representacion y serializacion JSON minima (FASE W-1).
//
// Infraestructura headless para la futura Web UI local: un subconjunto de
// JSON suficiente para la API (null / bool / string / int64 / uint64 /
// double / array / object) con:
//   - escritor que escapa correctamente (", \, \n, \r, \t y control < 0x20)
//   - parser con limites de seguridad (profundidad, longitud de string y
//     tamano de entrada) y errores estructurados (nunca excepciones)
//
// Los valores que representan direcciones o uint64 grandes NO deben
// serializarse como numero JSON (JS pierde precision por encima de 2^53):
// la API los pondra como STRING usando make_string / escape_string cuando
// corresponda.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mt {
namespace json {

// Limites de seguridad del parser (entradas de red/API).
inline constexpr size_t kMaxDepth = 32;          // anidamiento maximo
inline constexpr size_t kMaxString = 4096;       // longitud maxima de string
inline constexpr size_t kMaxInput = 64u * 1024u; // tamano maximo del body (64 KiB)
inline constexpr size_t kMaxNumberChars = 64;    // longitud maxima de un numero

// Un valor JSON. Por valor (sin punteros): cada nodo posee a sus hijos.
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;
    enum class Type {
        Null = 0,
        Bool = 1,
        Int = 2,    // int64
        UInt = 3,   // uint64
        Double = 4,
        String = 5,
        Array = 6,
        Object = 7,
    };

    // --- constructores de conveniencia ------------------------------------
    static JsonValue make_null();
    static JsonValue make_bool(bool b);
    static JsonValue make_int(int64_t i);
    static JsonValue make_uint(uint64_t u);
    static JsonValue make_double(double d);
    static JsonValue make_string(std::string s);
    static JsonValue make_array(Array a);
    static JsonValue make_object(Object o);

    JsonValue() = default; // null

    Type type() const;
    bool is_null() const { return type() == Type::Null; }
    bool is_string() const { return type() == Type::String; }

    // --- accesores (nunca lanzan; valor por defecto si el tipo no aplica) --
    bool as_bool(bool def = false) const;
    int64_t as_int(int64_t def = 0) const;
    uint64_t as_uint(uint64_t def = 0) const;
    double as_double(double def = 0.0) const;
    // Devuelve nullptr si no es string.
    const std::string* as_string() const;
    // Devuelven nullptr si no son array/object.
    const Array* as_array() const;
    const Object* as_object() const;
    // Busqueda por clave en objects (nullptr si no existe o no es object).
    const JsonValue* get(const std::string& key) const;

private:
    std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string,
                 Array, Object> v_;
};

// Serializa a JSON. Escapa ", \, \n, \r, \t y control < 0x20 (\u00XX).
// NaN/Inf se escriben como null (JSON no los admite). No lanza.
std::string write(const JsonValue& v);

// Escapa una cadena (con comillas) para incrustarla dentro de un JSON.
std::string escape_string(const std::string& s);

// Error estructurado del parser (nunca excepciones).
struct ParseError {
    bool ok = true;
    std::string message; // en espanol, orientado al usuario
    size_t offset = 0;   // posicion (bytes) del error en la entrada
};

// Parsea un JSON COMPLETO (todo el body, sin sobrantes). Aplica
// kMaxInput / kMaxDepth / kMaxString / kMaxNumberChars.
bool parse(const std::string& in, JsonValue& out, ParseError& err);

} // namespace json
} // namespace mt
