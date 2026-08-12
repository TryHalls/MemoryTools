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
TF=""
PT_PID=""
PT=""
cleanup() {
    exec 3>&- 2>/dev/null || true
    [ -n "$MT_PID" ] && kill "$MT_PID" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    [ -n "$OBJ2_PID" ] && kill "$OBJ2_PID" 2>/dev/null || true
    [ -n "$PT_PID" ] && kill "$PT_PID" 2>/dev/null || true
    [ -n "$TF" ] && rm -f "$TF"
    [ -n "$PT" ] && rm -f "$PT"
    rm -f "$LOG" "$LOG2" "$OUT" "$FIFO"
}
trap cleanup EXIT

fail() {
    echo "FALLO: $1"
    echo "--- salida de memorytool ---"
    cat "$OUT"
    exit 1
}

# contains <nombre-de-variable> <subcadena>: comprueba si la variable contiene
# la subcadena SIN usar pipes ni grep -q. Con `set -o pipefail`, un
# `echo "$VAR" | grep -q "$PAT"` puede fallar en falso: cuando grep -q
# encuentra la coincidencia sale antes de tiempo y el escritor del pipe puede
# recibir SIGPIPE, lo que hace que el pipeline devuelva 141 en lugar de 0.
# (Observado de forma intermitente en esta misma prueba con buffers grandes.)
contains() {
    local varname="$1"
    local s="${!varname}"
    case "$s" in
        *"$2"*) return 0 ;;
        *) return 1 ;;
    esac
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
"$BIN" list | grep -E 'objetivo|PID' | head -3 || true

echo
echo "== error paths del motor de memoria (con timeout, sin cuelgues):"
ATT1=$(timeout 5 "$BIN" attach 1 <<< 'quit' 2>&1 || true)
if contains ATT1 'permiso denegado'; then
    echo "OK: attach a PID 1 rechazado (ptrace_scope=1 / Yama)"
else
    echo "NOTA: este entorno no aplica la restriccion Yama a PID 1 (no es un fallo)"
fi
ATTX=$(timeout 5 "$BIN" attach 99999999 <<< 'quit' 2>&1 || true)
if contains ATTX 'no existe el proceso'; then
    echo "OK: attach a PID inexistente -> error controlado"
else
    echo "FALLO: attach a PID inexistente: $(echo "$ATTX" | head -3)"
    exit 1
fi

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
tail -n 25 "$OUT" | grep -m1 'Next Scan' || true
BLOCK25=$(tail -n 25 "$OUT")
contains BLOCK25 "$NEXP" || fail "la direccion de 'dinero' no sobrevive al Next Scan (15000)"

echo
echo "== esperando que 'objetivo' cambie dinero a 20000..."
wait_log 'Dinero: 20000' || fail "'objetivo' no cambio a 20000"

echo
echo "== NEXT SCAN (20000 int32)"
feed 'next 20000 i32' 'count' 'results 20'
wait_out 'Next Scan' || fail "no hubo respuesta del Next Scan"
sleep 0.5
tail -n 25 "$OUT" | grep -m1 'Next Scan' || true
BLOCK25=$(tail -n 25 "$OUT")
contains BLOCK25 "$NEXP" || fail "la direccion de 'dinero' no sobrevive al Next Scan (20000)"

echo
echo "== STRESS: first unknown + next 20000 (millones de candidatos)"
feed 'first unknown i32' 'count'
wait_out "Escaneo 'unknown'" || fail "no llego el resultado del first unknown"
sleep 1.5
feed 'next 20000 i32' 'count' 'results 10'
sleep 2
UNKBLOCK=$(sed -n "/Escaneo 'unknown'/,\$p" "$OUT")
UNKCOUNT=$(echo "$UNKBLOCK" | grep -m1 "Escaneo 'unknown'" | grep -o '[0-9]*' | tail -1 || true)
echo "unknown: ${UNKCOUNT:-?} posiciones legibles"
[ -n "$UNKCOUNT" ] && [ "$UNKCOUNT" -ge 1000000 ] \
    || fail "unknown scan sospechosamente pequeno (${UNKCOUNT:-?})"
echo "$UNKBLOCK" | grep -m1 'Next Scan' || true
contains UNKBLOCK "$NEXP" \
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
tail -n 30 "$OUT" | grep -m1 'Pattern Scan' || true
BLOCK30=$(tail -n 30 "$OUT")
contains BLOCK30 "$MSJ" || fail "el Pattern Scan exacto no encuentra 'mensaje' (esperada $MSJ)"

