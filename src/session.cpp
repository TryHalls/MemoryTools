// session.cpp - Implementacion de Session.
#include "session.h"

namespace mt {

bool Session::attach(int target, std::string& err) {
    // Comprobar acceso real antes de cambiar el estado (un attach fallido no
    // debe alterar ni el proceso actual ni los resultados del escaner).
    Memory probe;
    if (!probe.open(target, err)) return false;
    probe.close();

    if (pid_ && *pid_ != target) scanner_.clear(); // resultados del PID anterior
    pid_ = target;
    return true;
}

void Session::detach() {
    pid_.reset();
    scanner_.clear();
}

bool Session::with_memory(const std::function<void(Memory&)>& fn, std::string& err) {
    if (!pid_) {
        err = "no hay proceso objetivo";
        return false;
    }
    Memory mem;
    if (!mem.open(*pid_, err)) return false;
    fn(mem);
    mem.close();
    return true;
}

} // namespace mt
