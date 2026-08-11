// address_table.h - Tabla de direcciones de la sesion.
//
// Almacena direcciones absolutas encontradas por el escaner (o anadidas a
// mano) para seguirlas observando y modificando durante la sesion. Es
// almacenamiento puro: NO depende de la CLI ni de Memory. La lectura y
// escritura de memoria se delega en Session/Memory desde la capa de
// comandos; aqui solo se guardan los datos que no se pueden releer de
// memoria (direccion, tipo, descripcion, estado).
//
// El valor actual NO se almacena: se relee de memoria cuando hace falta.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace mt {

// Una entrada de la tabla.
struct AddressEntry {
    uint64_t address = 0;      // direccion absoluta (depende de ASLR)
    DataType type = DataType::I32;
    std::string description;
    bool enabled = true;       // las operaciones de memoria respetan esto
    bool stale = false;        // direccion posiblemente invalida (cambio de proceso)
};

class AddressTable {
public:
    // Anade una entrada y devuelve su indice.
    size_t add(uint64_t address, DataType type,
               const std::string& description = "");

    // Elimina la entrada 'index'. Devuelve false si no existe.
    bool remove(size_t index);

    void clear();
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    // Acceso por indice (nullptr si no existe).
    AddressEntry* get(size_t index);
    const AddressEntry* get(size_t index) const;

    // Marca todas las entradas como stale: al cambiar de proceso objetivo
    // las direcciones absolutas dejan de ser fiables.
    void mark_all_stale();

    // Guarda/carga la tabla en formato de texto (ver README). En caso de
    // error devuelve false y rellena 'err'. 'load' reemplaza la tabla.
    bool save(const std::string& path, std::string& err) const;
    bool load(const std::string& path, std::string& err);

private:
    std::vector<AddressEntry> entries_;
};

} // namespace mt
