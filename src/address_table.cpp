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

size_t AddressTable::add(const PointerChainRef& ref, const std::string& description) {
    AddressEntry e;
    e.type = ref.value_type;   // el tipo es el del VALOR FINAL (no 'pointer')
    e.description = description;
    e.ptr = ref;
    if (ref.root.kind == PointerBaseKind::ABSOLUTE)
        e.address = ref.root.address;
    // MODULE: address se deja a 0; se resuelve en cada uso.
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
        if (!e.ptr) {
            // v1: entrada absoluta (sin cambios).
            fprintf(f, "0x%016llx %s %d %s\n",
                    (unsigned long long)e.address,
                    type_name(e.type),
                    e.enabled ? 1 : 0,
                    quote_desc(e.description).c_str());
        } else {
            // v2: entrada dinamica (kind 'pointer'). 'type' es el tipo del
            // valor final; la raiz es module+offset (o direccion absoluta
            // como fallback no persistente); 'steps' son los offsets post-
            // deref de la cadena.
            fprintf(f, "pointer type=%s ", type_name(e.type));
            if (e.ptr->root.kind == PointerBaseKind::MODULE)
                fprintf(f, "module=%s root=0x%llx ", e.ptr->root.module.c_str(),
                        (unsigned long long)e.ptr->root.offset);
            else
                fprintf(f, "root=0x%llx ", (unsigned long long)e.ptr->root.address);
            fprintf(f, "steps=");
            for (size_t i = 0; i < e.ptr->offsets.size(); ++i) {
                if (i > 0) fprintf(f, ",");
                fprintf(f, "0x%llx", (unsigned long long)e.ptr->offsets[i]);
            }
            fprintf(f, " enabled=%d %s\n", e.enabled ? 1 : 0,
                    quote_desc(e.description).c_str());
        }
    }
    if (fclose(f) != 0) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}

// Parsea una linea v2 (primera palabra 'pointer'): clave=valor hasta una
// descripcion entre comillas. Devuelve false si falta algo obligatorio o el
// formato es invalido (la linea se salta, igual que las malformadas v1).
static bool parse_pointer_line(std::istringstream& iss, AddressEntry& ent) {
    std::vector<std::string> kv;
    std::string desc_raw;
    std::string tok;
    while (iss >> tok) {
        if (!tok.empty() && tok[0] == '"') {
            desc_raw = tok;
            std::string rest;
            std::getline(iss, rest);
            desc_raw += rest;
            break;
        }
        kv.push_back(tok);
    }

    std::string module;
    bool have_module = false, have_root = false, have_type = false;
    uint64_t root = 0;
    std::vector<uint64_t> steps;
    for (const std::string& t : kv) {
        size_t eq = t.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = t.substr(0, eq);
        const std::string val = t.substr(eq + 1);
        if (key == "type") {
            if (!parse_type(val, ent.type)) return false;
            // 'pointer' describe el mecanismo, nunca el valor final:
            if (ent.type == DataType::PTR) return false;
            have_type = true;
        } else if (key == "module") {
            module = val;
            have_module = true;
        } else if (key == "root") {
            errno = 0;
            char* end = nullptr;
            unsigned long long v = std::strtoull(val.c_str(), &end, 0);
            if (end == val.c_str() || *end != '\0' || errno == ERANGE) return false;
            root = (uint64_t)v;
            have_root = true;
        } else if (key == "steps") {
            if (val.empty()) continue;
            size_t pos = 0;
            while (pos < val.size()) {
                size_t comma = val.find(',', pos);
                const std::string part = (comma == std::string::npos)
                                             ? val.substr(pos)
                                             : val.substr(pos, comma - pos);
                if (part.empty()) return false;
                errno = 0;
                char* end = nullptr;
                unsigned long long v = std::strtoull(part.c_str(), &end, 0);
                if (end == part.c_str() || *end != '\0' || errno == ERANGE) return false;
                steps.push_back((uint64_t)v);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (key == "enabled") {
            char* end = nullptr;
            long v = std::strtol(val.c_str(), &end, 10);
            if (end == val.c_str() || *end != '\0') return false;
            ent.enabled = (v != 0);
        }
        // claves desconocidas se ignoran (formato extensible)
    }
    if (!have_type || !have_root) return false; // minimos obligatorios

    PointerChainRef ref;
    ref.value_type = ent.type;
    if (have_module) {
        ref.root.kind = PointerBaseKind::MODULE;
        ref.root.module = module;
        ref.root.offset = root;
    } else {
        ref.root.kind = PointerBaseKind::ABSOLUTE;
        ref.root.address = root;
        ent.address = root;
    }
    ref.offsets = std::move(steps);
    ent.ptr = ref;
    ent.description = unquote_desc(desc_raw);
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
        std::string first;
        if (!(iss >> first)) continue;

        if (first == "pointer") {
            // v2: cadena de punteros dinamica.
            AddressEntry ent;
            if (parse_pointer_line(iss, ent)) loaded.push_back(std::move(ent));
            continue;
        }

        // v1: linea absoluta (primera palabra = direccion).
        std::string type_s, en_s;
        if (!(iss >> type_s >> en_s)) continue;

        errno = 0;
        char* end = nullptr;
        unsigned long long addr = std::strtoull(first.c_str(), &end, 0);
        if (end == first.c_str() || *end != '\0' || errno == ERANGE) continue;

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
