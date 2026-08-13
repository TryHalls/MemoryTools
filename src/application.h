// application.h - Capa de aplicacion headless de MemoryTool.
//
// Separa la LOGICA de aplicacion (orquestar operaciones sobre Session/Core)
// de la presentacion (CLI/GUI). NINGUNA funcion imprime: cada operacion
// devuelve un resultado estructurado con datos y errores. La futura GUI
// usara esta misma capa; la CLI (command.cpp) es un frontend que parsea
// texto, llama a Application y formatea el resultado.
//
//   CLI / GUI
//      |
//      v
//   Application
//      |
//      v
//   Session -> Core (Scanner / Memory / AddressTable / Pointer / ...)
//
// Esta capa es sincrona (sin hilos); las APIs se disenan para poder anadir
// concurrencia, cancelacion y progreso despues sin cambiar su forma.
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "pattern.h"
#include "pointer.h"
#include "session.h"
#include "types.h"

namespace mt {

// Argumentos de un comando/funcion: tokens de texto (los usa la CLI y el
// parsing reutilizable).
using CommandArgs = std::vector<std::string>;

// Parsea una direccion (hex 0x... o decimal). Funcion pura, reutilizable por
// CLI y GUI (view/set/table add/pointer scan).
inline bool parse_addr(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    int base = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    out = strtoull(s.c_str(), &end, base);
    return end != s.c_str() && *end == '\0' && errno != ERANGE;
}

// Limite de seguridad de la ventana de offsets del pointer scan (IMP-2):
// floor(max_offset / offset_step) + 1 offsets por objetivo debe ser <=
// kMaxOffsetShifts. Evita que una combinacion de max_offset/offset_step
// (p. ej. 0x10000 con step 1) genere 65.537 posiciones por objetivo.
inline constexpr uint64_t kMaxOffsetShifts = 65536;

// Mensaje de error si el tipo propuesto para el 'next' NUMERICO no coincide
// con el tipo del 'first' (los valores y filtros se interpretan con el tipo
// original del escaneo). Devuelve texto vacio si son compatibles. Funcion
// pura (IMP-3).
inline std::string next_type_mismatch_message(DataType first, DataType proposed) {
    if (first == proposed) return {};
    return std::string("El tipo del Next Scan debe coincidir con el First Scan (")
           + type_name(first) + ").";
}

// --- Resultados estructurados ----------------------------------------------

// Resultado generico (operaciones sin datos que devolver).
struct OperationResult {
    bool ok = true;
    std::string error;
};

struct AttachOutcome {
    bool ok = false;
    std::string error;
    bool switched = false; // habia otro proceso objetivo (se descartaron resultados)
};

struct ScanOutcome {
    bool ok = false;
    std::string error;
    size_t count = 0;
    bool truncated = false;
    bool warned = false;
};

struct PatternOutcome {
    bool ok = false;
    std::string error;
    std::vector<uint64_t> hits;
    bool truncated = false;
};

struct ReadBytesOutcome {
    bool ok = false;       // se pudo abrir la memoria del proceso
    std::string error;
    uint64_t address = 0;
    ssize_t got = -1;      // bytes leidos por pread; < 0 = error de lectura
    std::vector<uint8_t> bytes;
};

struct InfoOutcome {
    bool ok = false;
    std::string error;
    std::optional<Region> region;
};

struct WriteOutcome {
    bool ok = false;       // escritura + relectura de verificacion completas
    std::string error;
    uint64_t address = 0;
    DataType type = DataType::I32;
    bool had_old = false;  // se pudo leer el valor anterior
    Value old_value;
    bool wrote = false;    // la relectura de verificacion tuvo exito (hay Nuevo:)
    Value new_value;
    bool verified = false; // relectura == valor escrito
};

struct EntryReadOutcome {
    size_t index = 0;        // indice de la entrada en la tabla
    bool attempted = false;  // la entrada estaba habilitada (se intento leer)
    bool ok = false;
    std::string error;       // listo para mostrar tras "[idx] "
    uint64_t address = 0;
    DataType type = DataType::I32;
    Value value;
    bool have_value = false;
    bool was_stale = false;  // estado stale ANTES de la operacion
};

struct ResolveEntryOutcome {
    bool ok = false;
    std::string error;
    uint64_t address = 0;
    Value value;
    DataType type = DataType::I32;
};

struct AddChainOutcome {
    bool ok = false;
    std::string error;
    size_t table_index = 0;
    PointerChainRef ref;     // para que el frontend muestre kind/tipo/cadena
};

struct PointerScanInput {
    PointerScanOptions opts;
    DataType value_type = DataType::I32;
    bool module_only = false; // solo cadenas con raiz de modulo (persistente)
};

struct PointerScanOutcome {
    bool ok = false;
    std::string error;
    PointerScanResult result;
};

// --- Parsing reutilizable del comando 'pointer scan' -----------------------

// Argumentos ya validados de 'pointer scan <dir> [depth=N] [max_offset=X]
// [offset_step=S] [module-only] [code] [type=T]'. Si 'error' no esta vacio
// los argumentos son invalidos y el resto de campos no es fiable.
struct PointerScanArgs {
    uint64_t target = 0;
    int depth = 3;              // 1..7
    uint64_t max_offset = 0x100; // ventana de offsets (0 incluido; max 0x10000)
    uint64_t offset_step = 8;    // > 0
    bool include_code = false;
    bool module_only = false;    // solo cadenas con raiz de modulo
    std::optional<DataType> value_type; // type=T (por defecto: tipo de sesion)
    std::string error;           // vacio = valido
};

// Parsea y valida los argumentos de 'pointer scan' (las mismas reglas que la
// CLI; la GUI reutilizara esta funcion para construir PointerScanInput).
PointerScanArgs parse_pointer_scan_args(const CommandArgs& args);

// --- Application ------------------------------------------------------------

class Application {
public:
    explicit Application(Session& session) : session_(session) {}

