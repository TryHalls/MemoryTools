// test_types.cpp - Tests unitarios de las funciones puras de src/types.h.
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_types.cpp -o build/test_types
// Ejecutar:  ./build/test_types        (0 = exito, !=0 = fallo)
//
// Los tests fijan la semantica ACTUAL de types.h (no la "ideal"): si un
// comportamiento resulta discutible se documenta en el informe, no se
// modifica el core para hacer pasar el test.
#include "types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace mt;

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
            std::printf("FALLO %s:%d: %s == %s (%lld != %lld)\n",                        \
                        __FILE__, __LINE__, #a, #b, _a, _b);                             \
        }                                                                                \
    } while (0)

#define CHECK_BITS(a, b)                                                                 \
    do {                                                                                 \
        uint64_t _a = (uint64_t)(a);                                                     \
        uint64_t _b = (uint64_t)(b);                                                     \
        if (_a == _b) {                                                                  \
            ++g_pass;                                                                    \
        } else {                                                                         \
            ++g_fail;                                                                    \
            std::printf("FALLO %s:%d: %s == %s (0x%llx != 0x%llx)\n",                    \
                        __FILE__, __LINE__, #a, #b,                                      \
                        (unsigned long long)_a, (unsigned long long)_b);                 \
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
            std::printf("FALLO %s:%d: %s == %s (\"%s\" != \"%s\")\n",                    \
                        __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str());             \
        }                                                                                \
    } while (0)

// ---------------------------------------------------------------------------
// parse_value: enteros con signo
// ---------------------------------------------------------------------------
static void test_i8() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::I8, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("1", DataType::I8, v); CHECK(ok); CHECK_EQ(v.bits, 1);
    ok = parse_value("-1", DataType::I8, v); CHECK(ok); CHECK_BITS(v.bits, 0xFF);
    CHECK_EQ(value_as_int(v, DataType::I8), -1);
    ok = parse_value("-128", DataType::I8, v); CHECK(ok); CHECK_BITS(v.bits, 0x80);
    CHECK_EQ(value_as_int(v, DataType::I8), INT8_MIN);
    ok = parse_value("127", DataType::I8, v); CHECK(ok); CHECK_EQ(v.bits, 127);
    CHECK_EQ(value_as_int(v, DataType::I8), INT8_MAX);
    // fuera de rango -> rechazado
    ok = parse_value("128", DataType::I8, v); CHECK(!ok);
    ok = parse_value("-129", DataType::I8, v); CHECK(!ok);
    // hex dentro de rango firmado: 0x7F = 127; 0xFF (255) excede INT8_MAX
    ok = parse_value("0x7F", DataType::I8, v); CHECK(ok); CHECK_EQ(v.bits, 127);
    ok = parse_value("0xFF", DataType::I8, v); CHECK(!ok);
    // basura -> rechazado
    ok = parse_value("abc", DataType::I8, v); CHECK(!ok);
    ok = parse_value("12abc", DataType::I8, v); CHECK(!ok);
    ok = parse_value("", DataType::I8, v); CHECK(!ok);
}

static void test_i16() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::I16, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("-1", DataType::I16, v); CHECK(ok); CHECK_BITS(v.bits, 0xFFFF);
    CHECK_EQ(value_as_int(v, DataType::I16), -1);
    ok = parse_value("-32768", DataType::I16, v); CHECK(ok); CHECK_BITS(v.bits, 0x8000);
    CHECK_EQ(value_as_int(v, DataType::I16), INT16_MIN);
    ok = parse_value("32767", DataType::I16, v); CHECK(ok); CHECK_BITS(v.bits, 0x7FFF);
    CHECK_EQ(value_as_int(v, DataType::I16), INT16_MAX);
    ok = parse_value("32768", DataType::I16, v); CHECK(!ok);
    ok = parse_value("-32769", DataType::I16, v); CHECK(!ok);
}

