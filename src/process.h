// process.h - Gestion de procesos (listado desde /proc).
#pragma once

#include <string>
#include <vector>

namespace mt {

struct ProcessInfo {
    int pid = 0;
    std::string name;    // comm (nombre del ejecutable)
    int uid = -1;
    std::string user;    // nombre del usuario o "uid:N"
    char state = '?';    // R, S, D, Z, T...
    long rss_kb = 0;
    bool accessible = false; // heuristico: mismo UID que nosotros
};

// Lista los procesos visibles en /proc ordenados por PID.
std::vector<ProcessInfo> list_processes();

} // namespace mt
