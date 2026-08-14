// test_http.cpp - Tests del parser HTTP minimo y de la seguridad local
// (FASE W-3). No abre puertos: prueba parse_request_head/parse_http_request,
// make_response y check_request_security con datos puros.
//
// Compilar:
//   g++ -std=c++17 -O2 -Wall -Wextra -I src
//       tests/test_http.cpp src/web/http.cpp src/web/json.cpp
//       -o build/test_http
// Ejecutar: ./build/test_http   (0 = exito, !=0 = fallo)
#include "web/http.h"
#include "web/json.h"

#include <cstdio>
#include <string>

using namespace mt::web;

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

// GET simple con query y cabeceras.
static void test_get() {
    const std::string raw =
        "GET /api/status?x=1 HTTP/1.1\r\n"
        "Host: 127.0.0.1:8080\r\n"
        "X-MemoryTool-Token: abc\r\n"
        "\r\n";
    const HttpRequest r = parse_http_request(raw);
    CHECK(r.valid);
    CHECK_EQ(r.status, 200);
    CHECK(r.method == "GET");
    CHECK(r.path == "/api/status");
    CHECK(r.query == "x=1");
    CHECK_EQ(r.headers.size(), 2);
    // nombres de cabecera en minusculas, busqueda case-insensitive
    CHECK(r.header("host") != nullptr);
    CHECK(*r.header("host") == "127.0.0.1:8080");
    CHECK(r.header("X-MEMORYTOOL-TOKEN") == nullptr); // la busqueda es en minusculas
    CHECK(r.header("x-memorytool-token") != nullptr);
    CHECK(*r.header("x-memorytool-token") == "abc");
    CHECK_EQ(r.content_length, 0);
    CHECK(r.body.empty());
}

// POST con Content-Length y body.
static void test_post() {
    const std::string raw =
        "POST /api/jobs/3/cancel HTTP/1.1\r\n"
        "Host: 127.0.0.1:9000\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    const HttpRequest r = parse_http_request(raw);
    CHECK(r.valid);
    CHECK(r.method == "POST");
    CHECK(r.path == "/api/jobs/3/cancel");
    CHECK_EQ(r.content_length, 5);
    CHECK(r.body == "hello");
}

// GET sin query.
static void test_no_query() {
    const HttpRequest r = parse_http_request(
        "GET /api/processes HTTP/1.1\r\nHost: 127.0.0.1:1\r\n\r\n");
    CHECK(r.valid);
    CHECK(r.path == "/api/processes");
    CHECK(r.query.empty());
}

// HTTP/1.0 aceptado.
static void test_http10() {
    const HttpRequest r = parse_http_request(
        "GET / HTTP/1.0\r\n\r\n");
    CHECK(r.valid);
}

