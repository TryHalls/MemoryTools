#!/usr/bin/env bash
# E2E de la API JSON completa (FASE W-4): arranca `memorytool --web --port 0`
# y ejercita con curl el flujo completo contra `objetivo` (attach/first/next/
# results/memory/write/table) y `pointer_offset_test` (pointer scan/results/
# add/resolve + supervivencia al ASLR), incluyendo cancelacion de un job en
# curso y detach. Usa python3 solo para parsear JSON en las comprobaciones.
# Uso: bash tests/web_api_e2e.sh   (0 = OK, !=0 = fallo)
set -euo pipefail
cd "$(dirname "$0")/.."

command -v curl >/dev/null 2>&1 || { echo "FALLO: curl no disponible"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "FALLO: python3 no disponible"; exit 1; }

for b in build/memorytool build/objetivo build/pointer_offset_test; do
    [ -x "$b" ] || { echo "FALLO: falta $b (ejecuta ./build.sh)"; exit 1; }
done

LOG=$(mktemp /tmp/mt_webapi_XXXXXX)
SRV=""
OBJ_PID=""
PT_PID=""
PT_PID2=""
BIG_PID=""
fails=0
cleanup() {
    [ -n "$SRV" ] && kill "$SRV" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    [ -n "$PT_PID" ] && kill "$PT_PID" 2>/dev/null || true
    [ -n "$PT_PID2" ] && kill "$PT_PID2" 2>/dev/null || true
    [ -n "$BIG_PID" ] && kill "$BIG_PID" 2>/dev/null || true
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null || true
    rm -f "$LOG" "$LOG.obj" "$LOG.pt" "$LOG.pt2" "$LOG.big"
    rm -f tables/apitabla
    rmdir tables 2>/dev/null || true
}
trap cleanup EXIT

check() { # check <nombre> <condicion...>
    local name="$1"; shift
    if "$@"; then
        echo "ok: $name"
    else
        echo "FALLO: $name"
        fails=$((fails + 1))
    fi
}

# --- arrancar el servidor ----------------------------------------------------
./build/memorytool --web --port 0 >"$LOG" 2>&1 &
SRV=$!
URL=""; TOKEN=""
for _ in $(seq 1 100); do
    URL=$(grep -m1 '^URL:' "$LOG" | awk '{print $2}' || true)
    TOKEN=$(grep -m1 '^Token:' "$LOG" | awk '{print $2}' || true)
    [ -n "$URL" ] && [ -n "$TOKEN" ] && break
    sleep 0.1
done
if [ -z "$URL" ] || [ -z "$TOKEN" ]; then
    echo "FALLO: no se obtuvo URL/Token del servidor"
    cat "$LOG"
    exit 1
fi
echo "== servidor en $URL (token ${TOKEN:0:8}...) =="

H=(-H "X-MemoryTool-Token: $TOKEN")
# api <metodo> <ruta> [json]  -> deja $J (cuerpo) y $code (HTTP) en el shell
# actual (sin subshell: con command substitution se perderian).
api() {
    local raw
    if [ $# -ge 3 ]; then
        raw=$(curl -s -w '\n%{http_code}' -X "$1" "${H[@]}" \
            -H 'Content-Type: application/json' --data "$3" "$URL$2")
    else
        raw=$(curl -s -w '\n%{http_code}' -X "$1" "${H[@]}" "$URL$2")
    fi
    J=$(printf '%s' "$raw" | sed '$d')
    code=$(printf '%s' "$raw" | tail -1)
}
# json_get <expr-python>   (lee el JSON por stdin)
json_get() {
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))' "$1"
}
# wait_job <id> -> imprime el estado final (completed/cancelled/failed/timeout:..)
wait_job() {
    local id="$1" st="" tries=300
    for _ in $(seq 1 "$tries"); do
        api GET "/api/jobs/$id"
        st=$(printf '%s' "$J" | json_get 'd["state"]')
        case "$st" in
            completed|cancelled|failed) echo "$st"; return ;;
        esac
        sleep 0.1
    done
    echo "timeout:$st"
}

# =============================================================================
echo
echo "== 1-2) status + processes =="
api GET /api/status
check "status 200" test "$code" = "200"
check "status ok:true" grep -q '"ok":true' <<<"$J"
check "status attached:false" grep -q '"attached":false' <<<"$J"
api GET /api/processes
check "processes 200" test "$code" = "200"
check "processes array" grep -q '"processes":\[' <<<"$J"