static void test_i32() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::I32, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("-1", DataType::I32, v); CHECK(ok); CHECK_BITS(v.bits, 0xFFFFFFFF);
    CHECK_EQ(value_as_int(v, DataType::I32), -1);
    ok = parse_value("-2147483648", DataType::I32, v); CHECK(ok);
    CHECK_BITS(v.bits, 0x80000000);
    CHECK_EQ(value_as_int(v, DataType::I32), INT32_MIN);
    ok = parse_value("2147483647", DataType::I32, v); CHECK(ok);
    CHECK_BITS(v.bits, 0x7FFFFFFF);
    CHECK_EQ(value_as_int(v, DataType::I32), INT32_MAX);
    ok = parse_value("2147483648", DataType::I32, v); CHECK(!ok);
    ok = parse_value("-2147483649", DataType::I32, v); CHECK(!ok);
    // hex
    ok = parse_value("0x10", DataType::I32, v); CHECK(ok); CHECK_EQ(v.bits, 16);
    ok = parse_value("0x7FFFFFFF", DataType::I32, v); CHECK(ok);
    ok = parse_value("0xFFFFFFFF", DataType::I32, v); CHECK(!ok); // excede INT32_MAX
}

static void test_i64() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::I64, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("-1", DataType::I64, v); CHECK(ok);
    CHECK_BITS(v.bits, 0xFFFFFFFFFFFFFFFFull);
    CHECK_EQ(value_as_int(v, DataType::I64), -1);
    ok = parse_value("-9223372036854775808", DataType::I64, v); CHECK(ok);
    CHECK_BITS(v.bits, 0x8000000000000000ull);
    CHECK_EQ(value_as_int(v, DataType::I64), INT64_MIN);
    ok = parse_value("9223372036854775807", DataType::I64, v); CHECK(ok);
    CHECK_BITS(v.bits, 0x7FFFFFFFFFFFFFFFull);
    CHECK_EQ(value_as_int(v, DataType::I64), INT64_MAX);
    // HALLAZGO (documentado): I64 NO comprueba rango/overflow. "9223372036854775808"
    // (INT64_MAX+1) hace que strtoll sature a LLONG_MAX con ERANGE y parse_value
    // devuelve true con bits = 0x7FFFFFFFFFFFFFFF, no el valor real.
    ok = parse_value("9223372036854775808", DataType::I64, v);
    CHECK(ok); // comportamiento actual: aceptado (silenciosamente saturado)
    CHECK_BITS(v.bits, 0x7FFFFFFFFFFFFFFFull);
}

// ---------------------------------------------------------------------------
// parse_value: enteros sin signo
// ---------------------------------------------------------------------------
static void test_u8() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::U8, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("1", DataType::U8, v); CHECK(ok); CHECK_EQ(v.bits, 1);
    ok = parse_value("255", DataType::U8, v); CHECK(ok); CHECK_EQ(v.bits, 255);
    CHECK_EQ(value_as_int(v, DataType::U8), UINT8_MAX);
    ok = parse_value("256", DataType::U8, v); CHECK(!ok);
    ok = parse_value("-1", DataType::U8, v); CHECK(!ok);
    ok = parse_value("0xFF", DataType::U8, v); CHECK(ok); CHECK_EQ(v.bits, 255);
}

static void test_u16() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::U16, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("65535", DataType::U16, v); CHECK(ok); CHECK_BITS(v.bits, 0xFFFF);
    CHECK_EQ(value_as_int(v, DataType::U16), UINT16_MAX);
    ok = parse_value("65536", DataType::U16, v); CHECK(!ok);
    ok = parse_value("-1", DataType::U16, v); CHECK(!ok);
}

static void test_u32() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::U32, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("4294967295", DataType::U32, v); CHECK(ok);
    CHECK_BITS(v.bits, 0xFFFFFFFFull);
    CHECK_EQ(value_as_int(v, DataType::U32), UINT32_MAX);
    ok = parse_value("4294967296", DataType::U32, v); CHECK(!ok);
    ok = parse_value("-1", DataType::U32, v); CHECK(!ok);
    ok = parse_value("0xFFFFFFFF", DataType::U32, v); CHECK(ok);
}