    // --- Proceso objetivo -------------------------------------------------
    AttachOutcome attach(int pid);
    OperationResult detach();
    bool has_pid() const { return session_.has_pid(); }
    int pid() const { return session_.pid(); }

    // --- Escaneos ---------------------------------------------------------
    ScanOutcome first_scan(DataType type, const std::optional<Value>& target);
    ScanOutcome next_scan(DataType type, Filter filter,
                          const std::optional<Value>& target);
    ScanOutcome first_scan_dynamic(const DynamicScanSpec& spec);
    ScanOutcome next_scan_dynamic(Filter filter,
                                  const std::optional<DynamicScanSpec>& newspec);
    PatternOutcome pattern_scan(const BytePattern& pat);

    // --- Memoria ----------------------------------------------------------
    ReadBytesOutcome read_bytes(uint64_t addr, size_t len);
    InfoOutcome region_info(uint64_t addr);
    WriteOutcome write(uint64_t addr, DataType type, const Value& value);

    // --- Address Table ----------------------------------------------------
    size_t add_entry(uint64_t addr, DataType type, const std::string& desc);
    size_t add_result_entry(size_t result_index, const std::string& desc);
    bool remove_entry(size_t idx);
    void clear_entries();
    bool save_table(const std::string& path, std::string& err);
    bool load_table(const std::string& path, std::string& err);
    EntryReadOutcome read_entry(size_t idx);
    void read_all_entries(std::vector<EntryReadOutcome>& out);
    WriteOutcome write_entry(size_t idx, const Value& value);
    AddressEntry* entry(size_t idx);
    size_t table_size() const { return session_.table().size(); }
    const AddressTable& table() const { return session_.table(); }

    // --- Pointer Scanner --------------------------------------------------
    PointerScanOutcome pointer_scan(const PointerScanInput& in);
    AddChainOutcome add_pointer_chain(size_t chain_index,
                                      const std::string& description);
    ResolveEntryOutcome resolve_entry(size_t idx);

    // --- Estado de la sesion (lectura para formateadores de los frontends) -
    Session& session() { return session_; }
    const Session& session() const { return session_; }

private:
    Session& session_;
};

} // namespace mt