# =============================================================================
echo
echo "== 3-6) attach objetivo + first scan + poll + results =="
# objetivo sin cambios temporizados: dinero se queda en 12345 (determinista).
( sleep 120 ) | build/objetivo > "$LOG.obj" 2>&1 &
OBJ_PID=$!
PID=""
for _ in $(seq 1 50); do
    PID=$(grep -m1 -o 'PID: [0-9]*' "$LOG.obj" | grep -o '[0-9]*' || true)
    [ -n "$PID" ] && break
    sleep 0.2
done
[ -n "$PID" ] || { echo "FALLO: objetivo no arranco"; exit 1; }
echo "objetivo PID=$PID"

api POST /api/attach "{\"pid\":\"$PID\"}"
check "attach 200" test "$code" = "200"
check "attach attached:true" grep -q '"attached":true' <<<"$J"

api POST /api/scan/first '{"type":"int32","value":"12345"}'
check "first scan 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
[ -n "$JID" ] || { echo "FALLO: no hay job_id en la respuesta"; echo "$J"; exit 1; }
check "first job queued" grep -q '"state":"queued"' <<<"$J"
ST=$(wait_job "$JID")
check "first job completed" test "$ST" = "completed"

api GET "/api/results?offset=0&limit=100"
check "results 200" test "$code" = "200"
TOTAL=$(printf '%s' "$J" | json_get 'd["total"]')
check "results total>=1" test "$TOTAL" -ge 1
NEXP=$(grep -m1 'dinero' "$LOG.obj" | grep -o '0x[0-9a-f]*' | sed -E 's/^0x0*//')
[ -n "$NEXP" ] || { echo "FALLO: no se pudo leer la direccion de dinero"; exit 1; }
grep -q "\"address\":\"0x0*$NEXP\"" <<<"$J" \
    || { echo "FALLO: la direccion de dinero ($NEXP) no esta en results:"; echo "$J"; exit 1; }
echo "OK: first scan encontro la direccion de dinero ($NEXP)"

# =============================================================================
echo
echo "== 7) next scan (unchanged: el valor no ha cambiado) =="
api POST /api/scan/next '{"filter":"unchanged"}'
check "next 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "next job completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NEXP\"" <<<"$J" \
    || { echo "FALLO: la direccion de dinero no sobrevive al next:"; echo "$J"; exit 1; }
echo "OK: next scan conserva la direccion de dinero"

# =============================================================================
echo
echo "== 8) memory viewer =="
FULL=$(printf '0x%016x' "$((0x$NEXP))")
api GET "/api/memory?address=$FULL&length=32"
check "memory 200" test "$code" = "200"
check "memory hex" grep -q '"hex":"' <<<"$J"
check "memory ascii" grep -q '"ascii":"' <<<"$J"
check "memory region" grep -q '"region":' <<<"$J"

# =============================================================================
echo
echo "== 9) write =="
api POST /api/write "{\"address\":\"$FULL\",\"type\":\"int32\",\"value\":\"99999\"}"
check "write 200" test "$code" = "200"
check "write verified" grep -q '"verified":true' <<<"$J"
VOK=0
for _ in $(seq 1 40); do
    grep -q 'Dinero: 99999' "$LOG.obj" && { VOK=1; break; }
    sleep 0.2
done
[ "$VOK" = 1 ] || { echo "FALLO: el proceso no refleja el valor escrito 99999"; exit 1; }
echo "OK: write verificado en el proceso (Dinero: 99999)"

# =============================================================================
echo
echo "== 10) address table: add/read/set/save/clear/load/remove =="
api POST /api/table/add "{\"address\":\"$FULL\",\"type\":\"int32\",\"description\":\"dinero\"}"
check "table add 200" test "$code" = "200"
TI=$(printf '%s' "$J" | json_get 'd["index"]')
check "table add index 0" test "$TI" = "0"
api POST /api/table/read '{"index":0}'
check "table read 200" test "$code" = "200"
check "table read value 99999" grep -q '"value":"99999"' <<<"$J"
api POST /api/table/set '{"index":0,"value":"424242"}'
check "table set 200" test "$code" = "200"
check "table set verified" grep -q '"verified":true' <<<"$J"
VOK=0
for _ in $(seq 1 40); do
    grep -q 'Dinero: 424242' "$LOG.obj" && { VOK=1; break; }
    sleep 0.2