static void test_u64() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::U64, v); CHECK(ok); CHECK_EQ(v.bits, 0);
    ok = parse_value("-1", DataType::U64, v); CHECK(!ok);
    // HALLAZGO (documentado): U64 tampoco puede representar UINT64_MAX por
    // texto: strtoll satura a LLONG_MAX con ERANGE y, al no haber control de
    // errno, parse_value devuelve true con bits = 0x7FFFFFFFFFFFFFFF.
    ok = parse_value("18446744073709551615", DataType::U64, v);
    CHECK(ok); // comportamiento actual: aceptado (saturado)
    CHECK_BITS(v.bits, 0x7FFFFFFFFFFFFFFFull);
    CHECK_EQ(v.bits, (uint64_t)std::numeric_limits<long long>::max());
    // Representable de forma exacta: 0x7FFFFFFFFFFFFFFF
    ok = parse_value("0x7FFFFFFFFFFFFFFF", DataType::U64, v); CHECK(ok);
    CHECK_BITS(v.bits, 0x7FFFFFFFFFFFFFFFull);
}

// ---------------------------------------------------------------------------
// parse_value: float / double
// ---------------------------------------------------------------------------
static void test_float_double() {
    bool ok = false;
    Value v;
    ok = parse_value("0", DataType::F32, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F32) == 0.0);
    ok = parse_value("1.5", DataType::F32, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F32) == 1.5);       // 1.5 es exacto en f32
    ok = parse_value("-2.25", DataType::F32, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F32) == -2.25);
    ok = parse_value("0.1", DataType::F32, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F32) == (double)0.1f); // aproximacion f32 exacta
    ok = parse_value("1e10", DataType::F32, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F32) == (double)1e10f);

    ok = parse_value("0", DataType::F64, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F64) == 0.0);
    ok = parse_value("3.14159", DataType::F64, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F64) == 3.14159);
    ok = parse_value("-0.5", DataType::F64, v); CHECK(ok);
    CHECK(value_as_double(v, DataType::F64) == -0.5);

    // NaN es parseable (strtod acepta "nan")
    ok = parse_value("nan", DataType::F64, v); CHECK(ok);
    CHECK(std::isnan(value_as_double(v, DataType::F64)));
    // basura
    ok = parse_value("abc", DataType::F32, v); CHECK(!ok);
    ok = parse_value("1.5x", DataType::F32, v); CHECK(!ok);
    ok = parse_value("", DataType::F64, v); CHECK(!ok);
}

