// server.cpp - Implementacion del servidor HTTP local (FASE W-3).
//
// Solo sockets POSIX + C++ estandar, sin dependencias externas. El socket de
// escucha se crea en 127.0.0.1 (loopback). Cada conexion se atiende en un
// hilo propio (limite kMaxConnections); el cierre de la conexion tras la
// respuesta evita keep-alive complejo.
#include "server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "../process.h"
#include "json.h"

namespace mt {
namespace web {

namespace {

// Hex de 16 bytes aleatorios (token por ejecucion). /dev/urandom con
// fallback a std::random_device.
std::string make_token() {
    unsigned char b[16];
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f) {
        const size_t n = std::fread(b, 1, sizeof b, f);
        std::fclose(f);
        if (n == sizeof b) {
            static const char* hex = "0123456789abcdef";
            std::string out;
            out.reserve(32);
            for (unsigned char c : b) {
                out += hex[c >> 4];
                out += hex[c & 0xf];
            }
            return out;
        }
    }
    std::random_device rd;
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        const unsigned int x = rd() & 0xff;
        static const char* hex = "0123456789abcdef";
        out += hex[x >> 4];
        out += hex[x & 0xf];
    }
    return out;
}

// Lee hasta encontrar el fin de cabeceras (\r\n\r\n) con tope de 8 KiB.
// Devuelve en 'out' TODO lo leido (puede incluir inicio del body).
bool read_request_head(int fd, std::string& out) {
    char buf[4096];
    while (out.find("\r\n\r\n") == std::string::npos) {
        if (out.size() > kMaxHeaderBlock + 4) return false;
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false; // EOF/error sin cabeceras completas
        out.append(buf, (size_t)n);
    }
    return out.size() <= kMaxHeaderBlock + 4;
}

// Escribe todo el buffer (maneja envios parciales). Ignora SIGPIPE.
bool send_all(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n =
            send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

} // namespace

bool WebServer::start(uint16_t port, std::string& err) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        err = "no se pudo crear el socket: " + std::string(strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // Loopback UNICAMENTE: nunca 0.0.0.0 ni ::.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof addr) < 0) {
        err = "no se pudo hacer bind en 127.0.0.1:" + std::to_string(port) +
              ": " + std::string(strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, 8) < 0) {
        err = "listen fallo: " + std::string(strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    // Puerto real (port==0 -> asignado por el kernel).
    socklen_t alen = sizeof addr;
    if (getsockname(listen_fd_, (sockaddr*)&addr, &alen) == 0)
        port_ = ntohs(addr.sin_port);
    else
        port_ = port;

    token_ = make_token();
    return true;
}

void WebServer::run() {
    while (true) {
        const int cfd = accept(listen_fd_, nullptr, nullptr);
        if (cfd < 0) {
            if (listen_fd_ < 0) break; // stop() llamado
            if (errno == EINTR) continue;
            break; // error fatal
        }
        if (conns_ >= (int)kMaxConnections) {
            send_all(cfd, make_response(503, "application/json",
                                        json_error("servidor ocupado")));
            ::close(cfd);
            continue;
        }
        conns_++;
        std::thread([this, cfd]() {
            handle_connection(cfd);
            ::close(cfd);
            conns_--;
        }).detach();
    }
}

void WebServer::stop() {
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void WebServer::handle_connection(int fd) {
    std::string raw;
    if (!read_request_head(fd, raw)) {
        send_all(fd, make_response(400, "application/json",
                                   json_error("request invalida")));
        return;
    }
    const size_t he = raw.find("\r\n\r\n");
    HttpRequest req = parse_request_head(raw.substr(0, he + 4));
    if (!req.valid) {
        send_all(fd, make_response(req.status, "application/json",
                                   json_error(req.error)));
        return;
    }
    // Body: primero lo que ya llego con las cabeceras, luego el resto.
    if (req.content_length > 0) {
        std::string body = raw.substr(he + 4);
        while (body.size() < req.content_length) {
            char buf[4096];
            const ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            body.append(buf, (size_t)n);
        }
        if (body.size() < req.content_length) {
            send_all(fd, make_response(400, "application/json",
                                       json_error("body incompleto")));
            return;
        }
        req.body = body.substr(0, req.content_length);
    }

    // Seguridad local: token + Host loopback.
    const int sec = check_request_security(req, token_, port_);
    if (sec != 0) {
        const int status = (sec == 401) ? 401 : 400;
        const char* msg = (sec == 401) ? "token invalido" : "host invalido";
        send_all(fd, make_response(status, "application/json",
                                   json_error(msg)));
        return;
    }

    send_all(fd, route(req));
}

// --- JSON de un Job ----------------------------------------------------------

namespace {

json::JsonValue job_json(const Job& j) {
    return json::JsonValue::make_object({
        {"id", json::JsonValue::make_uint(j.id())},
        {"kind", json::JsonValue::make_string(j.kind())},
        {"state", json::JsonValue::make_string(job_state_name(j.state()))},
        {"cancel_requested", json::JsonValue::make_bool(j.cancel_requested())},
        {"bytes_scanned", json::JsonValue::make_uint(j.bytes_scanned())},
        {"total_bytes", json::JsonValue::make_uint(j.total_bytes())},
        {"count", json::JsonValue::make_uint(j.count())},
        {"progress", json::JsonValue::make_double(j.progress_percent())},
        {"elapsed_ms", json::JsonValue::make_int(j.elapsed_ms())},
        {"error", json::JsonValue::make_string(j.error())},
    });
}

} // namespace

std::string WebServer::route(const HttpRequest& req) {
    // GET /api/status
    if (req.path == "/api/status" && req.method == "GET") {
        json::JsonValue job = json::JsonValue::make_null();
        const uint64_t jid = jobs_.active_job_id();
        if (jid) {
            if (const auto j = jobs_.get(jid)) job = job_json(*j);
        }
        const json::JsonValue v = json::JsonValue::make_object({
            {"ok", json::JsonValue::make_bool(true)},
            {"attached", json::JsonValue::make_bool(app_.has_pid())},
            {"pid", json::JsonValue::make_int(app_.pid())},
            {"runner_busy", json::JsonValue::make_bool(runner_.busy())},
            {"job", std::move(job)},
        });
        return make_response(200, "application/json", json::write(v));
    }

    // GET /api/processes
    if (req.path == "/api/processes" && req.method == "GET") {
        json::JsonValue::Array items;
        for (const auto& p : list_processes()) {
            items.push_back(json::JsonValue::make_object({
                {"pid", json::JsonValue::make_int(p.pid)},
                {"name", json::JsonValue::make_string(p.name)},
                {"user", json::JsonValue::make_string(p.user)},
                {"state", json::JsonValue::make_string(std::string(1, p.state))},
                {"rss_kb", json::JsonValue::make_int(p.rss_kb)},
                {"accessible", json::JsonValue::make_bool(p.accessible)},
            }));
        }
        return make_response(200, "application/json",
                             json::write(json::JsonValue::make_array(std::move(items))));
    }

    // /api/jobs/<id>[/cancel]
    if (req.path.rfind("/api/jobs/", 0) == 0) {
        std::string rest = req.path.substr(10); // despues de "/api/jobs/"
        bool want_cancel = false;
        if (rest.size() > 7 && rest.compare(rest.size() - 7, 7, "/cancel") == 0) {
            want_cancel = true;
            rest = rest.substr(0, rest.size() - 7);
        }
        if (rest.empty()) {
            return make_response(404, "application/json", json_error("job no encontrado"));
        }
        for (char c : rest) {
            if (c < '0' || c > '9')
                return make_response(404, "application/json",
                                     json_error("job no encontrado"));
        }
        errno = 0;
        char* end = nullptr;
        const unsigned long long id = std::strtoull(rest.c_str(), &end, 10);
        if (errno == ERANGE || end == rest.c_str() || *end != '\0')
            return make_response(404, "application/json",
                                 json_error("job no encontrado"));
        const auto j = jobs_.get(id);
        if (!j)
            return make_response(404, "application/json",
                                 json_error("job no encontrado"));
        if (want_cancel) {
            if (req.method != "POST")
                return make_response(405, "application/json",
                                     json_error("metodo no soportado"));
            const bool ok = jobs_.request_cancel(id);
            const json::JsonValue v = json::JsonValue::make_object({
                {"ok", json::JsonValue::make_bool(ok)},
                {"job", job_json(*j)},
            });
            return make_response(200, "application/json", json::write(v));
        }
        if (req.method != "GET")
            return make_response(405, "application/json",
                                 json_error("metodo no soportado"));
        return make_response(200, "application/json",
                             json::write(job_json(*j)));
    }

    // Metodo no permitido en una ruta conocida de esta fase.
    if (req.path == "/api/status" || req.path == "/api/processes")
        return make_response(405, "application/json",
                             json_error("metodo no soportado"));

    return make_response(404, "application/json",
                         json_error("ruta no encontrada"));
}

} // namespace web
} // namespace mt