echo
echo "== PATTERN SCAN con wildcards: 20 4e ?? ?? (primeros bytes de 'dinero' = 20000 LE)"
feed 'pattern 20 4e ?? ??'
wait_out 'con wildcards' || fail "no llego el resultado del Pattern Scan con wildcards"
sleep 0.5
BLOCK=$(sed -n '/con wildcards/,$p' "$OUT")
echo "$BLOCK" | grep -m1 'Pattern Scan' || true
contains BLOCK "$NEXP" || fail "el Pattern Scan con wildcards no encuentra 'dinero' (esperada $NEXP)"

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
if contains OUT2 'truncado'; then
    echo "OK: escaneo truncado al limite de candidatos"
else
    echo "FALLO: el escaneo no se trunco al limite (¿kMaxCandidates > 20M?)"
    echo "$OUT2" | grep -iE 'unknown|AVISO' || true
    exit 1
fi
if contains OUT2 'muy grande'; then
    echo "OK: aviso de candidatos elevados (>= 10M)"
else
    echo "FALLO: no aparecio el aviso de candidatos elevados"
    echo "$OUT2" | grep -iE 'unknown|AVISO' || true
    exit 1
fi
C2=$(echo "$OUT2" | grep -m1 "Escaneo 'unknown'" | grep -o '[0-9]*' | tail -1 || true)
echo "candidatos retenidos: ${C2:-?}"
[ "$C2" = "20000000" ] || { echo "FALLO: limite de candidatos distinto de 20000000 (obtenido ${C2:-?})"; exit 1; }
echo "OK: el limite es exactamente 20M"
kill "$OBJ2_PID" 2>/dev/null || true
OBJ2_PID=""

echo
echo "== filtro CHANGED (candidatos previos + set + next changed)"
BASE=$(wc -l < "$OUT")
feed "set $EXPECTED 30000 i32"
for _ in $(seq 1 30); do
    sed -n "$((BASE + 1)),\$p" "$OUT" | grep -q 'Nuevo' && break
    sleep 0.3
done
sleep 0.5
feed 'next changed' 'count' 'results 10'
sleep 2
TAIL=$(sed -n "$((BASE + 1)),\$p" "$OUT")
echo "$TAIL" | grep -m1 'Next Scan' || true
# Las filas de resultados llevan el prompt por delante ("mt(pid)> [ n] 0x..."),
# asi que se buscan como subcadena, no ancladas al inicio de linea.
RES=$(echo "$TAIL" | grep '\[ *0\] 0x' || true)
contains RES "$NEXP" || fail "next changed no conserva 'dinero'"
contains RES '= 30000' || fail "next changed no refleja el valor 30000"
echo "OK: next changed encuentra 'dinero' (20000 -> 30000)"

echo
echo "== ADDRESS TABLE: add-result, read, set, save/clear/load, toggle =="
TF=$(mktemp)
feed 'table add-result 0 "dinero"'
wait_out 'Entrada 0 anadida desde results' || fail "table add-result no respondio"
feed 'table'
wait_out 'dinero' || fail "la tabla no muestra la entrada"
feed 'table read 0'
wait_out '\[0\] 0x' || fail "table read no respondio"
sleep 0.3
BLOCKT=$(tail -n 5 "$OUT")
contains BLOCKT '= 30000' || fail "table read no muestra el valor 30000"
feed 'table set 0 424242'
wait_out 'Nuevo' || fail "table set no respondio"
wait_log 'Dinero: 424242' || fail "el proceso no refleja el valor escrito desde la tabla"
feed "table save $TF"
wait_out 'guardada' || fail "table save no respondio"
grep -q 'int32 1 "dinero"' "$TF" || { echo "FALLO: archivo de tabla incorrecto:"; cat "$TF"; exit 1; }
feed 'table clear'
wait_out 'vaciada' || fail "table clear no respondio"
feed 'table'
wait_out 'esta vacia' || fail "la tabla no quedo vacia"
feed "table load $TF"
wait_out 'cargada' || fail "table load no respondio"
feed 'table'
wait_out 'dinero' || fail "la entrada no se restauro tras load"
feed 'table read 0'
wait_out '\[0\] 0x' || fail "table read (restaurada) no respondio"
sleep 0.3
BLOCKT2=$(tail -n 5 "$OUT")
contains BLOCKT2 '= 424242' || fail "la entrada restaurada no se lee (424242)"
feed 'table toggle 0'
wait_out 'desactivada' || fail "table toggle no respondio"
feed 'table read 0'
wait_out 'desactivada' || fail "table read no respeta 'disabled'"
feed 'table toggle 0'
wait_out 'activada' || fail "table toggle (re) no respondio"
rm -f "$TF"
TF=""
echo "OK: Address Table completa (add-result/read/set/save/clear/load/toggle)"

echo
echo "== POINTER SCANNER (core: pointer_test + pointer_driver) =="
PTBIN=build/pointer_test
PDRV=build/pointer_driver
if [ ! -x "$PTBIN" ] || [ ! -x "$PDRV" ]; then
    echo "FALLO: faltan build/pointer_test o build/pointer_driver (ejecuta ./build.sh)"
    exit 1
