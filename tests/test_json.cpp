// test_json.cpp - Tests unitarios del JSON minimo (src/web/json.h).
//
// Cubre: objects, arrays, anidamiento, strings, escapes, unicode basico,
// bool/null, numeros (int/uint/double), uint64 como string, valores vacios,
// JSON invalido, profundidad > 32, string > 4096, body > 64 KiB y
// serializacion exacta (round-trip del writer).
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_json.cpp src/web/json.cpp -o build/test_json
// Ejecutar:
//   ./build/test_json      (0 = exito, !=0 = fallo)
#include "web/json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace mt::json;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (cond) {                                                      \
            ++g_pass;                                                    \
        } else {                                                         \
            ++g_fail;                                                    \
            std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        long long _a = (long long)(a);                                                   \
        long long _b = (long long)(b);                                                   \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (%lld != %lld)\n",                         \
                        __FILE__, __LINE__, #a, #b, _a, _b);                             \
        }                                                                                \
    } while (0)

#define CHECK_STR(a, b)                                                                  \
    do {                                                                                 \
        std::string _a = (a);                                                            \
        std::string _b = (b);                                                            \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (\"%s\" != \"%s\")\n",                     \
                        __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str());             \
        }                                                                                \
    } while (0)

// Parsea y devuelve true si tuvo exito.
static bool ok(const std::string& in, JsonValue& out, std::string& errmsg) {
    ParseError err;
    if (!parse(in, out, err)) {
        errmsg = err.message;
        return false;
    }
    return true;
}

static void expect_ok(const std::string& in) {
    JsonValue v;
    std::string errmsg;
    CHECK(ok(in, v, errmsg));
    if (g_fail > 0) return; // no acumular ruido
}

static void expect_fail(const std::string& in, const char* reason) {
    JsonValue v;
    std::string errmsg;
    CHECK(!ok(in, v, errmsg));
    if (!errmsg.empty() && reason)
        CHECK(errmsg.find(reason) != std::string::npos);
}

// --- 1) objects, arrays, anidamiento ----------------------------------------

