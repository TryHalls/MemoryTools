// main.cpp - Interfaz de terminal (frontend CLI) de MemoryTool.
//
// Responsabilidades: banner/uso, interpretacion de argumentos y el bucle de
// lectura de la sesion. Cada comando se delega en la capa de comandos
// (command.h) y el estado de la sesion vive en Session (session.h).
//
// Uso:
//   memorytool            sesion interactiva
//   memorytool <pid>      sesion interactiva con proceso objetivo
//   memorytool list       listar procesos
//   memorytool maps <pid> mostrar regiones de memoria
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "application.h"
#include "command.h"
#include "session.h"
#include "web/job_runner.h"
#include "web/jobs.h"
#include "web/server.h"

using namespace mt;

static void print_banner() {
    std::cout
        << "MemoryTool 0.1 - Analizador de memoria para Linux/ChromeOS\n"
        << "Motor propio, sin dependencias externas. Uso educativo y de depuracion.\n\n";
}

static void print_usage() {
    std::cout
        << "Uso:\n"
        << "  memorytool                     Sesion interactiva\n"
        << "  memorytool <pid>               Sesion interactiva con proceso objetivo\n"
        << "  memorytool list                Listar procesos\n"
        << "  memorytool maps <pid>          Mostrar regiones de memoria\n"
        << "  memorytool --web [--port N]    Web UI local (N=0: puerto libre)\n"
        << "  memorytool help                Esta ayuda\n";
}

// Modo servidor HTTP local (FASE W-3): arranca el servidor en 127.0.0.1,
// imprime URL y token por consola y bloquea atendiendo requests. El token
// solo se muestra aqui; toda request debe incluirlo en X-MemoryTool-Token.
static int run_web(int argc, char** argv) {
    uint16_t port = 0; // por defecto: puerto libre asignado por el kernel
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            errno = 0;
            char* end = nullptr;
            const long v = std::strtol(argv[++i], &end, 10);
            if (errno == ERANGE || end == argv[i] || *end != '\0' ||
                v < 0 || v > 65535) {
                printf("Puerto invalido: %s\n", argv[i]);
                return 1;
            }
            port = (uint16_t)v;
        } else {
            printf("Opcion desconocida: %s\n", a.c_str());
            print_usage();
            return 1;
        }
    }

    Session session;
    Application app(session);
    web::JobRegistry jobs;
    web::JobRunner runner(app, jobs);
    web::WebServer server(app, jobs, runner);

    std::string err;
    if (!server.start(port, err)) {
        fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    printf("MemoryTool Web UI\n");
    printf("URL: http://127.0.0.1:%u\n", server.port());
    printf("Token: %s\n", server.token().c_str());
    fflush(stdout);
    server.run(); // bloquea hasta stop()/proceso terminado
    return 0;
}

// Bucle principal de la sesion interactiva.
//  - initial_pid: proceso objetivo opcional (por argumento o 'attach').
static int run_repl(std::optional<int> initial_pid) {
    Session session;
    if (initial_pid) {
        std::string err;
        if (!session.attach(*initial_pid, err)) {
            printf("Aviso: no se pudo acceder al proceso %d: %s\n",
                   *initial_pid, err.c_str());
        } else {
            printf("Proceso objetivo: %d\n", *initial_pid);
        }
    }

    while (true) {
        if (session.has_pid())
            printf("mt(%d)> ", session.pid());
        else
            printf("mt> ");
        fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (execute(line, session).quit) break;
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // salida inmediata (prompts/scripts)
    const char* arg1 = argc > 1 ? argv[1] : nullptr;

    if (argc >= 2 && (std::string(arg1) == "help" || std::string(arg1) == "--help" || std::string(arg1) == "-h")) {
        print_banner();
        print_usage();
        Session s;
        execute("help", s);
        return 0;
    }
    if (argc >= 2 && std::string(arg1) == "list") {
        Session s;
        execute("list", s);
        return 0;
    }
    if (argc >= 3 && std::string(arg1) == "maps") {
        auto r = resolve_target(argv[2]);
        if (!r) return 1;
        print_maps(*r);
        return 0;
    }

    if (argc >= 2 && std::string(arg1) == "--web") {
        return run_web(argc, argv);
    }

    if (argc >= 2 && std::string(arg1) == "attach") {
        if (argc < 3) {
            printf("Uso: memorytool attach <pid|nombre>\n");
            return 1;
        }
        auto r = resolve_target(argv[2]);
        if (!r) return 1;
        print_banner();
        run_repl(r);
        return 0;
    }

    if (argc >= 2) {
        auto r = resolve_target(arg1);
        if (!r) return 1;
        print_banner();
        run_repl(r);
        return 0;
    }

    print_banner();
    run_repl(std::nullopt);
    return 0;
}
