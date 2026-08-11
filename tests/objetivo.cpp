// objetivo.cpp - Programa de prueba controlado para MemoryTool.
//
// Declara variables conocidas (volatile para que el compilador no las
// optimice) y se mantiene en ejecucion mostrando su valor periodico.
// Es el "objetivo" de las primeras pruebas del escaner.
//
// Compilar: g++ -O0 -g objetivo.cpp -o objetivo
//   (-O0 es importante: sin optimizacion las variables se quedan en memoria)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/prctl.h>
#include <sys/select.h>
#include <unistd.h>

// --- Variables objetivo -----------------------------------------------------
volatile int32_t  dinero      = 12345;  // objetivo principal (int32)
volatile uint16_t nivel       = 7;      // para probar int16/uint16
volatile float    velocidad   = 1.5f;   // para probar float
volatile int64_t  puntuacion  = 100000; // para probar int64
volatile char     mensaje[64] = "hola memorytool"; // para probar strings

static void handle_command(const std::string& line) {
    if (line == "q" || line == "quit" || line == "exit") {
        printf("Saliendo...\n");
        fflush(stdout);
        _exit(0);
    }
    if (line == "+" || line == "i" || line == "inc") { ++dinero; printf("  dinero -> %d\n", (int)dinero); return; }
    if (line == "-" || line == "d" || line == "dec") { --dinero; printf("  dinero -> %d\n", (int)dinero); return; }
    if (line == "r" || line == "reset") { dinero = 12345; printf("  dinero -> 12345 (reset)\n"); return; }
    if (line == "n" || line == "nivel") { ++nivel; printf("  nivel -> %u\n", (unsigned)nivel); return; }
    if (!line.empty() && (line[0] == 'v' || line[0] == 'V')) {
        char* end = nullptr;
        float f = strtof(line.c_str() + 1, &end);
        if (end != line.c_str() + 1) {
            velocidad = f;
            printf("  velocidad -> %.2f\n", (double)velocidad);
        } else {
            printf("  uso: v <float>\n");
        }
        return;
    }
    char* end = nullptr;
    long long v = strtoll(line.c_str(), &end, 0);
    if (end != line.c_str() && *end == '\0') {
        dinero = (int32_t)v;
        printf("  dinero -> %d\n", (int)dinero);
        return;
    }
    printf("  comando desconocido: %s\n", line.c_str());
}

int main() {
    // Conceder explicitamente permiso de ptrace.
    // Con ptrace_scope=1 (Yama) un proceso solo puede trazar a sus hijos o a
    // quien le haya dado permiso con PR_SET_PTRACER. Este programa de prueba
    // es nuestro: concedemos el permiso de forma explicita y documentada,
    // respetando el mecanismo del kernel (no es un intento de evadirlo).
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

    setvbuf(stdout, nullptr, _IONBF, 0);

    printf("== objetivo (programa de prueba de MemoryTool) ==\n");
    printf("PID: %d\n", (int)getpid());
    printf("Direcciones (para verificar contra el escaner):\n");
    printf("  dinero     (int32) : %p\n", (void*)&dinero);
    printf("  nivel      (u16)   : %p\n", (void*)&nivel);
    printf("  velocidad  (float) : %p\n", (void*)&velocidad);
    printf("  puntuacion (int64) : %p\n", (void*)&puntuacion);
    printf("Comandos: <numero> dinero | + o - incrementar/decrementar | r reset | n nivel++ | v <float> velocidad | q salir\n");

    bool stdin_open = true;
    while (true) {
        printf("PID: %d | Dinero: %d | Nivel: %u | Velocidad: %.2f | Puntuacion: %lld\n",
               (int)getpid(), (int)dinero, (unsigned)nivel, (double)velocidad, (long long)puntuacion);
        fflush(stdout);

        if (!stdin_open) { // stdin cerrado: seguimos vivos sin leer comandos
            sleep(1);
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = {1, 0};
        int r = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
        if (r > 0) {
            std::string line;
            int c;
            while ((c = fgetc(stdin)) != EOF && c != '\n') line += (char)c;
            if (c == EOF) {
                stdin_open = false;
                if (!line.empty()) handle_command(line);
                continue;
            }
            if (!line.empty()) handle_command(line);
        } else if (r < 0) {
            break;
        }
    }
    return 0;
}