static void test_objects() {
    JsonValue v;
    std::string errmsg;
    CHECK(ok("{\"a\":1,\"b\":\"x\"}", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Object);
    const auto* o = v.as_object();
    CHECK(o && o->size() == 2);
    const JsonValue* a = v.get("a");
    const JsonValue* b = v.get("b");
    CHECK(a && a->type() == JsonValue::Type::Int && a->as_int() == 1);
    CHECK(b && b->is_string() && *b->as_string() == "x");
    CHECK(v.get("zzz") == nullptr);
    // claves duplicadas: la ultima gana (se guarda en orden)
    JsonValue d;
    CHECK(ok("{\"k\":1,\"k\":2}", d, errmsg));
    CHECK_EQ(d.get("k")->as_int(), 2);

    // object con espacios y saltos de linea
    JsonValue w;
    CHECK(ok("  { \"pid\" : 1234 , \"ok\" : true }  ", w, errmsg));
    CHECK_EQ(w.get("pid")->as_int(), 1234);
    CHECK(w.get("ok")->as_bool());
}

static void test_arrays() {
    JsonValue v;
    std::string errmsg;
    CHECK(ok("[1,2,3]", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Array);
    const auto* a = v.as_array();
    CHECK(a && a->size() == 3);
    CHECK_EQ((*a)[0].as_int(), 1);
    CHECK_EQ((*a)[2].as_int(), 3);
}

static void test_nested() {
    JsonValue v;
    std::string errmsg;
    CHECK(ok("{\"a\":{\"b\":[true,null,\"s\"]}}", v, errmsg));
    const JsonValue* a = v.get("a");
    CHECK(a);
    const JsonValue* b = a->get("b");
    CHECK(b && b->type() == JsonValue::Type::Array);
    const auto& arr = *b->as_array();
    CHECK_EQ(arr.size(), 3u);
    CHECK(arr[0].as_bool());
    CHECK(arr[1].is_null());
    CHECK_STR(*arr[2].as_string(), "s");
}

// --- 2) strings, escapes, unicode -------------------------------------------

static void test_strings() {
    JsonValue v;
    std::string errmsg;
    // vacia
    CHECK(ok("\"\"", v, errmsg));
    CHECK(v.is_string() && v.as_string()->empty());
    // escapes basicos
    CHECK(ok("\"a\\\"b\"", v, errmsg));
    CHECK_STR(*v.as_string(), "a\"b");
    CHECK(ok("\"a\\\\b\"", v, errmsg));
    CHECK_STR(*v.as_string(), "a\\b");
    CHECK(ok("\"\\n\\r\\t\"", v, errmsg));
    CHECK_STR(*v.as_string(), "\n\r\t");
    CHECK(ok("\"\\b\\f\"", v, errmsg));
    CHECK_STR(*v.as_string(), "\b\f");
    CHECK(ok("\"a/b\"", v, errmsg));
    CHECK_STR(*v.as_string(), "a/b");
    // unicode basico: \u00e9 = é (UTF-8 0xC3 0xA9)
    CHECK(ok("\"\\u00e9\"", v, errmsg));
    CHECK_STR(*v.as_string(), "\xc3\xa9");
    // par de suplentes: U+1F600 (0xF0 0x9F 0x98 0x80)
    CHECK(ok("\"\\ud83d\\ude00\"", v, errmsg));
    CHECK_STR(*v.as_string(), "\xf0\x9f\x98\x80");
    // suplente bajo sin alto -> invalido
    expect_fail("\"\\udc00\"", nullptr);
    // suplente alto sin bajo -> invalido
    expect_fail("\"\\ud800\"", nullptr);
    // escape desconocido
    expect_fail("\"\\x\"", "escape");
    // caracter de control sin escapar
    expect_fail("\"a\nb\"", nullptr);
    // string sin cerrar
    expect_fail("\"abc", "sin cerrar");
}

// --- 3) bool, null ----------------------------------------------------------

static void test_bool_null() {
    JsonValue v;
    std::string errmsg;
    CHECK(ok("true", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Bool && v.as_bool());
    CHECK(ok("false", v, errmsg));
    CHECK(!v.as_bool());
    CHECK(ok("null", v, errmsg));
    CHECK(v.is_null());
    // literales rotos
    expect_fail("tru", "literal");
    expect_fail("truex", "literal");
    // JSON es case-sensitive: "True" no es un literal reconocido (la razon
    // estable del mensaje es "valor").
    expect_fail("True", "valor");
    expect_fail("nul", "literal");
}

// --- 4) numeros -------------------------------------------------------------

static void test_numbers() {
    JsonValue v;
    std::string errmsg;
    CHECK(ok("0", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Int && v.as_int() == 0);
    CHECK(ok("-1", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Int && v.as_int() == -1);
    CHECK(ok("9223372036854775807", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Int);
    CHECK_EQ(v.as_int(), INT64_MAX);
    // positivo fuera de int64 -> uint64
    CHECK(ok("18446744073709551615", v, errmsg));
    CHECK(v.type() == JsonValue::Type::UInt);
    CHECK_EQ(v.as_uint(), UINT64_MAX);
    // fuera de uint64 -> invalido
    expect_fail("18446744073709551616", "rango");
    // negativo fuera de int64 -> invalido
    expect_fail("-9223372036854775809", "rango");
    // dobles
    CHECK(ok("1.5", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Double);
    CHECK(v.as_double() == 1.5);
    CHECK(ok("-2.25", v, errmsg));
    CHECK(v.as_double() == -2.25);
    CHECK(ok("1e3", v, errmsg));
    CHECK(v.type() == JsonValue::Type::Double && v.as_double() == 1000.0);
    CHECK(ok("1.5e-2", v, errmsg));
    CHECK(v.as_double() == 0.015);
    // numeros mal formados
    expect_fail("01", "numero");       // cero a la izquierda
    expect_fail("1.", "fraccion");     // punto sin digitos
    expect_fail(".5", "valor");        // no empieza por digito ni '-'
    expect_fail("1e", "exponente");
    expect_fail("1e+", "exponente");
    expect_fail("-", "numero");
    expect_fail("--1", "numero");
}

// --- 5) serializacion exacta (writer) ---------------------------------------

static void test_write() {
    // object simple, orden estable
    JsonValue o = JsonValue::make_object({
        {"a", JsonValue::make_int(1)},
        {"b", JsonValue::make_string("x")},
    });
    CHECK_STR(write(o), "{\"a\":1,\"b\":\"x\"}");

    // strings escapadas
    JsonValue s = JsonValue::make_string("a\"b\\c\nd\t");
    CHECK_STR(write(s), "\"a\\\"b\\\\c\\nd\\t\"");
    // caracter de control 0x01 -> \u0001
    JsonValue c = JsonValue::make_string(std::string("\x01", 1));
    CHECK_STR(write(c), "\"\\u0001\"");

    // array anidado
    JsonValue arr = JsonValue::make_array({
        JsonValue::make_int(1),
        JsonValue::make_null(),
        JsonValue::make_bool(false),
        JsonValue::make_string("hola"),
    });
    CHECK_STR(write(arr), "[1,null,false,\"hola\"]");

    // uint64 max como numero JSON (solo para el writer; la API lo usara como
    // string cuando lo necesite)
    JsonValue u = JsonValue::make_uint(UINT64_MAX);
    CHECK_STR(write(u), "18446744073709551615");

    // uint64/address como STRING (el uso previsto para direcciones)
    JsonValue addr = JsonValue::make_string("0x7f1234567890");
    CHECK_STR(write(addr), "\"0x7f1234567890\"");

    // doubles: 1.5 exacto; NaN -> null
    JsonValue d = JsonValue::make_double(1.5);
    CHECK_STR(write(d), "1.5");
    JsonValue nan = JsonValue::make_double(std::nan(""));
    CHECK_STR(write(nan), "null");
    JsonValue inf = JsonValue::make_double(INFINITY);
    CHECK_STR(write(inf), "null");

    // object vacio, array vacio
    CHECK_STR(write(JsonValue::make_object({})), "{}");
    CHECK_STR(write(JsonValue::make_array({})), "[]");

    // escape_string (util de la API)
    CHECK_STR(escape_string("a\"b"), "\"a\\\"b\"");
}

// --- 6) round-trip parse -> write -------------------------------------------

static void test_roundtrip() {
    const std::string in =
        "{\"pid\":1234,\"name\":\"objetivo\",\"ok\":true,\"vals\":[1,2.5,"
        "\"hola \\u00e9\"],\"nada\":null}";
    JsonValue v;
    std::string errmsg;
    CHECK(ok(in, v, errmsg));
    JsonValue v2;
    CHECK(ok(write(v), v2, errmsg));
    CHECK_STR(write(v2), write(v));
}

// --- 7) limites de seguridad ------------------------------------------------

static void test_limits() {
    // profundidad > 32 -> invalido
    std::string deep;
    for (int i = 0; i < 33; ++i) deep += '[';
    for (int i = 0; i < 33; ++i) deep += ']';
    expect_fail(deep, "profundo");
    // profundidad exactamente 32 -> valido
    std::string ok32;
    for (int i = 0; i < 32; ++i) ok32 += '[';
    for (int i = 0; i < 32; ++i) ok32 += ']';
    expect_ok(ok32);

    // string > 4096 -> invalido
    std::string longstr = "\"" + std::string(4097, 'a') + "\"";
    expect_fail(longstr, "larga");
    // string de exactamente 4096 -> valido
    std::string exact = "\"" + std::string(4096, 'a') + "\"";
    expect_ok(exact);

    // numero demasiado largo (100 digitos) -> invalido
    std::string longnum;
    for (int i = 0; i < 100; ++i) longnum += '9';
    expect_fail(longnum, "largo");

    // body > 64 KiB -> invalido
    std::string big = "[" + std::string(70000, ' ') + "]";
    expect_fail(big, "grande");
    // body de exactamente 64 KiB -> valido (aunque el parseo puede fallar por
    // otros limites; un object vacio rodeado de espacios cabe)
    std::string edge;
    const size_t pad = 64u * 1024u - 2u; // "{}" son 2 bytes
    edge = "{" + std::string(pad, ' ') + "}";
    expect_ok(edge);
}

// --- 8) JSON invalido (estructura) ------------------------------------------

static void test_invalid() {
    expect_fail("", "vacio");
    expect_fail("   ", "vacio");
    expect_fail("{", "object");
    expect_fail("[1,", "array");
    expect_fail("{\"a\":}", "valor");
    expect_fail("{\"a\" 1}", "':'");
    expect_fail("{a:1}", "clave");
    expect_fail("{\"a\":1", "cerrar");
    expect_fail("{\"a\":1,}", "clave");
    expect_fail("[1 2]", "','");
    expect_fail("{}x", "extra");
    expect_fail("[1]x", "extra");
}

int main() {
    test_objects();
    test_arrays();
    test_nested();
    test_strings();
    test_bool_null();
    test_numbers();
    test_write();
    test_roundtrip();
    test_limits();
    test_invalid();
    std::printf("\n== test_json: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
