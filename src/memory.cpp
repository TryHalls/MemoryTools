// memory.cpp - Regiones de memoria y acceso a /proc/PID/mem con ptrace.
// Nota: g++ define _GNU_SOURCE automaticamente en glibc (necesario para
// __WALL, __WCLONE...).
#include "memory.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mt {

static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::vector<Region> parse_maps(int pid) {
    std::vector<Region> out;
    FILE* f = fopen(("/proc/" + std::to_string(pid) + "/maps").c_str(), "r");
    if (!f) return out;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        Region r;
        char perms[8] = {0};
        char dev[16] = {0};
        unsigned long start = 0, end = 0, off = 0;
        unsigned long long inode = 0;
        int consumed = 0;
        // formato: start-end perms offset dev inode pathname
        int n = sscanf(line, "%lx-%lx %7s %lx %15s %llu %n",
                       &start, &end, perms, &off, dev, &inode, &consumed);
        if (n < 6) continue;
        r.start = start;
        r.end = end;
        r.perms = perms;
        r.offset = off;
        r.dev = dev;
        r.inode = (uint64_t)inode;
        if (consumed > 0) r.path = trim(line + consumed);
        out.push_back(std::move(r));
    }
    fclose(f);
    return out;
}

std::optional<Region> region_at(const std::vector<Region>& regions, uint64_t addr) {
    for (const auto& r : regions)
        if (addr >= r.start && addr < r.end) return r;
    return std::nullopt;
}

// ---------------------------------------------------------------------------

Memory::~Memory() { close(); }

bool Memory::open(int pid, std::string& err) {
    close();
    err.clear();

    const std::string proc = "/proc/" + std::to_string(pid);
    if (access(proc.c_str(), F_OK) != 0) {
        err = "no existe el proceso " + std::to_string(pid);
        return false;
    }

    // 1) Solicitar traza. PTRACE_SEIZE no detiene el proceso: pedimos despues
    //    una parada explicita con PTRACE_INTERRUPT. Si SEIZE no esta
    //    disponible, usamos el clasico PTRACE_ATTACH (que envia SIGSTOP).
    long r = ptrace(PTRACE_SEIZE, pid, nullptr, 0);
    if (r == -1) {
        r = ptrace(PTRACE_ATTACH, pid, nullptr, 0);
    } else {
        // SEIZE no detiene el proceso: pedimos una parada explicita con
        // PTRACE_INTERRUPT. Si falla (p. ej. el proceso murio justo despues
        // de SEIZE), esperar en waitpid podria bloquearse indefinidamente:
        // liberamos la relacion de traza (detach) y reportamos el error.
        if (ptrace(PTRACE_INTERRUPT, pid, nullptr, 0) == -1) {
            err = std::string("ptrace(PTRACE_INTERRUPT): ") + strerror(errno);
            traced_ = true; // la relacion de traza quedo concedida: cerrarla
            close();
            return false;
        }
    }
    if (r == -1) {
        if (errno == EPERM)
            err = "permiso denegado (ptrace_scope=1 / Yama). El proceso debe "
                  "ser hijo nuestro o haber concedido PR_SET_PTRACER.";
        else if (errno == ESRCH)
            err = "el proceso no existe o es un kernel thread";
        else
            err = std::string("ptrace: ") + strerror(errno);
        return false;
    }
    traced_ = true;

    // 2) Esperar la parada de ptrace. El tracee se "reparenta" al tracer,
    //    asi que waitpid funciona aunque no sea hijo directo.
    int status = 0;
    pid_t w = waitpid(pid, &status, __WALL | WUNTRACED);
    if (w == -1) {
        err = std::string("waitpid: ") + strerror(errno);
        close();
        return false;
    }
    if (!WIFSTOPPED(status)) {
        err = "el proceso no se detuvo (estado inesperado)";
        close();
        return false;
    }

    // 3) Abrir /proc/PID/mem. Se prefiere O_RDWR (para escritura); si no se
    //    puede, O_RDONLY (solo lectura). El permiso se valida al abrir.
    const std::string mempath = proc + "/mem";
    fd_ = ::open(mempath.c_str(), O_RDWR);
    if (fd_ == -1) fd_ = ::open(mempath.c_str(), O_RDONLY);
    if (fd_ == -1) {
        err = std::string("open /proc/PID/mem: ") + strerror(errno);
        close();
        return false;
    }

    pid_ = pid;
    return true;
}

void Memory::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (traced_) {
        // CRIT-3: si el detach falla, avisar y reanudar el proceso como
        // ultimo recurso. ESRCH es benigno (el tracee ya no existe, o esta
        // corriendo sin parada de ptrace: el kernel libera la traza cuando
        // nuestro proceso termina); no hay nada que reanudar en ese caso.
        if (ptrace(PTRACE_DETACH, pid_, nullptr, 0) == -1 && errno != ESRCH) {
            fprintf(stderr,
                    "AVISO (MemoryTool): no se pudo detach del proceso %d: %s.\n"
                    "Reanudando el proceso con PTRACE_CONT (mejor esfuerzo).\n",
                    pid_, strerror(errno));
            ptrace(PTRACE_CONT, pid_, nullptr, 0);
        }
        traced_ = false;
    }
    pid_ = 0;
}

ssize_t Memory::read(uint64_t addr, void* buf, size_t len) const {
    if (fd_ < 0) return -1;
    return pread(fd_, buf, len, (off_t)addr);
}

ssize_t Memory::write(uint64_t addr, const void* buf, size_t len) const {
    if (fd_ < 0) return -1;
    return pwrite(fd_, buf, len, (off_t)addr);
}

} // namespace mt
