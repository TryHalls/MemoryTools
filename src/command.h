// command.h - Capa de comandos de MemoryTool.
//
// Separa el dispatch de comandos del resto de la interfaz. Cada comando es
// una funcion independiente registrada en una tabla (commands()); anadir un
// comando nuevo = anadir un handler + una entrada en la tabla (y su linea de
// ayuda), sin tocar bloques gigantes de if/else.
//
//   Frontend (CLI/GUI) -> execute() -> handler -> Session -> Core
//
// El handler recibe los argumentos y la sesion, y es quien imprime el
// resultado (el formateo es responsabilidad del frontend, no del core).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mt {

class Session;

// Resultado de ejecutar un comando.
struct CommandResult {
    bool quit = false;     // true -> el frontend debe terminar (quit/exit)
    bool handled = true;   // false -> comando desconocido (ya se aviso)
};

// Argumentos de un comando: tokens que siguen al nombre del comando.
using CommandArgs = std::vector<std::string>;

// Handler de un comando. Recibe los argumentos (sin el nombre) y la sesion.
using CommandFn = CommandResult (*)(const CommandArgs&, Session&);

struct CommandDef {
    const char* name;    // nombre del comando (o alias)
    const char* usage;   // texto de 'help'; nullptr = alias oculto
    CommandFn fn;
};

// Tabla de comandos registrados (el orden es el que muestra 'help').
const std::vector<CommandDef>& commands();

// Ejecuta una linea de comando completa (divide en tokens y despacha).
//  - linea vacia: no hace nada (handled=true, quit=false).
//  - comando desconocido: imprime el aviso y devuelve handled=false.
CommandResult execute(const std::string& line, Session& s);

// Resuelve "1234" (PID) o un nombre de proceso a un PID. Imprime el motivo
// cuando no se encuentra. Lo usan 'attach'/'maps' y la CLI por argumentos.
std::optional<int> resolve_target(const std::string& s);

// Muestra las regiones de memoria de un PID (formato de la CLI). Lo usan el
// comando 'maps' y la forma por argumentos "memorytool maps <pid>".
void print_maps(int pid);

// ---------------------------------------------------------------------------
// Pointer Scanner (CLI): helpers puros expuestos para poder testear el
// parsing y el formateo sin depender de un proceso real.

// Argumentos ya validados de 'pointer scan <dir> [depth=N] [code]'. Si
// 'error' no esta vacio los argumentos son invalidos (direccion o opcion
// desconocida, depth fuera de 1..7) y el resto de campos no es fiable.
struct PointerScanArgs {
    uint64_t target = 0;
    int depth = 3;          // valor por defecto
    bool include_code = false;
    std::string error;      // vacio = valido
};

// Parsea y valida los argumentos de 'pointer scan'. La direccion se acepta
// en hex (0x...) o decimal (misma regla que 'view'/'set'). 'code' activa
// include_code; 'depth=N' con N en 1..7.
PointerScanArgs parse_pointer_scan_args(const CommandArgs& args);

// Representacion textual de una cadena: "0xA -> 0xB -> ... -> TARGET" (el
// ultimo nodo es el target del escaneo). La usa 'pointer results' y tambien
// la descripcion de la entrada de la tabla creada por 'pointer add'.
std::string pointer_chain_description(const std::vector<uint64_t>& nodes);

} // namespace mt
