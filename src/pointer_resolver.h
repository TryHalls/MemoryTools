// pointer_resolver.h - Resolucion de PointerChainRef (V2) contra un proceso.
//
// Convierte una cadena persistente en una direccion absoluta actual y lee el
// valor final, siguiendo:
//
//   addr = root (module + file offset, o direccion absoluta)
//   por cada offset o en offsets:
//       p    = read_u64(addr)
//       addr = p + o
//   valor final = read(addr, value_type)
//
// El modulo NO hace attach/detach: recibe un Memory ya abierto y las
// regiones (parse_maps) del proceso en el momento de la resolucion. No
// cachea direcciones entre procesos: cada llamada recalcula desde la base.
//
// Las funciones puras (make_base_from_address, resolve_root, follow_chain)
// solo dependen de Region/read_fn y son testables con datos sinteticos.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "memory.h"
#include "pointer.h"

namespace mt {

struct ResolveResult {
    bool ok = false;
    uint64_t address = 0;   // direccion del valor final (si ok)
    Value value;            // valor final leido (si ok)
    std::string error;      // motivo del fallo (si !ok)
};

// Convierte una direccion absoluta en una base persistente:
//  - MODULE si la region que la contiene tiene pathname de archivo (module =
//    pathname exacto; offset = region.offset + (addr - region.start)).
//  - ABSOLUTE en caso contrario (heap/stack/anonima): NO se finge
//    persistencia de regiones dinamicas.
PointerBase make_base_from_address(const std::vector<Region>& regions,
                                   uint64_t address);

// Localiza la direccion absoluta del primer puntero de la cadena. Pura:
// solo usa regiones. Errores: "modulo no encontrado", "offset fuera del
// modulo". ABSOLUTE devuelve directamente la direccion guardada.
bool resolve_root(const PointerBase& root, const std::vector<Region>& regions,
                  uint64_t& out, std::string& err);

// Sigue la cadena desde root_addr leyendo con 'read_fn' (debe leer 'len'
// bytes en 'addr' y devolver los bytes leidos; negativo o corto = error).
// Pura respecto a la memoria: usable con buffers sinteticos en unit tests.
// Errores: "cadena rota (direccion no legible)", "puntero no legible",
// "no se pudo leer el valor final".
ResolveResult follow_chain(const PointerChainRef& chain, uint64_t root_addr,
                           const std::vector<Region>& regions,
                           const std::function<ssize_t(uint64_t, void*, size_t)>& read_fn);

// Comodidad: resuelve la cadena contra una Memory ya abierta (no hace
// attach/detach; no cachea direcciones entre procesos).
ResolveResult resolve_chain(const PointerChainRef& chain, Memory& mem,
                            const std::vector<Region>& regions);

// Construye la referencia persistente de una cadena V1 (nodes son
// direcciones absolutas; nodes.back() es el target). La raiz (nodes[0]) se
// convierte en base persistente (MODULE si es posible) y los offsets entre
// derefs son 0 (V1 no tiene offsets).
PointerChainRef make_chain_ref(const std::vector<Region>& regions,
                               const std::vector<uint64_t>& nodes,
                               DataType value_type);

// Igual que la anterior pero con los offsets de la cadena V2 (un offset por
// deref, en el mismo orden que los nodos). Si la cantidad no cuadra con
// nodes (cadenas V1 sin offsets), se usan offsets a 0.
PointerChainRef make_chain_ref(const std::vector<Region>& regions,
                               const std::vector<uint64_t>& nodes,
                               const std::vector<uint64_t>& offsets,
                               DataType value_type);

} // namespace mt
