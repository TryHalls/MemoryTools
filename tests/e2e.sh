#!/usr/bin/env bash
# Prueba de extremo a extremo de MemoryTool.
#
# 1. Lanza 'objetivo' (dinero = 12345) con comandos programados que lo
#    cambian a 15000 y luego a 20000.
# 2. Abre UNA sesion interactiva del escaner (por FIFO) y ejecuta:
#      first 12345  ->  next 15000  ->  next 20000
#    (el estado de First/Next Scan vive en la misma sesion).
# 3. Comprueba que la direccion de 'dinero' (via &dinero, impresa por el
#    propio programa) aparece y sobrevive a los refinamientos.
# 4. Inspecciona con el visor hexadecimal y escribe 99999 en esa direccion,
#    verificando que el proceso lo refleja.
set -euo pipefail
cd "$(dirname "$0")/.."

BIN=build/memorytool
OBJ=build/objetivo
if [ ! -x "$BIN" ] || [ ! -x "$OBJ" ]; then
    echo "Compila primero:  ./build.sh   (o: cmake -S . -B build && cmake --build build)"
    exit 1
fi

LOG=$(mktemp)
OUT=$(mktemp)
FIFO=$(mktemp -u)
rm -f "$FIFO"
mkfifo "$FIFO"

OBJ_PID=""
MT_PID=""
cleanup() {
    exec 3>&- 2>/dev/null || true
    [ -n "$MT_PID" ] && kill "$MT_PID" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    rm -f "$LOG" "$OUT" "$FIFO"
}
trap cleanup EXIT

fail() {
    echo "FALLO: $1"
    echo "--- salida de memorytool ---"
    cat "$OUT"
    exit 1
}

# --- 'objetivo' con cambios programados: a los 3s -> 15000, a los 7s -> 20000
( sleep 3; echo 15000; sleep 4; echo 20000; sleep 60 ) | "$OBJ" > "$LOG" 2>&1 &
OBJ_PID=$!

PID=""
for _ in $(seq 1 50); do
    PID=$(grep -m1 -o 'PID: [0-9]*' "$LOG" | grep -o '[0-9]*' || true)
    [ -n "$PID" ] && break
    sleep 0.2
done
[ -n "$PID" ] || fail "'objetivo' no arranco"
echo "== objetivo PID=$PID"

# --- UNA sesion del escaner, alimentada por FIFO ----------------------------
"$BIN" "$PID" < "$FIFO" > "$OUT" 2>&1 &
MT_PID=$!
exec 3>"$FIFO"

feed() { printf '%s\n' "$@" >&3; }
wait_out() { # wait_out <patron> [intentos]
    local pat="$1" tries="${2:-30}"
    for _ in $(seq 1 "$tries"); do
        grep -q "$pat" "$OUT" && return 0
        sleep 0.3
    done
    return 1
}
wait_log() {
    local pat="$1"
    for _ in $(seq 1 40); do grep -q "$pat" "$LOG" && return 0; sleep 0.3; done
    return 1
}

wait_out 'mt(' || fail "memorytool no arranco"

echo
echo "== proceso en la lista:"
"$BIN" list | grep -E 'objetivo|PID' | head -3

EXPECTED=$(grep -m1 'dinero' "$LOG" | grep -o '0x[0-9a-f]*' || true)
[ -n "$EXPECTED" ] || fail "no se pudo leer la direccion de 'dinero' del log"
# Normalizada sin ceros a la izquierda (el escaner imprime 16 digitos)
NEXP=$(printf '%s' "$EXPECTED" | sed -E 's/^0x0*//')
echo
echo "== direccion esperada de 'dinero' (impresa por el propio programa): $EXPECTED"

echo
echo "== FIRST SCAN (12345 int32)"
feed 'first 12345 i32' 'count' 'results 20'
wait_out '\[ *0\] 0x' || fail "no llego la lista de resultados del First Scan"
grep -m1 'First Scan' "$OUT"
grep -q "$NEXP" "$OUT" || fail "la direccion de 'dinero' no aparece en el First Scan"

echo
echo "== esperando que 'objetivo' cambie dinero a 15000..."
wait_log 'Dinero: 15000' || fail "'objetivo' no cambio a 15000"

echo
echo "== NEXT SCAN (15000 int32)"
feed 'next 15000 i32' 'count' 'results 20'
wait_out 'Next Scan' || fail "no hubo respuesta del Next Scan"
sleep 0.5
tail -n 25 "$OUT" | grep -m1 'Next Scan'
tail -n 25 "$OUT" | grep -q "$NEXP" || fail "la direccion de 'dinero' no sobrevive al Next Scan (15000)"

echo
echo "== esperando que 'objetivo' cambie dinero a 20000..."
wait_log 'Dinero: 20000' || fail "'objetivo' no cambio a 20000"

echo
echo "== NEXT SCAN (20000 int32)"
feed 'next 20000 i32' 'count' 'results 20'
wait_out 'Next Scan' || fail "no hubo respuesta del Next Scan"
sleep 0.5
tail -n 25 "$OUT" | grep -m1 'Next Scan'
tail -n 25 "$OUT" | grep -q "$NEXP" || fail "la direccion de 'dinero' no sobrevive al Next Scan (20000)"

echo
echo "== visor hexadecimal de $EXPECTED"
feed "view $EXPECTED 32"
wait_out 'Memoria en' || fail "no llego el visor hexadecimal"
sleep 0.3
tail -n 8 "$OUT"

echo
echo "== escritura: set $EXPECTED 99999 int32"
feed "set $EXPECTED 99999 i32"
wait_out 'Nuevo' || fail "no se pudo escribir el nuevo valor"
sleep 0.3
tail -n 6 "$OUT"

echo
echo "== comprobando que 'objetivo' ahora muestra 99999..."
wait_log 'Dinero: 99999' || fail "el proceso no refleja el nuevo valor"
grep -m1 'Dinero: 99999' "$LOG"

feed 'quit'
sleep 0.5
echo
echo "== PRUEBA E2E SUPERADA =="
