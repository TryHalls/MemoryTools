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
LOG2=$(mktemp)
OUT=$(mktemp)
FIFO=$(mktemp -u)
rm -f "$FIFO"
mkfifo "$FIFO"

OBJ_PID=""
OBJ2_PID=""
MT_PID=""
cleanup() {
    exec 3>&- 2>/dev/null || true
    [ -n "$MT_PID" ] && kill "$MT_PID" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    [ -n "$OBJ2_PID" ] && kill "$OBJ2_PID" 2>/dev/null || true
    rm -f "$LOG" "$LOG2" "$OUT" "$FIFO"
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

echo
echo "== error paths del motor de memoria (con timeout, sin cuelgues):"
ATT1=$(timeout 5 "$BIN" attach 1 <<< 'quit' 2>&1 || true)
echo "$ATT1" | grep -q 'permiso denegado' \
    && echo "OK: attach a PID 1 rechazado (ptrace_scope=1 / Yama)" \
    || echo "NOTA: este entorno no aplica la restriccion Yama a PID 1 (no es un fallo)"
ATTX=$(timeout 5 "$BIN" attach 99999999 <<< 'quit' 2>&1 || true)
echo "$ATTX" | grep -q 'no existe el proceso' \
    && echo "OK: attach a PID inexistente -> error controlado" \
    || { echo "FALLO: attach a PID inexistente: $(echo "$ATTX" | head -3)"; exit 1; }

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
echo "== STRESS: first unknown + next 20000 (millones de candidatos)"
feed 'first unknown i32' 'count'
wait_out "Escaneo 'unknown'" || fail "no llego el resultado del first unknown"
sleep 1.5
feed 'next 20000 i32' 'count' 'results 10'
sleep 2
UNKBLOCK=$(sed -n "/Escaneo 'unknown'/,\$p" "$OUT")
UNKCOUNT=$(echo "$UNKBLOCK" | grep -m1 "Escaneo 'unknown'" | grep -o '[0-9]*' | tail -1)
echo "unknown: ${UNKCOUNT:-?} posiciones legibles"
[ -n "$UNKCOUNT" ] && [ "$UNKCOUNT" -ge 1000000 ] \
    || fail "unknown scan sospechosamente pequeno (${UNKCOUNT:-?})"
echo "$UNKBLOCK" | grep -m1 'Next Scan'
echo "$UNKBLOCK" | grep -q "$NEXP" \
    || fail "first unknown + next 20000 no conserva la direccion de 'dinero'"

echo
echo "== visor hexadecimal de $EXPECTED"
feed "view $EXPECTED 32"
wait_out 'Memoria en' || fail "no llego el visor hexadecimal"
sleep 0.3
tail -n 8 "$OUT"

echo
echo "== PATTERN SCAN: bytes exactos de la cadena 'hola memorytool'"
MSJ=$(grep -m1 'mensaje' "$LOG" | grep -o '0x[0-9a-f]*' | sed -E 's/^0x0*//' || true)
[ -n "$MSJ" ] || fail "no se pudo leer la direccion de 'mensaje' del log"
feed 'pattern 68 6f 6c 61 20 6d 65 6d 6f 72 79 74 6f 6f 6c'
wait_out 'Pattern Scan' || fail "no llego el resultado del Pattern Scan"
sleep 0.5
tail -n 30 "$OUT" | grep -m1 'Pattern Scan'
tail -n 30 "$OUT" | grep -q "$MSJ" || fail "el Pattern Scan exacto no encuentra 'mensaje' (esperada $MSJ)"

echo
echo "== PATTERN SCAN con wildcards: 20 4e ?? ?? (primeros bytes de 'dinero' = 20000 LE)"
feed 'pattern 20 4e ?? ??'
wait_out 'con wildcards' || fail "no llego el resultado del Pattern Scan con wildcards"
sleep 0.5
BLOCK=$(sed -n '/con wildcards/,$p' "$OUT")
echo "$BLOCK" | grep -m1 'Pattern Scan'
echo "$BLOCK" | grep -q "$NEXP" || fail "el Pattern Scan con wildcards no encuentra 'dinero' (esperada $NEXP)"

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

echo
echo "== CRIT-1: limites de candidatos (aviso a 10M, truncado a 20M)"
( sleep 20 ) | "$OBJ" 18 > "$LOG2" 2>&1 &
OBJ2_PID=$!
PID2=""
for _ in $(seq 1 50); do
    PID2=$(grep -m1 -o 'PID: [0-9]*' "$LOG2" | grep -o '[0-9]*' || true)
    [ -n "$PID2" ] && break
    sleep 0.2
done
[ -n "$PID2" ] || fail "objetivo grande no arranco"
echo "objetivo grande PID=$PID2 (18 MiB extra)"
OUT2=$(printf 'first unknown i32\ncount\nquit\n' | timeout 60 "$BIN" "$PID2" 2>&1 || true)
echo "$OUT2" | grep -q 'truncado' \
    && echo "OK: escaneo truncado al limite de candidatos" \
    || { echo "FALLO: el escaneo no se trunco al limite (¿kMaxCandidates > 20M?)"; echo "$OUT2" | grep -iE 'unknown|AVISO' || true; exit 1; }
echo "$OUT2" | grep -q 'muy grande' \
    && echo "OK: aviso de candidatos elevados (>= 10M)" \
    || { echo "FALLO: no aparecio el aviso de candidatos elevados"; echo "$OUT2" | grep -iE 'unknown|AVISO' || true; exit 1; }
C2=$(echo "$OUT2" | grep -m1 "Escaneo 'unknown'" | grep -o '[0-9]*' | tail -1)
echo "candidatos retenidos: ${C2:-?}"
[ "$C2" = "20000000" ] || { echo "FALLO: limite de candidatos distinto de 20000000 (obtenido ${C2:-?})"; exit 1; }
echo "OK: el limite es exactamente 20M"
kill "$OBJ2_PID" 2>/dev/null || true
OBJ2_PID=""

feed 'quit'
sleep 0.5
echo
echo "== PRUEBA E2E SUPERADA =="
