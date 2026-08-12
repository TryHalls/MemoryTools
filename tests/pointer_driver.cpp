// pointer_driver.cpp - Conductor de prueba del Pointer Scanner.
//
// No es la CLI: es infraestructura de tests para ejercitar pointer_scan
// contra un proceso real desde el shell (tests/e2e.sh). Adjunta el proceso,
// ejecuta el escaneo con las opciones dadas y vuelca el resultado en texto
// plano (resumen + una linea por cadena), con direcciones en 0x%016llx.
//
// Uso: pointer_driver <pid> <target_hex> [depth] [max_edges] [max_chains] [max_offset]
//
// max_edges/max_chains = 0 significa "por defecto" (500000/100000);
// max_offset = 0 fuerza el caso V1 (ventana de offsets {0}) y es el valor
// que usa el bloque determinista de la E2E. Sin el argumento, se usa el
// max_offset por defecto del motor (0x100).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "memory.h"
#include "pointer.h"

using namespace mt;

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
                "uso: pointer_driver <pid> <target_hex> [depth] [max_edges] [max_chains] [max_offset]\n");
        return 2;
    }
    const int pid = atoi(argv[1]);
    const uint64_t target = strtoull(argv[2], nullptr, 0);

    PointerScanOptions opts;
    opts.target = target;
    if (argc > 3) opts.max_depth = atoi(argv[3]);
    if (argc > 4) { long v = atoll(argv[4]); if (v > 0) opts.max_edges_per_level = (size_t)v; }
    if (argc > 5) { long v = atoll(argv[5]); if (v > 0) opts.max_chains = (size_t)v; }
    if (argc > 6) opts.max_offset = strtoull(argv[6], nullptr, 0);

    Memory mem;
    std::string err;
    if (!mem.open(pid, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::vector<Region> regions = parse_maps(pid);
    const PointerScanResult res = pointer_scan(mem, regions, opts);
    mem.close();

    printf("target=0x%016llx depth=%d levels=%d edges=%llu chains=%llu "
           "edges_truncated=%d chains_truncated=%d\n",
           (unsigned long long)opts.target, opts.max_depth, res.levels,
           (unsigned long long)res.total_edges,
           (unsigned long long)res.chains.size(),
           res.edges_truncated ? 1 : 0, res.chains_truncated ? 1 : 0);
    for (const PointerChain& c : res.chains) {
        printf("chain d=%d:", c.depth);
        for (uint64_t n : c.nodes)
            printf(" 0x%016llx", (unsigned long long)n);
        printf("\n");
    }
    return 0;
}
