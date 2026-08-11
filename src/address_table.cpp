// address_table.cpp - Implementacion de AddressTable.
#include "address_table.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace mt {

// ---------------------------------------------------------------------------
// Persistencia: formato de texto v1.
//
// Una entrada por linea:
//   0x00007f1234567890 i32 1 "dinero"
//   ^direccion        ^tipo ^enabled ^descripcion (entre comillas, con
//   escapes \" \\ \n). Las lineas que empiezan por '#' son comentarios.

static std::string quote_desc(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') {
            o += "\\\"";
        } else if (c == '\\') {
            o += "\\\\";
        } else if (c == '\n') {
            o += "\\n";
        } else {
            o += c;
        }
    }
    o += '"';
    return o;
}

// 's' es la descripcion cruda empezando por comilla; devuelve su contenido.
static std::string unquote_desc(const std::string& s) {
    std::string o;
    size_t i = 1;
    size_t n = s.size();
    if (n >= 2 && s[n - 1] == '"') --n; // comilla final
    while (i < n) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            char nx = s[i + 1];
            if (nx == 'n') {
                o += '\n';
            } else {
                o += nx; // \", \\, \t, ...
            }
            i += 2;
            continue;
        }
        o += c;
        ++i;
    }
    return o;
}

// ---------------------------------------------------------------------------

size_t AddressTable::add(uint64_t address, DataType type,
                         const std::string& description) {
    AddressEntry e;
    e.address = address;
    e.type = type;
    e.description = description;
    entries_.push_back(e);
    return entries_.size() - 1;
}

bool AddressTable::remove(size_t index) {
    if (index >= entries_.size()) return false;
    entries_.erase(entries_.begin() + (long)index);
    return true;
}

void AddressTable::clear() { entries_.clear(); }

AddressEntry* AddressTable::get(size_t index) {
    return (index < entries_.size()) ? &entries_[index] : nullptr;
}

const AddressEntry* AddressTable::get(size_t index) const {
    return (index < entries_.size()) ? &entries_[index] : nullptr;
}

void AddressTable::mark_all_stale() {
    for (auto& e : entries_) e.stale = true;
}

bool AddressTable::save(const std::string& path, std::string& err) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        err = std::strerror(errno);
        return false;
    }
    fprintf(f, "# MemoryTool Address Table v1\n");
    for (const auto& e : entries_) {
        fprintf(f, "0x%016llx %s %d %s\n",
                (unsigned long long)e.address,
                type_name(e.type),
                e.enabled ? 1 : 0,
                quote_desc(e.description).c_str());
    }
    if (fclose(f) != 0) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}

bool AddressTable::load(const std::string& path, std::string& err) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        err = std::strerror(errno);
        return false;
    }

    std::vector<AddressEntry> loaded;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
        if (b >= e) continue;
        if (s[b] == '#') continue;

        std::istringstream iss(s.substr(b, e - b));
        std::string addr_s, type_s, en_s;
        if (!(iss >> addr_s >> type_s >> en_s)) continue;

        errno = 0;
        char* end = nullptr;
        unsigned long long addr = std::strtoull(addr_s.c_str(), &end, 0);
        if (end == addr_s.c_str() || *end != '\0' || errno == ERANGE) continue;

        DataType type;
        if (!parse_type(type_s, type)) continue;

        char* en_end = nullptr;
        long en = std::strtol(en_s.c_str(), &en_end, 10);
        if (en_end == en_s.c_str() || *en_end != '\0') continue;

        std::string rest;
        std::getline(iss, rest);
        size_t rb = rest.find_first_not_of(" \t");
        std::string desc;
        if (rb != std::string::npos && rest[rb] == '"') {
            desc = unquote_desc(rest.substr(rb));
        } else if (rb != std::string::npos) {
            desc = rest.substr(rb);
        }

        AddressEntry ent;
        ent.address = (uint64_t)addr;
        ent.type = type;
        ent.enabled = (en != 0);
        ent.description = desc;
        loaded.push_back(std::move(ent));
    }
    fclose(f);

    entries_ = std::move(loaded);
    return true;
}

} // namespace mt
