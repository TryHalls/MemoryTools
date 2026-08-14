// http.cpp - Implementacion del parser HTTP minimo (FASE W-3).
#include "http.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "json.h"

namespace mt {
namespace web {

namespace {

// Minusculas en UTF-8: solo transforma A-Z (los nombres de cabecera son
// ASCII); el resto pasa igual.
std::string to_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

HttpRequest fail(int status, const std::string& msg) {
    HttpRequest r;
    r.valid = false;
    r.status = status;
    r.error = msg;
    return r;
}

} // namespace

const std::string* HttpRequest::header(const std::string& name) const {
    for (const auto& h : headers)
        if (h.name == name) return &h.value;
    return nullptr;
}

HttpRequest parse_request_head(const std::string& head) {
    // Fin de cabeceras (obligatorio).
    const size_t he = head.find("\r\n\r\n");
    if (he == std::string::npos) return fail(400, "cabeceras incompletas");
    if (he + 4 > kMaxHeaderBlock) return fail(400, "cabeceras demasiado grandes");

    // Request line: METODO SP objetivo SP version.
    const size_t line_end = head.find("\r\n");
    if (line_end == std::string::npos || line_end == 0)
        return fail(400, "request line invalida");
    if (line_end > kMaxRequestLine) return fail(400, "request line demasiado larga");
    const std::string line = head.substr(0, line_end);

    const size_t sp1 = line.find(' ');
    const size_t sp2 = line.rfind(' ');
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 == sp2)
        return fail(400, "request line invalida");
    if (sp2 - sp1 - 1 == 0) return fail(400, "objetivo vacio");

    HttpRequest r;
    r.method = line.substr(0, sp1);
    const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string version = line.substr(sp2 + 1);

    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        return fail(400, "version HTTP no soportada");
    if (r.method != "GET" && r.method != "POST")
        return fail(405, "metodo no soportado");

    const size_t q = target.find('?');
    if (q != std::string::npos) {
        r.path = target.substr(0, q);
        r.query = target.substr(q + 1);
    } else {
        r.path = target;
    }
    if (r.path.empty() || r.path[0] != '/')
        return fail(400, "path invalido");

    // Cabeceras: nombre: valor (nombres en minusculas), limite de cantidad.
    size_t p = line_end + 2;
    while (p < he) {
        const size_t h_end = head.find("\r\n", p);
        if (h_end == std::string::npos || h_end > he) break;
        const std::string hline = head.substr(p, h_end - p);
        p = h_end + 2;
        if (r.headers.size() >= kMaxHeaders)
            return fail(400, "demasiadas cabeceras");
        const size_t colon = hline.find(':');
        if (colon == std::string::npos) return fail(400, "cabecera invalida");
        const std::string name = to_lower(trim(hline.substr(0, colon)));
        if (name.empty()) return fail(400, "cabecera invalida");
        r.headers.push_back(HttpHeader{name, trim(hline.substr(colon + 1))});
    }

    // Content-Length: solo digitos; limite de 64 KiB -> 413.
    if (const std::string* cl = r.header("content-length")) {
        if (!all_digits(*cl)) return fail(400, "content-length invalido");
        errno = 0;
        char* end = nullptr;
        const unsigned long long v = std::strtoull(cl->c_str(), &end, 10);
        if (errno == ERANGE || end == cl->c_str() || *end != '\0')
            return fail(400, "content-length invalido");
        if (v > kMaxBody) return fail(413, "body demasiado grande");
        r.content_length = (size_t)v;
    }

    r.valid = true;
    r.status = 200;
    return r;
}

HttpRequest parse_http_request(const std::string& raw) {
    const size_t he = raw.find("\r\n\r\n");
    if (he == std::string::npos) return fail(400, "cabeceras incompletas");
    HttpRequest r = parse_request_head(raw.substr(0, he + 4));
    if (!r.valid) return r;
    if (r.content_length > 0) {
        const size_t body_start = he + 4;
        if (raw.size() < body_start + r.content_length)
            return fail(400, "body incompleto");
        r.body = raw.substr(body_start, r.content_length);
    }
    return r;
}

const char* http_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
    }
    return "Unknown";
}

std::string make_response(int status, const std::string& content_type,
                          const std::string& body) {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " +
                      http_reason(status) + "\r\n";
    out += "Content-Type: " + content_type + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "\r\n";
    out += body;
    return out;
}

int check_request_security(const HttpRequest& req, const std::string& token,
                           uint16_t port) {
    const std::string* host = req.header("host");
    if (!host) return 400;
    const std::string port_s = std::to_string(port);
    if (*host != "127.0.0.1:" + port_s && *host != "localhost:" + port_s)
        return 400;
    const std::string* t = req.header("x-memorytool-token");
    if (!t || *t != token) return 401;
    return 0;
}

std::string json_error(const std::string& message) {
    using namespace mt::json;
    return write(JsonValue::make_object(
        {{"error", JsonValue::make_string(message)}}));
}

} // namespace web
} // namespace mt
