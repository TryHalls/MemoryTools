// pointer.h - Pointer scanner (level-scan inverso).
//
// Dado un TARGET (direccion), busca cadenas de punteros que conducen hasta
// el: direcciones cuyo valor de 8 bytes es el TARGET, luego direcciones cuyo
// valor es una de esas direcciones, etc., hasta max_depth:
//
//   Node3 -> Node2 -> Node1 -> TARGET
//
// Cada nivel conserva la relacion source -> target (PointerEdge), y las
// cadenas se construyen incrementalmente (frontera), sin volver a leer
// memoria para reconstruirlas.
//
// Control de ciclos POR CADENA: al extender una cadena se descarta un source
// que ya este dentro de esa misma cadena. No hay visited global, para no
// perder cadenas validas que comparten nodos (X->Y->T y Z->Y->T conviven).
//
// El modulo NO hace attach/detach: recibe un Memory ya abierto (la
// Session/CLI se encarga del ciclo ptrace).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory.h"
#include "types.h"

namespace mt {

// Clasificacion de una region para el pointer scanner.
enum class RegionKind {
    HEAP,    // [heap]
    STACK,   // [stack] / [stack:NNN]
    ANON_RW, // anonima legible+escribible (sin path)
    DATA,    // data/bss con respaldo de archivo (escribible)
    CODE,    // ejecutable (r-x)
    OTHER,   // el resto: archivos de solo lectura, [vdso], no legibles...
};

RegionKind classify_region(const Region& r);

// Selecciona las regiones fuente por defecto: heap, stack, anon rw y data
// (data/BSS respaldada por archivo, incluida la del ejecutable principal).
// Con include_code = true se anaden tambien las regiones ejecutables.
// Excluye code, [vdso]/[vsyscall], archivos de solo lectura y no legibles.
std::vector<Region> select_pointer_regions(const std::vector<Region>& all,
                                           bool include_code);

// --- Resultados -----------------------------------------------------------

struct PointerScanOptions {
    uint64_t target = 0;
    int max_depth = 3;
    size_t max_edges_per_level = 500000; // limite de aristas por nivel
    size_t max_chains = 100000;          // limite de cadenas totales
    bool include_code = false;
    uint64_t min_addr = 0;               // 0 = sin limite
    uint64_t max_addr = 0;               // 0 = sin limite
};

// Una arista: la direccion source contiene un puntero (valor de 8 bytes) a
// target. Es la relacion que permite reconstruir las cadenas sin rescanear.
struct PointerEdge {
    uint64_t source = 0;
    uint64_t target = 0;
};

// Una cadena completa [root ... TARGET]; nodes.back() == target del scan.
// depth = numero de derefs = nodes.size() - 1.
struct PointerChain {
    std::vector<uint64_t> nodes;
    int depth = 0;
};

struct PointerScanResult {
    uint64_t target = 0;
    std::vector<PointerChain> chains;
    bool edges_truncated = false;  // se alcanzo max_edges_per_level en un nivel
    bool chains_truncated = false; // se alcanzo max_chains
    int levels = 0;                // niveles completados con resultados
    size_t total_edges = 0;        // aristas encontradas en total
};

// ---------------------------------------------------------------------------
// V2: representacion persistente de una cadena (Pointer Chain Ref).
//
// Diferencia con PointerChain (V1, instantanea): V1 guarda direcciones
// absolutas del momento del escaneo; V2 guarda RELACIONES que permiten
// volver a resolver la cadena cuando el proceso cambia (ASLR):
//
//   root  -> localiza el PRIMER puntero (modulo + offset de archivo, o una
//            direccion absoluta como fallback no persistente)
//   offsets -> desplazamientos APLICADOS DESPUES de cada dereference.
//              offsets.size() = numero de derefs; el ultimo offset localiza
//              el valor final (no se dereferencia).
//   value_type -> tipo del VALOR FINAL (independiente del kind 'pointer':
//              'pointer' describe el mecanismo, no el tipo).
//
// Resolution: addr = root; para cada offset o en offsets: addr = read(addr)
// + o; valor final = read(addr, value_type).

// Tipo de base de una cadena persistente.
enum class PointerBaseKind { ABSOLUTE, MODULE };

struct PointerBase {
    PointerBaseKind kind = PointerBaseKind::ABSOLUTE;
    uint64_t address = 0;   // ABSOLUTE: direccion de la raiz
    std::string module;     // MODULE: pathname exacto de /proc/PID/maps
    uint64_t offset = 0;    // MODULE: offset de archivo de la raiz
};

struct PointerChainRef {
    PointerBase root;               // localiza el primer puntero
    std::vector<uint64_t> offsets;  // steps post-deref (size = derefs)
    DataType value_type = DataType::I32;
};

// Busca cadenas de punteros hacia opts.target en las regiones dadas.
// mem debe estar ya abierta. Usa current_set (hash plano) para comprobar si
// un valor leido pertenece al conjunto objetivo del nivel actual, y la
// frontera incremental para construir las cadenas completas.
PointerScanResult pointer_scan(Memory& mem, const std::vector<Region>& regions,
                               const PointerScanOptions& opts);

// --- Funciones puras (expuestas para tests unitarios) ----------------------

// Conjunto hash plano (open addressing) para pertenencia O(1). El valor 0 se
// usa como marcador de "vacio": las direcciones de espacio de usuario nunca
// son 0. Se mantiene plano para no pagar el overhead de std::unordered_set.
class FlatHashSet {
public:
    FlatHashSet() = default;
    explicit FlatHashSet(std::vector<uint64_t> values) { build(std::move(values)); }
    void build(std::vector<uint64_t> values);
    bool contains(uint64_t v) const;
    size_t size() const { return size_; }

private:
    void insert(uint64_t v);
    std::vector<uint64_t> slots_;
    size_t mask_ = 0;
    size_t size_ = 0;
};

// Extiende la frontera con las aristas de un nivel (deben venir ordenadas
// por target): para cada cadena cuyo head (nodes[0]) es el target de alguna
// arista, crea [source] + chain. Descarta un source ya presente en esa misma
// cadena (control de ciclos por cadena). Rellena chains_truncated si se
// alcanza max_chains. Funcion pura: no toca memoria.
std::vector<PointerChain> extend_chains(const std::vector<PointerChain>& frontier,
                                        const std::vector<PointerEdge>& edges_sorted,
                                        size_t max_chains, bool& chains_truncated);

} // namespace mt