// Request line malformada -> 400.
static void test_malformed() {
    // sin espacios
    HttpRequest r = parse_http_request("GET/ HTTP/1.1\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // sin version
    r = parse_http_request("GET /api/status\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // version no soportada
    r = parse_http_request("GET / HTTP/2.0\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // sin fin de cabeceras
    r = parse_http_request("GET / HTTP/1.1\r\nHost: x");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // path vacio
    r = parse_http_request("GET  HTTP/1.1\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // cabecera sin ':'
    r = parse_http_request("GET / HTTP/1.1\r\nMalformada\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
}

// Metodo desconocido -> 405 (sin que el router tenga que decidir).
static void test_method() {
    const HttpRequest r = parse_http_request("DELETE /api/status HTTP/1.1\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 405);
}

// Content-Length invalido -> 400; demasiado grande -> 413.
static void test_content_length() {
    HttpRequest r = parse_http_request(
        "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    r = parse_http_request("POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
    // 64 KiB exacto -> OK (sin body real -> body incompleto; validamos el limite)
    r = parse_http_request("POST / HTTP/1.1\r\nContent-Length: 65536\r\n\r\n");
    CHECK(!r.valid); // body incompleto (no hay 64 KiB en 'raw')
    CHECK_EQ(r.status, 400);
    // > 64 KiB -> 413 (antes de mirar el body)
    r = parse_http_request("POST / HTTP/1.1\r\nContent-Length: 65537\r\n\r\n");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 413);
    // body incompleto con CL pequena
    r = parse_http_request("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc");
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
}

// Body demasiado grande (limite real, no solo el header).
static void test_body_too_large() {
    const size_t n = kMaxBody + 1;
    std::string raw = "POST / HTTP/1.1\r\nContent-Length: " +
                      std::to_string(n) + "\r\n\r\n";
    raw.append(n, 'x');
    const HttpRequest r = parse_http_request(raw);
    CHECK(!r.valid);
    CHECK_EQ(r.status, 413);
}

// Cabeceras demasiado grandes -> 400.
static void test_headers_too_large() {
    std::string raw = "GET / HTTP/1.1\r\n";
    const std::string big(kMaxHeaderBlock, 'a');
    raw += "X-Big: " + big + "\r\n\r\n";
    const HttpRequest r = parse_http_request(raw);
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
}

// Demasiadas cabeceras -> 400.
static void test_too_many_headers() {
    std::string raw = "GET / HTTP/1.1\r\n";
    for (size_t i = 0; i < kMaxHeaders + 1; ++i)
        raw += "X-H" + std::to_string(i) + ": v\r\n";
    raw += "\r\n";
    const HttpRequest r = parse_http_request(raw);
    CHECK(!r.valid);
    CHECK_EQ(r.status, 400);
}

// make_response: status line, Content-Type, Content-Length exacto, cierre.
static void test_response() {
    const std::string resp = make_response(200, "application/json", "{\"a\":1}");
    CHECK(resp.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(resp.find("Content-Type: application/json\r\n") != std::string::npos);
    CHECK(resp.find("Content-Length: 7\r\n") != std::string::npos);
    CHECK(resp.find("Connection: close\r\n") != std::string::npos);
    CHECK(resp.find("\r\n\r\n{\"a\":1}") != std::string::npos);
    // codigos de error
    CHECK(make_response(404, "application/json", "{}")
              .rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
    CHECK(make_response(413, "application/json", "{}")
              .rfind("HTTP/1.1 413 Payload Too Large\r\n", 0) == 0);
}

// check_request_security: token y Host.
static void test_security() {
    const std::string tok = "0123456789abcdef0123456789abcdef";
    const uint16_t port = 8080;

    // OK: Host loopback + token correcto
    HttpRequest ok = parse_http_request(
        "GET /api/status HTTP/1.1\r\n"
        "Host: 127.0.0.1:8080\r\n"
        "X-MemoryTool-Token: " + tok + "\r\n\r\n");
    CHECK_EQ(check_request_security(ok, tok, port), 0);

    // localhost aceptado
    HttpRequest lh = parse_http_request(
        "GET / HTTP/1.1\r\nHost: localhost:8080\r\n"
        "X-MemoryTool-Token: " + tok + "\r\n\r\n");
    CHECK_EQ(check_request_security(lh, tok, port), 0);

    // token ausente -> 401
    HttpRequest nt = parse_http_request("GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n");
    CHECK_EQ(check_request_security(nt, tok, port), 401);

    // token incorrecto -> 401
    HttpRequest wt = parse_http_request(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nX-MemoryTool-Token: wrong\r\n\r\n");
    CHECK_EQ(check_request_security(wt, tok, port), 401);

    // Host ausente -> 400
    HttpRequest nh = parse_http_request(
        "GET / HTTP/1.1\r\nX-MemoryTool-Token: " + tok + "\r\n\r\n");
    CHECK_EQ(check_request_security(nh, tok, port), 400);

    // Host no loopback -> 400
    HttpRequest wh = parse_http_request(
        "GET / HTTP/1.1\r\nHost: evil.com\r\n"
        "X-MemoryTool-Token: " + tok + "\r\n\r\n");
    CHECK_EQ(check_request_security(wh, tok, port), 400);

    // Host con puerto distinto -> 400
    HttpRequest wp = parse_http_request(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:9999\r\n"
        "X-MemoryTool-Token: " + tok + "\r\n\r\n");
    CHECK_EQ(check_request_security(wp, tok, port), 400);
}

// json_error: cuerpo JSON valido con la clave "error".
static void test_json_error() {
    const std::string s = json_error("token invalido");
    mt::json::JsonValue v;
    mt::json::ParseError e;
    CHECK(mt::json::parse(s, v, e));
    const auto* err = v.get("error");
    CHECK(err != nullptr);
    CHECK(err->is_string());
    CHECK(*err->as_string() == "token invalido");
}

int main() {
    test_get();
    test_post();
    test_no_query();
    test_http10();
    test_malformed();
    test_method();
    test_content_length();
    test_body_too_large();
    test_headers_too_large();
    test_too_many_headers();
    test_response();
    test_security();
    test_json_error();

    std::printf("\n== test_http: %d checks, %d fallos ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
