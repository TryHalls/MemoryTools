// http.h - Parser HTTP/1.0-1.1 minimo (FASE W-3).
//
// Separado de los sockets para poder testear el parseo sin abrir puertos:
//   - parse_request_head(): request line + headers (hasta \r\n\r\n)
//   - parse_http_request(): head + body segun Content-Length (tests)
//   - make_response(): respuesta HTTP/1.1 minima con Content-Length
//   - check_request_security(): token + Host (puro, testeable)
//
// Limites de seguridad aplicados SIEMPRE (nunca allocations ilimitadas
// basadas en datos del cliente):
//   - request line <= 8 KiB
//   - bloque de cabeceras <= 8 KiB
//   - body <= 64 KiB (413)
//   - numero maximo de cabeceras
//   - metodo != GET/POST -> 405; request malformada -> 400
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mt {
namespace web {

inline constexpr size_t kMaxRequestLine = 8192;   // bytes de la request line
inline constexpr size_t kMaxHeaderBlock = 8192;   // bytes de cabeceras (+ CRLFCRLF)
inline constexpr size_t kMaxBody = 64u * 1024u;   // 64 KiB
inline constexpr size_t kMaxHeaders = 32;         // numero maximo de cabeceras
inline constexpr size_t kMaxConnections = 8;      // concurrencia maxima del server

struct HttpHeader {
    std::string name;  // en minusculas
    std::string value;
};

// Request HTTP parseada. Si !valid, 'status' es el codigo HTTP de error
// (400 malformada/incompleta, 405 metodo no soportado, 413 body demasiado
// grande) y 'error' un mensaje legible.
struct HttpRequest {
    bool valid = false;
    int status = 400;
    std::string error;
    std::string method;  // GET | POST
    std::string path;    // sin query
    std::string query;   // despues de '?' (sin decodificar)
    std::vector<HttpHeader> headers;
    std::string body;
    size_t content_length = 0;

    // Valor de una cabecera por nombre (en minusculas); nullptr si no existe.
    const std::string* header(const std::string& name) const;
};

// Parsea solo la cabecera (request line + headers hasta \r\n\r\n inclusive).
// Comprueba request line, metodo (GET/POST), version, path, cabeceras,
// Content-Length (sintaxis y limite de 64 KiB -> 413). NO comprueba el body.
HttpRequest parse_request_head(const std::string& head);

// Parsea una request COMPLETA (head + body segun Content-Length). El body
// debe venir incluido en 'raw'; si falta -> 400 "body incompleto".
HttpRequest parse_http_request(const std::string& raw);

// Cabecera de respuesta minima HTTP/1.1: status line + Content-Type +
// Content-Length + Connection: close + cuerpo.
std::string make_response(int status, const std::string& content_type,
                          const std::string& body);

// Texto de la razon para un codigo HTTP conocido.
const char* http_reason(int status);

// Seguridad local (puro): 0 = OK; 401 = token invalido/ausente;
// 400 = Host ausente o distinto de "127.0.0.1:<port>" / "localhost:<port>".
int check_request_security(const HttpRequest& req, const std::string& token,
                           uint16_t port);

// Cuerpo JSON de error: {"error": "<mensaje>"}.
std::string json_error(const std::string& message);

} // namespace web
} // namespace mt
