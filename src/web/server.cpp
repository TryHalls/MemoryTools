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

#include "http.h"

namespace mt {
namespace web {

namespace {

// Paths de la Web UI estatica (GET /, /index.html, /app.js, /styles.css).
// Estos assets se sirven SIN validacion de token: el navegador no puede
// anadir la cabecera X-MemoryTool-Token en <script src> / <link href>. El
// token se inyecta SOLO en index.html (bootstrap) y los /api/* siguen
// exigiendolo en cada request.
bool is_static_path(const std::string& path) {
    return path == "/" || path == "/index.html" || path == "/app.js" ||
           path == "/styles.css";
}

// Directorio del ejecutable (para localizar los assets junto al binario).
std::string exe_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    const std::string p(buf);
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? "" : p.substr(0, slash);
}

// Localiza la carpeta de assets estaticos probando candidatos:
//   1) <directorio del ejecutable>/../src/web/static  (binario en build/)
//   2) src/web/static                                  (cwd = raiz del repo)
// Devuelve "" si no encuentra index.html.
std::string static_root() {
    const std::string exe = exe_dir();
    std::vector<std::string> cands;
    if (!exe.empty()) cands.push_back(exe + "/../src/web/static");
    cands.push_back("src/web/static");
    for (const auto& c : cands) {
        if (access((c + "/index.html").c_str(), R_OK) == 0) return c;
    }
    return "";
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)sz);
    const size_t n = std::fread(&out[0], 1, (size_t)sz, f);
    std::fclose(f);
    return n == (size_t)sz;
}

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

    // Seguridad local: token + Host loopback. Los assets estaticos de la Web
    // UI se sirven SIN token (el navegador no puede enviar la cabecera en
    // <script src>/<link href>); el token se inyecta en index.html al servirlo
    // y los /api/* siguen exigiendolo.
    if (!is_static_path(req.path)) {
        const int sec = check_request_security(req, token_, port_);
        if (sec != 0) {
            const int status = (sec == 401) ? 401 : 400;
            const char* msg =
                (sec == 401) ? "token invalido" : "host invalido";
            send_all(fd, make_response(status, "application/json",
                                       json_error(msg)));
            return;
        }
    }

    send_all(fd, route(req));
}

std::string WebServer::route(const HttpRequest& req) {
    // Assets estaticos de la Web UI (FASE W-5A). Se sirven desde disco
    // (src/web/static/ junto al binario o en el cwd). index.html recibe el
    // token de la ejecucion sustituyendo el placeholder __MEMORYTOOL_TOKEN__
    // (bootstrap: la pagina lo expone a app.js como window.MEMORYTOOL_TOKEN;
    // nunca aparece en URLs ni por query string).
    if (req.method == "GET") {
        std::string name;
        if (req.path == "/" || req.path == "/index.html")
            name = "index.html";
        else if (req.path == "/app.js")
            name = "app.js";
        else if (req.path == "/styles.css")
            name = "styles.css";
        if (!name.empty()) {
            std::string content;
            const std::string root = static_root();
            if (root.empty() || !read_file(root + "/" + name, content))
                return make_response(404, "text/plain",
                                     "asset no encontrado");
            if (name == "index.html") {
                const std::string ph = "__MEMORYTOOL_TOKEN__";
                size_t p = 0;
                while ((p = content.find(ph, p)) != std::string::npos) {
                    content.replace(p, ph.size(), token_);
                    p += token_.size();
                }
            }
            const std::string ct =
                name == "index.html"
                    ? "text/html; charset=utf-8"
                    : name == "app.js" ? "application/javascript; charset=utf-8"
                                        : "text/css; charset=utf-8";
            return make_response(200, ct, content);
        }
    }

    // Todo el enrutado y la construccion de JSON viven en la capa Api
    // (src/web/api.cpp). El servidor solo valida token/Host (handle_connection)
    // y serializa la respuesta.
    const ApiResponse r = api_.handle(req);
    return make_response(r.status, "application/json", r.body);
}

} // namespace web
} // namespace mt
