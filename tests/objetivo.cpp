// objetivo.cpp - Programa de prueba controlado para MemoryTool.
//
// Declara variables conocidas (volatile para que el compilador no las
// optimice) y se mantiene en ejecucion mostrando su valor periodico.
// Es el "objetivo" de las primeras pruebas del escaner.
//
// Compilar: g++ -O0 -g objetivo.cpp -o objetivo
//   (-O0 es importante: sin optimizacion las variables se quedan en memoria)
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <unistd.h>

// --- Variables objetivo -----------------------------------------------------
volatile int32_t  dinero      = 12345;  // objetivo principal (int32)
volatile uint16_t nivel       = 7;      // para probar int16/uint16
volatile float    velocidad   = 1.5f;   // para probar float
volatile int64_t  puntuacion  = 100000; // para probar int64
volatile char     mensaje[64] = "hola memorytool"; // para probar strings
volatile uint8_t  datos[16] = {0x48, 0x8B, 0x05, 0x90, 0x90, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0}; // para probar bytes

// Memoria anonima extra (opcional): se mapea si se pasa un tamanio en MB
// como argumento. Sirve para probar escaneos con millones de candidatos
// (first unknown) y los limites de candidatos del escaner.
static volatile char* g_extra_mem = nullptr;

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
    if (line == "b" || line == "byte") {
        datos[2] ^= 0x01;
        printf("  datos[2] -> 0x%02x\n", (unsigned)datos[2]);
        return;
    }
    if (!line.empty() && (line[0] == 'm' || line[0] == 'M')) {
        const std::string t = line.substr(1);
        size_t sp = t.find_first_not_of(' ');
        if (sp == std::string::npos) {
            printf("  uso: m <texto>\n");
        } else {
            std::strncpy((char*)mensaje, t.c_str() + sp, sizeof(mensaje) - 1);
            mensaje[sizeof(mensaje) - 1] = '\0';
            printf("  mensaje -> %s\n", (const char*)mensaje);
        }
        return;
    }
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

int main(int argc, char** argv) {
    // Memoria extra opcional: objetivo [MB]  ->  mmap N MiB legibles.
    if (argc > 1) {
        long mb = atol(argv[1]);
        if (mb > 0) {
            void* p = mmap(nullptr, (size_t)mb * 1024 * 1024,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p != MAP_FAILED) {
                g_extra_mem = (volatile char*)p;
                printf("Extra: %ld MiB de memoria anonima legible mapeados (%p)\n",
                       mb, (void*)g_extra_mem);
            } else {
                printf("Extra: fallo de mmap (%s)\n", strerror(errno));
            }
        }
    }

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
    printf("  mensaje    (char[64]): %p\n", (void*)&mensaje);
    printf("  datos      (u8[16]) : %p\n", (void*)&datos);
    printf("Comandos: <numero> dinero | + o - incrementar/decrementar | r reset | n nivel++ | v <float> velocidad | m <texto> mensaje | b toggle byte datos[2] | q salir\n");

    bool stdin_open = true;
    while (true) {
        printf("PID: %d | Dinero: %d | Nivel: %u | Velocidad: %.2f | Puntuacion: %lld | Msg: %s | Datos2: %02x\n",
               (int)getpid(), (int)dinero, (unsigned)nivel, (double)velocidad,
               (long long)puntuacion, (const char*)mensaje, (unsigned)datos[2]);
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