fi
PT=$(mktemp)
( sleep 90 ) | "$PTBIN" > "$PT" 2>&1 &
PT_PID=$!
PTPID=""
for _ in $(seq 1 50); do
    PTPID=$(grep -m1 -o 'PID: [0-9]*' "$PT" | grep -o '[0-9]*' || true)
    [ -n "$PTPID" ] && break
    sleep 0.2
done
[ -n "$PTPID" ] || fail "pointer_test no arranco"
echo "pointer_test PID=$PTPID"
PTARGET=$(grep -m1 '^TARGET:' "$PT" | grep -o '0x[0-9a-f]*' || true)
NODE1=$(grep -m1 '^NODE1:' "$PT" | grep -o '0x[0-9a-f]*' || true)
NODE2=$(grep -m1 '^NODE2:' "$PT" | grep -o '0x[0-9a-f]*' || true)
NODE3=$(grep -m1 '^NODE3:' "$PT" | grep -o '0x[0-9a-f]*' || true)
CYCLEA=$(grep -m1 '^CYCLEA:' "$PT" | grep -o '0x[0-9a-f]*' || true)
CYCLEB=$(grep -m1 '^CYCLEB:' "$PT" | grep -o '0x[0-9a-f]*' || true)
[ -n "$PTARGET" ] && [ -n "$NODE1" ] && [ -n "$NODE2" ] && [ -n "$NODE3" ] \
    || fail "faltan direcciones en la salida de pointer_test"
echo "TARGET=$PTARGET"
echo "NODE1=$NODE1 NODE2=$NODE2 NODE3=$NODE3 CYCLEA=$CYCLEA CYCLEB=$CYCLEB"

echo
echo "== pointer scan depth=1: espera Node1 -> TARGET =="
D1=$(timeout 30 "$PDRV" "$PTPID" "$PTARGET" 1 2>&1 || true)
contains D1 "$NODE1 $PTARGET" || { echo "$D1"; fail "depth=1 no encuentra Node1->TARGET"; }
echo "OK: depth=1 encuentra Node1->TARGET"

echo
echo "== pointer scan depth=2: espera Node2 -> Node1 -> TARGET =="
D2=$(timeout 30 "$PDRV" "$PTPID" "$PTARGET" 2 2>&1 || true)
contains D2 "$NODE2 $NODE1 $PTARGET" \
    || { echo "$D2"; fail "depth=2 no reconstruye Node2->Node1->TARGET"; }
echo "OK: depth=2 reconstruye Node2->Node1->TARGET"

echo
echo "== pointer scan depth=3: espera Node3 -> Node2 -> Node1 -> TARGET =="
D3=$(timeout 30 "$PDRV" "$PTPID" "$PTARGET" 3 2>&1 || true)
contains D3 "$NODE3 $NODE2 $NODE1 $PTARGET" \
    || { echo "$D3"; fail "depth=3 no reconstruye la cadena completa"; }
echo "OK: depth=3 reconstruye Node3->Node2->Node1->TARGET"

echo
echo "== ciclo controlado (TARGET=CycleB): sin cuelgues y sin cadenas ciclicas =="
DC=$(timeout 30 "$PDRV" "$PTPID" "$CYCLEB" 3 2>&1 || true)
contains DC "$CYCLEA $CYCLEB" \
    || { echo "$DC"; fail "el ciclo no se detecto (esperaba CycleA->CycleB)"; }
# El ciclo B->A->B se descarta por el control por cadena: ninguna cadena debe
# EMPEZAR por CycleB (si empezara, seria la cadena ciclica B->A->B). Cadenas
# aciclicas que terminan en CycleB (p. ej. g_cycle_a -> CycleA -> CycleB) si
# pueden existir y son correctas.
if contains DC "chain d=1: $CYCLEB" || contains DC "chain d=2: $CYCLEB" \
   || contains DC "chain d=3: $CYCLEB"; then
    echo "FALLO: aparecio una cadena ciclica (empieza por CycleB):"
    echo "$DC"
    exit 1
fi
echo "OK: ciclo controlado (termina sin colgarse; B->A->B descartada)"

echo
echo "== truncado por max_edges_per_level =="
DT=$(timeout 30 "$PDRV" "$PTPID" "$PTARGET" 3 1 2>&1 || true)
contains DT 'edges_truncated=1' \
    || { echo "$DT"; fail "max_edges=1 no marco edges_truncated"; }
contains DT "$NODE1 $PTARGET" \
    || { echo "$DT"; fail "el nivel truncado perdio los resultados ya obtenidos"; }
echo "OK: nivel truncado por limite de aristas conservando resultados"

kill "$PT_PID" 2>/dev/null || true
PT_PID=""
rm -f "$PT"
PT=""

feed 'quit'
sleep 0.5
echo
echo "== PRUEBA E2E SUPERADA =="
