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
//
// Cancelacion: pointer_scan acepta un flag atomico opcional que se comprueba
// al comienzo de cada nivel, tras cada recorrido por bloques y antes de
// extender cadenas (ademas del check por bloque dentro de for_each_window).
// Si se cancela se devuelve un resultado con cancelled = true y sin cadenas
// (nunca se publica un resultado parcial); el resultado anterior de la
// sesion queda intacto.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "chunk.h"
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
    // Ventana de offsets (V2): al leer un puntero V en 'source', se
    // comprueba (V + o) para cada o = 0, offset_step, ... <= max_offset.
    // offset 0 siempre incluido (V1 es el caso de ventana {0}).
    uint64_t max_offset = 0x100;
    uint64_t offset_step = 8;
};

// Una arista: en la direccion source hay un puntero (valor de 8 bytes) V
// tal que V + offset == target. Es la relacion que permite reconstruir las
// cadenas sin rescanear (source --offset--> target).
struct PointerEdge {
    uint64_t source = 0;
    uint64_t target = 0;
    uint64_t offset = 0; // V2: mem[source] + offset == target (0 en V1)
};

// Una cadena completa [root ... TARGET]; nodes.back() == target del scan.
// depth = numero de derefs = nodes.size() - 1. offsets tiene el mismo
// tamano que depth: offsets[i] es el offset aplicado DESPUES del deref de
// nodes[i] para alcanzar nodes[i+1] (el ultimo localiza el valor final).
struct PointerChain {
    std::vector<uint64_t> nodes;
    std::vector<uint64_t> offsets; // V2 (vacio en cadenas V1)
    int depth = 0;
};