// ---------------------------------------------------------------------------
// value_from_bytes: ancho, zero-padding, little-endian, clamp
// ---------------------------------------------------------------------------
static void test_value_from_bytes() {
    const uint8_t b1[8] = {0xAB};
    CHECK_BITS(value_from_bytes(b1, 1).bits, 0xAB);

    const uint8_t b2[8] = {0x34, 0x12};
    CHECK_BITS(value_from_bytes(b2, 2).bits, 0x1234); // little-endian

    const uint8_t b4[8] = {0x78, 0x56, 0x34, 0x12};
    CHECK_BITS(value_from_bytes(b4, 4).bits, 0x12345678);

    const uint8_t b8[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK_BITS(value_from_bytes(b8, 8).bits, 0x0807060504030201ull);

    // zero-padding: 1 byte -> los 7 bytes superiores quedan a cero
    const uint8_t bf[8] = {0xFF};
    CHECK_BITS(value_from_bytes(bf, 1).bits, 0xFF);

    // clamp: w > 8 usa solo los primeros 8 bytes
    const uint8_t b10[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    CHECK_BITS(value_from_bytes(b10, 10).bits, 0x0807060504030201ull);

    // interpretacion despues del copiado
    const uint8_t ff[8] = {0xFF, 0xFF};
    CHECK_EQ(value_as_int(value_from_bytes(ff, 2), DataType::I16), -1);
    CHECK_EQ(value_as_int(value_from_bytes(ff, 2), DataType::U16), 65535);
    // 0x3FC00000 en LE = 1.5f
    const uint8_t f15[8] = {0x00, 0x00, 0xC0, 0x3F};
    CHECK(value_as_double(value_from_bytes(f15, 4), DataType::F32) == 1.5);
}

// ---------------------------------------------------------------------------
// value_compare: igualdad, orden, signed/unsigned, flotantes, NaN
// ---------------------------------------------------------------------------
static Value parse_or(DataType t, const std::string& s) {
    Value v;
    bool ok = parse_value(s, t, v);
    CHECK(ok);
    return v;
}

static void test_compare_ints() {
    Value a, b;
    // iguales
    CHECK_EQ(value_compare(parse_or(DataType::I32, "5"), parse_or(DataType::I32, "5"), DataType::I32), 0);
    CHECK(value_equal(parse_or(DataType::I32, "5"), parse_or(DataType::I32, "5"), DataType::I32));
    // orden
    CHECK_EQ(value_compare(parse_or(DataType::I32, "3"), parse_or(DataType::I32, "7"), DataType::I32), -1);
    CHECK_EQ(value_compare(parse_or(DataType::I32, "7"), parse_or(DataType::I32, "3"), DataType::I32), 1);
    // >= / <= se derivan del 0
    CHECK_EQ(value_compare(parse_or(DataType::I32, "5"), parse_or(DataType::I32, "5"), DataType::I32), 0);
    // !=
    CHECK(!value_equal(parse_or(DataType::I32, "5"), parse_or(DataType::I32, "6"), DataType::I32));
    // signed: I8 -1 < 1
    CHECK_EQ(value_compare(parse_or(DataType::I8, "-1"), parse_or(DataType::I8, "1"), DataType::I8), -1);
    CHECK_EQ(value_compare(parse_or(DataType::I8, "-2"), parse_or(DataType::I8, "-1"), DataType::I8), -1);
    // unsigned: U8 200 > 100
    CHECK_EQ(value_compare(parse_or(DataType::U8, "200"), parse_or(DataType::U8, "100"), DataType::U8), 1);
    // I64 con bits 0xFF..FF = -1 firmado: menor que 0
    a.bits = 0xFFFFFFFFFFFFFFFFull;
    b.bits = 0;
    CHECK_EQ(value_compare(a, b, DataType::I64), -1);
    // U64 con los mismos bits: comparacion unsigned -> mayor que 0
    CHECK_EQ(value_compare(a, b, DataType::U64), 1);
    // U32 max vs 1
    CHECK_EQ(value_compare(parse_or(DataType::U32, "4294967295"),
                           parse_or(DataType::U32, "1"), DataType::U32), 1);
}

static void test_compare_floats() {
    Value a = parse_or(DataType::F64, "1.0");
    Value b = parse_or(DataType::F64, "2.0");
    Value c = parse_or(DataType::F64, "1.5");
    CHECK_EQ(value_compare(a, b, DataType::F64), -1);
    CHECK_EQ(value_compare(b, a, DataType::F64), 1);
    CHECK_EQ(value_compare(c, c, DataType::F64), 0);
    CHECK(value_equal(c, c, DataType::F64));
    // f32: 0.1f es igual a si mismo (misma aproximacion)
    Value f1 = parse_or(DataType::F32, "0.1");
    Value f2 = parse_or(DataType::F32, "0.1");
    CHECK_EQ(value_compare(f1, f2, DataType::F32), 0);
    // HALLAZGO (documentado): NaN no es ni < ni > que nada, asi que
    // value_compare(NaN, x) == 0 para cualquier x: NaN "es igual" a todo.
    bool ok = false;
    Value nan;
    ok = parse_value("nan", DataType::F64, nan); CHECK(ok);
    CHECK(std::isnan(value_as_double(nan, DataType::F64)));
    CHECK_EQ(value_compare(nan, a, DataType::F64), 0);
    CHECK_EQ(value_compare(nan, nan, DataType::F64), 0);
}

// ---------------------------------------------------------------------------
// parse_type / type_size / type_is_float / value_to_string
// ---------------------------------------------------------------------------
static void test_types_meta() {
    DataType t = DataType::I32;
    CHECK(parse_type("int32", t)); CHECK(t == DataType::I32);
    CHECK(parse_type("int", t));   CHECK(t == DataType::I32);
    CHECK(parse_type("i8", t));    CHECK(t == DataType::I8);
    CHECK(parse_type("uint8", t)); CHECK(t == DataType::U8);
    CHECK(parse_type("byte", t));  CHECK(t == DataType::U8);
    CHECK(parse_type("int16", t)); CHECK(t == DataType::I16);
    CHECK(parse_type("uint16", t));CHECK(t == DataType::U16);
    CHECK(parse_type("u32", t));   CHECK(t == DataType::U32);
    CHECK(parse_type("uint", t));  CHECK(t == DataType::U32);
    CHECK(parse_type("int64", t)); CHECK(t == DataType::I64);
    CHECK(parse_type("long", t));  CHECK(t == DataType::I64);
    CHECK(parse_type("float", t)); CHECK(t == DataType::F32);
    CHECK(parse_type("double", t));CHECK(t == DataType::F64);
    CHECK(!parse_type("nope", t));
    CHECK(!parse_type("", t));

    CHECK_EQ(type_size(DataType::I8), 1);
    CHECK_EQ(type_size(DataType::U8), 1);
    CHECK_EQ(type_size(DataType::I16), 2);
    CHECK_EQ(type_size(DataType::U16), 2);
    CHECK_EQ(type_size(DataType::I32), 4);
    CHECK_EQ(type_size(DataType::U32), 4);
    CHECK_EQ(type_size(DataType::F32), 4);
    CHECK_EQ(type_size(DataType::I64), 8);
    CHECK_EQ(type_size(DataType::U64), 8);
    CHECK_EQ(type_size(DataType::F64), 8);

    CHECK(type_is_float(DataType::F32));
    CHECK(type_is_float(DataType::F64));
    CHECK(!type_is_float(DataType::I32));
    CHECK(!type_is_float(DataType::U64));

    CHECK_STR(value_to_string(parse_or(DataType::I32, "12345"), DataType::I32), "12345");
    CHECK_STR(value_to_string(parse_or(DataType::I32, "-123"), DataType::I32), "-123");
    CHECK_STR(value_to_string(parse_or(DataType::U32, "4294967295"), DataType::U32), "4294967295");
    CHECK_STR(value_to_string(parse_or(DataType::F32, "1.5"), DataType::F32), "1.5");
    CHECK_STR(value_to_string(parse_or(DataType::F64, "-2.25"), DataType::F64), "-2.25");
    CHECK_STR(value_to_string(parse_or(DataType::I8, "-1"), DataType::I8), "-1");
    Value u64max;
    u64max.bits = 0xFFFFFFFFFFFFFFFFull;
    CHECK_STR(value_to_string(u64max, DataType::U64), "18446744073709551615");
}

// HALLAZGO adicional (documentado, no probado como fallo): parse_value no
// soporta hex negativo. "0x10" se detecta por el prefijo "0x" al INICIO; con
// "-0x10" la base queda en 10 y strtoll se detiene en la 'x' (basura final),
// asi que devuelve false. No se modifica: es comportamiento actual.
static void note_negative_hex() {
    bool ok = false;
    Value v;
    ok = parse_value("-0x10", DataType::I32, v);
    std::printf("  [nota] parse_value(\"-0x10\", i32) -> %s (hex negativo no soportado)\n",
                ok ? "true" : "false");
}

int main() {
    test_i8();
    test_i16();
    test_i32();
    test_i64();
    test_u8();
    test_u16();
    test_u32();
    test_u64();
    test_float_double();
    test_value_from_bytes();
    test_compare_ints();
    test_compare_floats();
    test_types_meta();
    note_negative_hex();

    std::printf("\n== test_types: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