done
[ "$VOK" = 1 ] || { echo "FALLO: el proceso no refleja el valor de la tabla 424242"; exit 1; }
api POST /api/table/save '{"name":"apitabla"}'
check "table save 200" test "$code" = "200"
api POST /api/table/clear
check "table clear 200" test "$code" = "200"
api POST /api/table/load '{"name":"apitabla"}'
check "table load 200" test "$code" = "200"
CNT=$(printf '%s' "$J" | json_get 'd["count"]')
check "table load count 1" test "$CNT" = "1"
api POST /api/table/remove '{"index":0}'
check "table remove 200" test "$code" = "200"
api GET /api/table
check "table vacia" grep -q '"count":0' <<<"$J"
echo "OK: address table completa (add/read/set/save/clear/load/remove)"

# =============================================================================
echo
echo "== 11-13) pointer scan/results/add/resolve sobre pointer_offset_test =="
( sleep 120 ) | build/pointer_offset_test > "$LOG.pt" 2>&1 &
PT_PID=$!
P1PID=""
for _ in $(seq 1 50); do
    P1PID=$(grep -m1 -o 'PID: [0-9]*' "$LOG.pt" | grep -o '[0-9]*' || true)
    [ -n "$P1PID" ] && break
    sleep 0.2
done
[ -n "$P1PID" ] || { echo "FALLO: pointer_offset_test no arranco"; exit 1; }
PTGT=$(grep -m1 '^TARGET:' "$LOG.pt" | grep -o '0x[0-9a-f]*')
[ -n "$PTGT" ] || { echo "FALLO: sin TARGET en pointer_offset_test"; exit 1; }
echo "pointer test PID=$P1PID TARGET=$PTGT"

api POST /api/attach "{\"pid\":\"$P1PID\"}"
check "attach pointer 200" test "$code" = "200"

api POST /api/pointer/scan "{\"target\":\"$PTGT\",\"depth\":3}"
check "pointer scan 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "pointer job completed" test "$ST" = "completed"

# Buscar la cadena MODULE con offsets +0x20 -> +0x18 (paginas de 200).
IDX=""
for off in 0 200 400 600 800 1000 1200 1400 1600 1800; do
    api GET "/api/pointer/results?offset=$off&limit=200"
    IDX=$(python3 - "$J" <<'EOF'
import json,sys
d=json.loads(sys.argv[1])
for row in d["rows"]:
    if row["kind"] == "MODULE" and \
       row["offsets"] == ["0x0000000000000020", "0x0000000000000018"]:
        print(row["index"]); break
EOF
)
    [ -n "$IDX" ] && break
    TOTAL=$(printf '%s' "$J" | json_get 'd["total"]')
    [ "$TOTAL" -le $((off + 200)) ] && break
done
[ -n "$IDX" ] || { echo "FALLO: no se encontro la cadena MODULE +0x20/+0x18"; echo "$J"; exit 1; }
echo "OK: cadena MODULE +0x20 -> +0x18 encontrada (index $IDX)"

api POST /api/pointer/add "{\"chain_index\":$IDX,\"description\":\"valor\"}"
check "pointer add 200" test "$code" = "200"
check "pointer add kind MODULE" grep -q '"kind":"MODULE"' <<<"$J"
check "pointer add value_type" grep -q '"value_type":"int32"' <<<"$J"

api POST /api/pointer/resolve '{"index":0}'
check "pointer resolve 200" test "$code" = "200"
check "pointer resolve value 4242" grep -q '"value":"4242"' <<<"$J"
RADDR=$(printf '%s' "$J" | json_get 'd["address"]')
echo "OK: pointer resolve -> $RADDR = 4242"

api POST /api/table/read '{"index":0}'
check "table read pointer 200" test "$code" = "200"
grep -q '"value":"4242"' <<<"$J" \
    || { echo "FALLO: table read dinamico no lee 4242:"; echo "$J"; exit 1; }
echo "OK: table read dinamico lee 4242"

