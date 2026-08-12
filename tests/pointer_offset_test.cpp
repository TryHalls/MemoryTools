// pointer_offset_test.cpp - Objetivo de prueba del Pointer Scanner V2
// (cadenas con offsets y raiz modulo-relative, persistente ante ASLR).
//
// Genera deliberadamente:
//
//   global root (&g_a, en .data del ejecutable = raiz modulo-relative)
//      ↓ dereference
//   +0x20   (miembro 'next' de A)
//      ↓ dereference
//   +0x18   (miembro 'value' de B)
//      ↓
//   int final
//
// Cadena resoluble: root = (MODULE, offset de archivo de &g_a),
// offsets = [0x20, 0x18], value_type = i32.
//
// Imprime PID, MODULE (pathname tal como aparece en /proc/PID/maps),
// ROOT (&g_a), ROOT_OFFSET (offset de archivo calculado sobre sus propios
// maps), TARGET (&g_b->value) y VALUE. Concede ptrace explicitamente
// (PR_SET_PTRACER) igual que los otros objetivos de prueba.
//
// Compilar: g++ -O0 -g -std=c++17 pointer_offset_test.cpp -o pointer_offset_test
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

struct A {
    uint8_t pad[0x20];
    uint64_t* next;          // miembro en +0x20
};

struct B {
    uint8_t pad[0x18];
    volatile int value;      // miembro en +0x18
};

static A* g_a = nullptr;     // global en .data: raiz modulo-relative
static B* g_b = nullptr;

// Halla el pathname (tal cual aparece en /proc/self/maps) y el offset de
// archivo de la region que contiene 'addr'.
static bool self_mapping(uint64_t addr, std::string& path, uint64_t& file_off) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long s = 0, e = 0, off = 0, inode = 0;
        char perms[8] = {0};
        char dev[64] = {0};
        char p[1024] = {0};
        // start-end perms offset dev inode [pathname]
        if (std::sscanf(line.c_str(), "%llx-%llx %7s %llx %63s %llx %1023[^\n]",
                        &s, &e, perms, &off, dev, &inode, p) >= 6) {
            if (addr >= s && addr < e) {
                path = p;                                    // puede quedar vacio (anonimo)
                file_off = off + (addr - s);
                return true;
            }
        }
    }
    return false;
}

int main() {
    g_a = new A;
    g_b = new B;
    g_a->next = (uint64_t*)g_b;
    g_b->value = 4242;

    // Concesion explicita de ptrace (misma filosofia que objetivo/pointer_test).
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::string mod;
    uint64_t root_off = 0;
    if (!self_mapping((uint64_t)&g_a, mod, root_off))
        mod = "(no mapeada)";

    printf("== pointer_offset_test (Pointer Scanner V2) ==\n");
    printf("PID: %d\n", (int)getpid());
    printf("MODULE: %s\n", mod.c_str());
    printf("ROOT: 0x%016llx\n", (unsigned long long)&g_a);
    printf("ROOT_OFFSET: 0x%llx\n", (unsigned long long)root_off);
    printf("TARGET: 0x%016llx\n", (unsigned long long)&g_b->value);
    printf("VALUE: %d\n", g_b->value);
    printf("Comandos: q salir\n");

    while (true) {
        printf("vivo | value=%d | root=%llx\n", (int)g_b->value,
               (unsigned long long)g_a);
        fflush(stdout);
        sleep(1);
    }
    return 0;
}
