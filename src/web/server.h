// server.h - Servidor HTTP local minimo (FASE W-3).
//
// Escucha SOLO en 127.0.0.1 (nunca 0.0.0.0/::). Cada request debe traer
// X-MemoryTool-Token (token aleatorio de 16 bytes por ejecucion, expuesto
// solo por consola) y Host 127.0.0.1:<puerto> o localhost:<puerto>.
//
// Rutas implementadas en esta fase:
//   GET  /api/status
//   GET  /api/processes
//   GET  /api/jobs/<id>
//   POST /api/jobs/<id>/cancel
//
// Threading: un accept loop + un hilo por conexion (con limite de
// concurrencia); los endpoints de jobs/status NO esperan a session_mtx (el
// worker del JobRunner puede estar ejecutando un escaneo). Sin WebSocket,
// sin keep-alive, sin TLS: se cierra la conexion tras cada respuesta.
//
// Lifecycle: start(port) -> run() (bloquea aceptando) -> stop() (cierra el
// socket de escucha; run() retorna). No se instalan senales POSIX todavia.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "../application.h"
#include "api.h"
#include "http.h"
#include "job_runner.h"
#include "jobs.h"

namespace mt {
namespace web {

class WebServer {
public:
    WebServer(Application& app, JobRegistry& jobs, JobRunner& runner)
        : app_(app), jobs_(jobs), runner_(runner), api_(app, jobs, runner) {}

    // Crea el socket, hace bind en 127.0.0.1:<port> (0 = puerto libre del
    // kernel) y escucha. Genera el token de la ejecucion. Devuelve false y
    // rellena err si falla.
    bool start(uint16_t port, std::string& err);

    // Bucle de aceptacion: bloquea hasta stop() (o error fatal). No crea
    // hilos de escucha; un hilo por conexion con limite de concurrencia.
    void run();

    // Cierra el socket de escucha; run() retorna. Seguro llamarlo desde otro
    // hilo. Las conexiones activas terminan su request y cierran su fd.
    void stop();

    // Puerto real en el que escucha (util si se pidio puerto 0).
    uint16_t port() const { return port_; }
    // Token de la ejecucion (solo se imprime por consola).
    const std::string& token() const { return token_; }

private:
    // Atiende una conexion: lee la request, valida token/Host, enruta,
    // escribe la respuesta y cierra el fd (a cargo del hilo de conexion).
    void handle_connection(int fd);

    // Enrutado de una request ya validada -> respuesta HTTP completa.
    std::string route(const HttpRequest& req);

    Application& app_;
    JobRegistry& jobs_;
    JobRunner& runner_;
    Api api_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::string token_;
    std::atomic<int> conns_{0};
};

} // namespace web
} // namespace mt
