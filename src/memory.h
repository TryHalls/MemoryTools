// memory.h - Regiones de memoria (/proc/PID/maps) y acceso autorizado a la
// memoria de un proceso (/proc/PID/mem) mediante ptrace.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mt {

struct Region {
    uint64_t start = 0;
    uint64_t end = 0;
    std::string perms;   // "r--", "rw-", "r-x", "rwx", ... (+p/s)
    uint64_t offset = 0;
    std::string dev;
    uint64_t inode = 0;
    std::string path;    // archivo asociado o "[heap]", "[stack]"...

    uint64_t size() const { return end - start; }
    bool readable() const { return !perms.empty() && perms[0] == 'r'; }
    bool writable() const { return perms.size() > 1 && perms[1] == 'w'; }
    bool executable() const { return perms.size() > 2 && perms[2] == 'x'; }
};

// Lee /proc/PID/maps del proceso. Devuelve lista vacia si no hay acceso.
std::vector<Region> parse_maps(int pid);

// Parsea UNA linea de /proc/PID/maps (formato: start-end perms offset dev
// inode [pathname]). Devuelve false si la linea no tiene el formato esperado.
// Expuesta para poder testear el parseo con strings simulados.
bool parse_maps_line(const std::string& line, Region& out);

// Region que contiene la direccion dada (si existe).
std::optional<Region> region_at(const std::vector<Region>& regions, uint64_t addr);

// Acceso a la memoria de un proceso:
//   open()  -> solicita traza (PTRACE_SEIZE/ATTACH) y abre /proc/PID/mem.
//              El proceso objetivo se detiene brevemente mientras dura la
//              operacion, y se reanuda al hacer close().
//   read()  -> pread en la direccion absoluta.
//   write() -> pwrite en la direccion absoluta.
class Memory {
public:
    ~Memory();
    Memory() = default;
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    bool open(int pid, std::string& err);
    void close();
    bool is_open() const { return fd_ >= 0; }
    int pid() const { return pid_; }

    ssize_t read(uint64_t addr, void* buf, size_t len) const;
    ssize_t write(uint64_t addr, const void* buf, size_t len) const;

private:
    int fd_ = -1;
    int pid_ = 0;
    bool traced_ = false;
};

} // namespace mt
