// session.h - Estado de una sesion interactiva de MemoryTool.
//
// Encapsula el estado que vive durante una sesion (proceso objetivo,
// escaner de First/Next Scan y tipo de dato actual) y las operaciones que
// dependen de ese estado. NO formatea salida: imprimir resultados es
// responsabilidad del frontend (CLI/GUI), que usa Session a traves de la
// capa de comandos (command.h).
//
//   Frontend (CLI/GUI) -> Command -> Session -> Core (Scanner/Memory/...)
//
// La logica del core no depende de esta clase ni de la CLI.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "address_table.h"
#include "memory.h"
#include "pointer.h"
#include "scanner.h"
#include "types.h"

namespace mt {

class Session {
public:
    Session() = default;

    // --- Proceso objetivo ------------------------------------------------
    bool has_pid() const { return pid_.has_value(); }
    int pid() const { return pid_.value_or(0); }

    // Adjunta un proceso como objetivo: comprueba acceso real (abre y cierra
    // /proc/PID/mem) y, si ya habia otro proceso seleccionado, descarta los
    // resultados del escaner (pertenecen al PID anterior). Devuelve false y
    // rellena err si no se puede acceder; en ese caso el estado no cambia.
    bool attach(int target, std::string& err);

    // Quita el proceso objetivo y descarta los resultados del escaner.
    void detach();

    // Ejecuta fn con la memoria del proceso objetivo abierta (attach +
    // operacion + detach por llamada). Devuelve false y rellena err si no
    // hay proceso objetivo o no se pudo abrir la memoria.
    bool with_memory(const std::function<void(Memory&)>& fn, std::string& err);

    // --- Escaner de la sesion -------------------------------------------
    Scanner& scanner() { return scanner_; }
    const Scanner& scanner() const { return scanner_; }

    // Tipo de dato del escaneo actual (lo usa 'next'/'results' si no se
    // indica otro explícitamente).
    DataType scan_type() const { return scan_type_; }
    void set_scan_type(DataType t) { scan_type_ = t; }

    // --- Tabla de direcciones -------------------------------------------
    // Pertenece a la sesion: al cambiar de proceso objetivo sus entradas se
    // marcan stale (las direcciones absolutas dejan de ser fiables).
    AddressTable& table() { return table_; }
    const AddressTable& table() const { return table_; }

    // --- Pointer Scanner -------------------------------------------------
    // Ultimo resultado de 'pointer scan'. Pertenecen al proceso objetivo
    // actual: al cambiar de proceso o hacer detach se descartan.
    const std::optional<PointerScanResult>& pointer_result() const {
        return last_pointer_;
    }
    void set_pointer_result(PointerScanResult r) {
        last_pointer_ = std::move(r);
    }

private:
    std::optional<int> pid_;
    Scanner scanner_;
    DataType scan_type_ = DataType::I32;
    AddressTable table_;
    std::optional<PointerScanResult> last_pointer_;
};

} // namespace mt
