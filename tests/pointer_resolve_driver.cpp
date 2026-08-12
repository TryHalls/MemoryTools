// pointer_resolve_driver.cpp - Conductor de prueba del PointerResolver (V2).
//
// No es la CLI: es infraestructura de tests para ejercitar la resolucion de
// PointerChainRef contra un proceso real desde el shell (tests/e2e.sh), y
// para demostrar la persistencia frente a ASLR (misma cadena, proceso nuevo).
//
// Uso:
//   pointer_resolve_driver save <pid> <outfile> <module> <root_hex>
//                            <steps_csv|-> <type> [desc]
//       Construye la PointerChainRef, la anade a una AddressTable, la guarda
//       en <outfile> y la resuelve contra <pid>. Imprime:
//         RESOLVED=0x... VALUE=<valor>
//       (o ERROR=<motivo>).
//   pointer_resolve_driver resolve <pid> <infile>
//       Carga la AddressTable de <infile> y resuelve la entrada 0 contra
//       <pid>.
//
// steps_csv: "0x20,0x18" (offsets post-deref) o "-" para ninguno.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "address_table.h"
#include "memory.h"
#include "pointer.h"
#include "pointer_resolver.h"

using namespace mt;

static void print_result(const ResolveResult& r, DataType t) {
    if (r.ok) {
        printf("RESOLVED=0x%016llx\n", (unsigned long long)r.address);
        printf("VALUE=%s\n", value_to_string(r.value, t).c_str());
    } else {
        printf("ERROR=%s\n", r.error.c_str());
    }
}

static bool parse_steps(const std::string& csv, std::vector<uint64_t>& out) {
    if (csv == "-") return true; // sin steps
    size_t pos = 0;
    while (pos < csv.size()) {
        size_t comma = csv.find(',', pos);
        std::string part = (comma == std::string::npos)
                               ? csv.substr(pos)
                               : csv.substr(pos, comma - pos);
        if (part.empty()) return false;
        char* end = nullptr;
        unsigned long long v = std::strtoull(part.c_str(), &end, 0);
        if (end == part.c_str() || *end != '\0') return false;
        out.push_back((uint64_t)v);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
                "uso: pointer_resolve_driver save <pid> <out> <module> <root> <steps> <type> [desc]\n"
                "     pointer_resolve_driver resolve <pid> <infile>\n");
        return 2;
    }
    const int pid = atoi(argv[2]);

    Memory mem;
    std::string err;
    if (!mem.open(pid, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::vector<Region> regions = parse_maps(pid);

    if (std::string(argv[1]) == "save") {
        if (argc < 8) {
            fprintf(stderr, "faltan argumentos para 'save'\n");
            return 2;
        }
        PointerChainRef ref;
        ref.root.kind = PointerBaseKind::MODULE;
        ref.root.module = argv[4];
        ref.root.offset = strtoull(argv[5], nullptr, 0);
        if (!parse_steps(argv[6], ref.offsets)) {
            fprintf(stderr, "steps invalidos: %s\n", argv[6]);
            return 2;
        }
        if (!parse_type(argv[7], ref.value_type)) {
            fprintf(stderr, "tipo invalido: %s\n", argv[7]);
            return 2;
        }
        const std::string desc = argc >= 9 ? argv[8] : "cadena de prueba";

        AddressTable t;
        t.add(ref, desc);
        if (!t.save(argv[3], err)) {
            fprintf(stderr, "error al guardar: %s\n", err.c_str());
            return 1;
        }
        print_result(resolve_chain(ref, mem, regions), ref.value_type);
        return 0;
    }

    if (std::string(argv[1]) == "resolve") {
        if (argc < 4) {
            fprintf(stderr, "faltan argumentos para 'resolve'\n");
            return 2;
        }
        AddressTable t;
        if (!t.load(argv[3], err)) {
            fprintf(stderr, "error al cargar: %s\n", err.c_str());
            return 1;
        }
        const AddressEntry* e = t.get(0);
        if (!e || !e->ptr) {
            fprintf(stderr, "la entrada 0 no es una cadena dinamica\n");
            return 1;
        }
        print_result(resolve_chain(*e->ptr, mem, regions), e->type);
        return 0;
    }

    fprintf(stderr, "modo desconocido: %s\n", argv[1]);
    return 2;
}
