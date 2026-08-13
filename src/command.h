// command.h - Capa de comandos de MemoryTool (frontend CLI).
//
// Separa el dispatch de comandos del resto de la interfaz. Cada comando es
// una funcion independiente registrada en una tabla (commands()); anadir un
// comando nuevo = anadir un handler + una entrada en la tabla (y su linea de
// ayuda), sin tocar bloques gigantes de if/else.
//
//   Frontend (CLI/GUI) -> Application -> Session -> Core
//
// La logica de cada operacion vive en Application (application.h): los
// handlers de aqui se limitan a parsear texto, llamar a Application y
// formatear/imprimir el resultado. La futura GUI reutilizara Application
// directamente, sin pasar por el parsing de texto.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "application.h"

namespace mt {

// Resultado de ejecutar un comando.
struct CommandResult {
    bool quit = false;     // true -> el frontend debe terminar (quit/exit)
    bool handled = true;   // false -> comando desconocido (ya se aviso)
};

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
// Formateadores de la CLI (solo presentacion; la logica vive en Application).

// Representacion textual de una cadena: "0xA -> 0xB -> ... -> TARGET" (el
// ultimo nodo es el target del escaneo). La usa 'pointer results' y tambien
// la descripcion de la entrada de la tabla creada por 'pointer add'.
std::string pointer_chain_description(const std::vector<uint64_t>& nodes);

// Igual que la anterior pero intercalando los offsets de la cadena V2:
// "0xA -> +0x20 -> 0xB -> +0x18 -> TARGET". Si offsets.size() no cuadra con
// nodes (cadenas V1), se comporta como la version sin offsets.
std::string pointer_chain_description(const std::vector<uint64_t>& nodes,
                                      const std::vector<uint64_t>& offsets);

} // namespace mt