struct PointerScanResult {
    uint64_t target = 0;
    std::vector<PointerChain> chains;
    bool edges_truncated = false;  // se alcanzo max_edges_per_level en un nivel
    bool chains_truncated = false; // se alcanzo max_chains
    bool cancelled = false;        // true si el escaneo fue cancelado
    int levels = 0;                // niveles completados con resultados
    size_t total_edges = 0;        // aristas encontradas en total
    DataType value_type = DataType::I32; // tipo del valor final (para add)
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
// arista, crea [source] + chain, anteponiendo tambien el offset de la
// arista a los offsets de la cadena. Descarta un source ya presente en esa
// misma cadena (control de ciclos por cadena, sin visited global). Rellena
// chains_truncated si se alcanza max_chains. Funcion pura: no toca memoria.
std::vector<PointerChain> extend_chains(const std::vector<PointerChain>& frontier,
                                        const std::vector<PointerEdge>& edges_sorted,
                                        size_t max_chains, bool& chains_truncated);

// --- Escaneo ----------------------------------------------------------------

// Busca cadenas de punteros hacia opts.target en las regiones dadas.
// mem debe estar ya abierta. Usa current_set (hash plano) para comprobar si
// un valor leido pertenece al conjunto objetivo del nivel actual, y la
// frontera incremental para construir las cadenas completas.
//
// cancel: flag atomico opcional; se comprueba al comienzo de cada nivel,
// tras el recorrido por bloques del nivel y antes de extender cadenas
// (ademas del check por bloque dentro de for_each_window). Si se activa, se
// devuelve un resultado con cancelled = true y sin cadenas (no se publica
// ningun resultado parcial).
// progress: callback opcional (bytes_scanned, total_bytes). El total es la
// suma de bytes de las regiones fuente por el numero de niveles; el progreso
// es monotonico, nunca supera el total y llega a 100% al completar
// normalmente (no llega a 100% si se cancela).
// 'Mem' es generico (igual que for_each_window) para poder testear el
// recorrido con un fake; los llamadores reales pasan un Memory&.
template <typename Mem>
PointerScanResult pointer_scan(Mem& mem, const std::vector<Region>& regions,
                               const PointerScanOptions& opts,
                               const std::atomic<bool>* cancel = nullptr,
                               const ProgressFn& progress = {}) {
    PointerScanResult res;
    res.target = opts.target;
    if (opts.target == 0 || opts.max_depth <= 0) return res;

    std::vector<Region> source = select_pointer_regions(regions, opts.include_code);
    if (opts.min_addr || opts.max_addr) {
        std::vector<Region> filtered;
        for (Region r : source) { // copia para recortar al rango
            if (opts.max_addr && r.start >= opts.max_addr) continue;
            if (opts.min_addr && r.end <= opts.min_addr) continue;
            if (opts.min_addr && r.start < opts.min_addr) r.start = opts.min_addr;
            if (opts.max_addr && r.end > opts.max_addr) r.end = opts.max_addr;
            if (r.end > r.start) filtered.push_back(r);
        }
        source = std::move(filtered);
    }

    std::vector<uint64_t> current_values{opts.target};
    FlatHashSet current_set(current_values);
    const uint64_t step = opts.offset_step == 0 ? 1 : opts.offset_step;

    std::vector<PointerChain> frontier;
    frontier.push_back(PointerChain{{opts.target}, {}, 0});

    // Progreso: total = bytes de las regiones fuente por nivel x max_depth
    // (cada nivel recorre las mismas regiones fuente).
    const uint64_t level_total = readable_total(source);
    const uint64_t grand_total =
        level_total * (uint64_t)std::max(opts.max_depth, 1);
    uint64_t scanned_prev = 0; // bytes cubiertos en niveles anteriores

    auto is_cancelled = [&]() -> bool {
        return cancel && cancel->load(std::memory_order_relaxed);
    };
    // Resultado cancelado: sin cadenas parciales y sin flags de truncado.
    auto finish_cancelled = [&]() -> PointerScanResult {
        res.chains.clear();
        res.levels = 0;
        res.total_edges = 0;
        res.edges_truncated = false;
        res.chains_truncated = false;
        res.cancelled = true;
        return res;
    };

    for (int d = 1; d <= opts.max_depth; ++d) {
        // 0) Check de cancelacion al comienzo de cada nivel (entre niveles).
        if (is_cancelled()) return finish_cancelled();

        // 0b) Conjunto desplazado: { t - o : t in current_values, o en
        //     ventana }. Permite comprobar una posicion con UNA sola busqueda
        //     y ampliar a los offsets solo cuando hay coincidencia (evita
        //     |posiciones| x |ventana| busquedas por nivel).
        std::vector<uint64_t> shifted_vals;
        shifted_vals.reserve(current_values.size() *
                             ((opts.max_offset / step) + 1));
        for (uint64_t t : current_values)
            for (uint64_t o = 0; o <= opts.max_offset; o += step)
                shifted_vals.push_back(t - o);
        FlatHashSet shifted(std::move(shifted_vals));

        // 1) Escanear regiones fuente: posiciones alineadas a 8 bytes cuyo
        //    valor V satisface (V + o) in current_set para algun offset o.
        std::vector<PointerEdge> edges;
        bool stopped = false;
        auto cb = [&](const uint8_t* win, uint64_t addr) -> bool {
            uint64_t v;
            std::memcpy(&v, win, sizeof(v));
            if (!shifted.contains(v)) return true;
            for (uint64_t o = 0; o <= opts.max_offset; o += step) {
                if (current_set.contains(v + o)) {
                    edges.push_back(PointerEdge{addr, v + o, o});
                    if (edges.size() >= opts.max_edges_per_level) {
                        res.edges_truncated = true;
                        stopped = true;
                        return false; // detener todo el recorrido
                    }
                }
            }
            return true;
        };
        // El progreso del nivel se suma a los niveles anteriores: global.
        auto level_progress = [&](uint64_t s, uint64_t /*t*/) {
            if (progress) progress(scanned_prev + s, grand_total);
        };
        for_each_window(mem, source, 8, 8, cb, cancel, level_progress);

        // 2) Check tras el recorrido del nivel (cancel durante el nivel).
        if (is_cancelled()) return finish_cancelled();

        if (edges.empty()) break; // sin referencias nuevas: fin del escaneo
        res.levels = d;
        res.total_edges += edges.size();

        // 3) Indice por target (y offset) para la extension de la frontera.
        std::sort(edges.begin(), edges.end(),
                  [](const PointerEdge& a, const PointerEdge& b) {
                      if (a.target != b.target) return a.target < b.target;
                      if (a.offset != b.offset) return a.offset < b.offset;
                      return a.source < b.source;
                  });

        // 4) Check antes de extender cadenas.
        if (is_cancelled()) return finish_cancelled();

        // 5) Extender la frontera un nivel hacia atras.
        bool ctrunc = false;
        std::vector<PointerChain> next =
            extend_chains(frontier, edges, opts.max_chains, ctrunc);
        if (ctrunc) res.chains_truncated = true;
        if (next.empty()) break;

        res.chains.insert(res.chains.end(), next.begin(), next.end());
        frontier = std::move(next);

        // 6) Siguiente nivel: S_d = fuentes de este nivel.
        std::vector<uint64_t> srcs;
        srcs.reserve(edges.size());
        for (const PointerEdge& e : edges) srcs.push_back(e.source);
        current_values = srcs;
        current_set.build(std::move(srcs));

        scanned_prev += level_total; // base de progreso del siguiente nivel
        if (stopped) break; // el nivel se trunco: no tiene sentido continuar
    }

    // 100% solo si termino normalmente (nunca al cancelar).
    if (progress && !is_cancelled() && grand_total > 0)
        progress(grand_total, grand_total);
    return res;
}

} // namespace mt
