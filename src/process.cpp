// process.cpp - Listado de procesos leyendo /proc.
#include "process.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace mt {

static bool is_numeric(const char* s) {
    if (!s || !*s) return false;
    for (; *s; ++s)
        if (!std::isdigit((unsigned char)*s)) return false;
    return true;
}

static std::string read_file(const std::string& path) {
    std::string out;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// /proc/PID/stat: "123 (comm) S ..." - el nombre puede contener espacios y
// parentesis, asi que buscamos el primer '(' y el ultimo ')'.
static void parse_stat(int pid, std::string& comm, char& state) {
    comm.clear();
    state = '?';
    std::string s = read_file("/proc/" + std::to_string(pid) + "/stat");
    if (s.empty()) return;
    size_t lp = s.find('(');
    size_t rp = s.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || rp <= lp) return;
    comm = s.substr(lp + 1, rp - lp - 1);
    const char* p = s.c_str() + rp + 1;
    while (*p == ' ') ++p;
    if (*p) state = *p;
}

// /proc/PID/status: Uid, VmRSS...
static void parse_status(int pid, int& uid, long& rss_kb) {
    uid = -1;
    rss_kb = 0;
    std::string s = read_file("/proc/" + std::to_string(pid) + "/status");
    if (s.empty()) return;
    for (const char* p = s.c_str(); *p;) {
        const char* eol = p;
        while (*eol && *eol != '\n') ++eol;
        std::string line(p, eol);
        if (line.rfind("Uid:", 0) == 0) {
            int a = 0;
            if (sscanf(line.c_str(), "Uid:\t%d", &a) == 1) uid = a;
        } else if (line.rfind("VmRSS:", 0) == 0) {
            long kb = 0;
            if (sscanf(line.c_str(), "VmRSS:\t%ld", &kb) == 1) rss_kb = kb;
        }
        p = *eol ? eol + 1 : eol;
    }
}

std::vector<ProcessInfo> list_processes() {
    std::vector<ProcessInfo> out;
    DIR* d = opendir("/proc");
    if (!d) return out;

    const uid_t me = geteuid();
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!is_numeric(e->d_name)) continue;
        ProcessInfo pi;
        pi.pid = atoi(e->d_name);
        parse_stat(pi.pid, pi.name, pi.state);
        parse_status(pi.pid, pi.uid, pi.rss_kb);

        if (pi.uid >= 0) {
            struct passwd* pw = getpwuid((uid_t)pi.uid);
            pi.user = pw ? pw->pw_name : ("uid:" + std::to_string(pi.uid));
            pi.accessible = (pi.uid == (int)me);
        } else {
            pi.user = "?";
        }
        if (pi.name.empty()) pi.name = "?";
        out.push_back(std::move(pi));
    }
    closedir(d);

    std::sort(out.begin(), out.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) { return a.pid < b.pid; });
    return out;
}

} // namespace mt
