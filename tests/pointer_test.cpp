// pointer_test.cpp - Programa de prueba para el Pointer Scanner.
//
// Crea deliberadamente una cadena de punteros hacia una variable objetivo:
//
//   Node3 -> Node2 -> Node1 -> TARGET
//
// (Node1 almacena la direccion de TARGET, Node2 la de Node1, Node3 la de
// Node2) y un ciclo controlado:
//
//   CycleA <-> CycleB
//
// Se mantiene en ejecucion y concede ptrace explicitamente (PR_SET_PTRACER),
// igual que objetivo.cpp, para que el scanner pueda acceder en Crostini.
//
// Compilar: g++ -O0 -g pointer_test.cpp -o pointer_test
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/prctl.h>
#include <sys/select.h>
#include <unistd.h>

volatile uint64_t g_target = 4242; // TARGET: la variable a la que se llega
volatile uint64_t* g_node1 = nullptr; // almacena &g_target
volatile uint64_t* g_node2 = nullptr; // almacena la direccion del almacen de node1
volatile uint64_t* g_node3 = nullptr; // almacena la direccion del almacen de node2
volatile uint64_t* g_cycle_a = nullptr;
volatile uint64_t* g_cycle_b = nullptr;

static volatile uint64_t* make_node(uint64_t v) {
    volatile uint64_t* p = new volatile uint64_t;
    *p = v;
    return p;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Cadena deliberada: Node3 -> Node2 -> Node1 -> TARGET.
    g_node1 = make_node((uint64_t)&g_target);
    g_node2 = make_node((uint64_t)g_node1);
    g_node3 = make_node((uint64_t)g_node2);

    // Ciclo controlado: CycleA <-> CycleB (A almacena la direccion de B y
    // viceversa), para probar la deteccion de ciclos por cadena.
    g_cycle_a = new volatile uint64_t;
    g_cycle_b = new volatile uint64_t;
    *g_cycle_a = (uint64_t)g_cycle_b;
    *g_cycle_b = (uint64_t)g_cycle_a;

    // Conceder explicitamente permiso de ptrace (misma filosofia que
    // objetivo.cpp: concesion explicita y documentada, no evasion).
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

    setvbuf(stdout, nullptr, _IONBF, 0);

    printf("== pointer_test (programa de prueba del Pointer Scanner) ==\n");
    printf("PID: %d\n", (int)getpid());
    printf("TARGET: 0x%016llx\n", (unsigned long long)&g_target);
    printf("NODE1:  0x%016llx\n", (unsigned long long)g_node1);
    printf("NODE2:  0x%016llx\n", (unsigned long long)g_node2);
    printf("NODE3:  0x%016llx\n", (unsigned long long)g_node3);
    printf("CYCLEA: 0x%016llx\n", (unsigned long long)g_cycle_a);
    printf("CYCLEB: 0x%016llx\n", (unsigned long long)g_cycle_b);
    printf("Comandos: q salir\n");

    bool stdin_open = true;
    while (true) {
        printf("vivo | target=%llu | node1->%llx node2->%llx node3->%llx\n",
               (unsigned long long)g_target,
               (unsigned long long)*g_node1,
               (unsigned long long)*g_node2,
               (unsigned long long)*g_node3);
        fflush(stdout);

        if (!stdin_open) {
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
                continue;
            }
            if (line == "q" || line == "quit" || line == "exit") {
                printf("Saliendo...\n");
                fflush(stdout);
                _exit(0);
            }
        } else if (r < 0) {
            break;
        }
    }
    return 0;
}