# =============================================================================
echo
echo "== ASLR: reiniciar el proceso y resolver la MISMA cadena =="
kill "$PT_PID" 2>/dev/null || true
wait "$PT_PID" 2>/dev/null || true
PT_PID=""
sleep 0.5
( sleep 120 ) | build/pointer_offset_test > "$LOG.pt2" 2>&1 &
PT_PID2=$!
PPID2=""
for _ in $(seq 1 50); do
    PPID2=$(grep -m1 -o 'PID: [0-9]*' "$LOG.pt2" | grep -o '[0-9]*' || true)
    [ -n "$PPID2" ] && break
    sleep 0.2
done
[ -n "$PPID2" ] || { echo "FALLO: pointer_offset_test (2) no arranco"; exit 1; }
api POST /api/attach "{\"pid\":\"$PPID2\"}"
check "attach pid2 200" test "$code" = "200"
api POST /api/table/read '{"index":0}'
check "table read pid2 200" test "$code" = "200"
grep -q '"value":"4242"' <<<"$J" \
    || { echo "FALLO: el valor no sobrevive al reinicio:"; echo "$J"; exit 1; }
RADDR2=$(printf '%s' "$J" | json_get 'd["address"]')
if [ "$RADDR2" != "$RADDR" ]; then
    echo "OK: ASLR - direccion NUEVA ($RADDR -> $RADDR2) con el mismo valor 4242"
else
    echo "NOTA: el layout no cambio entre procesos; resolucion correcta ($RADDR)"
fi
kill "$PT_PID2" 2>/dev/null || true
PT_PID2=""

# =============================================================================
echo
echo "== 14) cancelacion de un job en curso =="
# Objetivo grande (18 MiB extra): el unknown scan tarda lo suficiente.
( sleep 20 ) | build/objetivo 18 > "$LOG.big" 2>&1 &
BIG_PID=$!
BPID=""
for _ in $(seq 1 50); do
    BPID=$(grep -m1 -o 'PID: [0-9]*' "$LOG.big" | grep -o '[0-9]*' || true)
    [ -n "$BPID" ] && break
    sleep 0.2
done
[ -n "$BPID" ] || { echo "FALLO: objetivo grande no arranco"; exit 1; }
api POST /api/attach "{\"pid\":\"$BPID\"}"
check "attach big 200" test "$code" = "200"
api POST /api/scan/first '{"type":"int32","value":null}'
check "unknown scan 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
api POST "/api/jobs/$JID/cancel"
check "cancel 200" test "$code" = "200"
check "cancel flag" grep -q '"cancelled":true' <<<"$J"
ST=$(wait_job "$JID")
check "job final cancelled" test "$ST" = "cancelled"
echo "OK: el job cancelado termino en CANCELLED"
kill "$BIG_PID" 2>/dev/null || true
BIG_PID=""

# =============================================================================
echo
echo "== 15) detach + errores =="
api POST /api/detach
check "detach 200" test "$code" = "200"
check "detach attached:false" grep -q '"attached":false' <<<"$J"

api POST /api/scan/first '{"type":"xyz","value":"5"}'
check "tipo invalido 400" test "$code" = "400"
api POST /api/attach '{"pid":"999999999999999999999999"}'
check "pid gigante 400" test "$code" = "400"
api POST /api/pointer/scan '{"target":"0x1000","max_offset":"0x10000","offset_step":1}'
check "offsets peligrosos 400" test "$code" = "400"
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-MemoryTool-Token: malo" "$URL/api/status")
check "token malo 401" test "$code" = "401"

# --- terminacion limpia ------------------------------------------------------
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true
SRV=""
sleep 0.3
if kill -0 "$SRV" 2>/dev/null; then
    echo "FALLO: el servidor sigue vivo tras la terminacion"
    fails=$((fails + 1))
fi

# limpiar procesos restantes
kill "$OBJ_PID" 2>/dev/null || true
OBJ_PID=""
rm -f "$LOG" "$LOG.obj" "$LOG.pt" "$LOG.pt2" "$LOG.big" tables/apitabla
rmdir tables 2>/dev/null || true

if [ "$fails" = 0 ]; then
    echo
    echo "== PRUEBA WEB API E2E SUPERADA =="
    exit 0
fi
echo
echo "== PRUEBA WEB API E2E CON FALLOS ($fails) =="
exit 1
