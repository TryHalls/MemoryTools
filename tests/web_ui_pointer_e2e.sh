#!/usr/bin/env bash
# E2E de la Web UI - pestana Pointers (FASE W-5D).
#
# Sin navegador automatizable: arranca `memorytool --web --port 0`, comprueba
# que se sirven los assets con la pestana Pointers y ejercita con curl el
# flujo real contra `pointer_offset_test`:
#   attach -> pointer scan (depth 1 y 3) -> results (MODULE +0x20/+0x18)
#   -> add a Address Table -> resolve (4242) -> memory read de la direccion
#   -> ASLR (reiniciar proceso, resolver la misma cadena: direccion nueva,
#      mismo valor) -> cancel de un pointer scan grande -> detach.
# Uso: bash tests/web_ui_pointer_e2e.sh   (0 = OK, !=0 = fallo)
set -euo pipefail
cd "$(dirname "$0")/.."

command -v curl >/dev/null 2>&1 || { echo "FALLO: curl no disponible"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "FALLO: python3 no disponible"; exit 1; }

for b in build/memorytool build/pointer_offset_test build/objetivo; do
    [ -x "$b" ] || { echo "FALLO: falta $b (ejecuta ./build.sh)"; exit 1; }
done

LOG=$(mktemp /tmp/mt_ui_ptr_XXXXXX)
SRV=""; PT_PID=""; PT_PID2=""; BIG_PID=""
fails=0
cleanup() {
    [ -n "$SRV" ] && kill "$SRV" 2>/dev/null || true
    [ -n "$PT_PID" ] && kill "$PT_PID" 2>/dev/null || true
    [ -n "$PT_PID2" ] && kill "$PT_PID2" 2>/dev/null || true
    [ -n "$BIG_PID" ] && kill "$BIG_PID" 2>/dev/null || true
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null || true
    rm -f "$LOG" "$LOG.pt" "$LOG.pt2" "$LOG.big"
}
trap cleanup EXIT

check() { # check <nombre> <condicion...>
    local name="$1"; shift
    if "$@"; then echo "ok: $name"; else echo "FALLO: $name"; fails=$((fails + 1)); fi
}

# --- arrancar el servidor ----------------------------------------------------
./build/memorytool --web --port 0 >"$LOG" 2>&1 &
SRV=$!
URL=""; TOKEN=""
for _ in $(seq 1 100); do
    URL=$(grep -m1 '^URL:' "$LOG" | awk '{print $2}' || true)
    TOKEN=$(grep -m1 '^Token:' "$LOG" | awk '{print $2}' || true)
    if [ -n "$URL" ] && [ -n "$TOKEN" ]; then break; fi
    sleep 0.1
done
if [ -z "$URL" ] || [ -z "$TOKEN" ]; then
    echo "FALLO: no se obtuvo URL/Token del servidor"; cat "$LOG"; exit 1
fi
echo "== servidor en $URL (token ${TOKEN:0:8}...) =="
H=(-H "X-MemoryTool-Token: $TOKEN")

# api <metodo> <ruta> [json] -> deja $J (cuerpo) y $code (HTTP) en el shell.
api() {
    local raw
    if [ $# -ge 3 ]; then
        raw=$(curl -s --max-time 15 -w '\n%{http_code}' -X "$1" "${H[@]}" \
            -H 'Content-Type: application/json' --data "$3" "$URL$2")
    else
        raw=$(curl -s --max-time 15 -w '\n%{http_code}' -X "$1" "${H[@]}" "$URL$2")
    fi
    J=$(printf '%s' "$raw" | sed '$d')
    code=$(printf '%s' "$raw" | tail -1)
}
json_get() { python3 -c 'import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))' "$1"; }
wait_job() { # wait_job <id> -> estado final
    local id="$1" st=""
    for _ in $(seq 1 300); do
        api GET "/api/jobs/$id"
        st=$(printf '%s' "$J" | json_get 'd["state"]')
        case "$st" in completed|cancelled|failed) echo "$st"; return;; esac
        sleep 0.1
    done
    echo "timeout:$st"
}

# --- 1-3) assets: GET / + Pointer tab -----------------------------------------
idx=$(curl -s --max-time 15 "$URL/")
check "GET / 200" test -n "$idx"
check "index contiene pestana Pointers" grep -q 'data-tab="pointers"' <<<"$idx"
check "index contiene panel pointers" grep -q 'id="panel-pointers"' <<<"$idx"
check "index contiene target" grep -q 'id="ptr-target"' <<<"$idx"
check "index contiene depth" grep -q 'id="ptr-depth"' <<<"$idx"
check "index contiene max_offset" grep -q 'id="ptr-maxoff"' <<<"$idx"
check "index contiene module-only" grep -q 'id="ptr-module"' <<<"$idx"
check "index contiene tabla pointer" grep -q 'id="vt-pointer"' <<<"$idx"

app=$(curl -s --max-time 15 "$URL/app.js")
check "app.js contiene ptrScan" grep -q "async function ptrScan" <<<"$app"
check "app.js contiene ptrRender" grep -q "function ptrRender" <<<"$app"
check "app.js contiene ptrSelect" grep -q "function ptrSelect" <<<"$app"
check "app.js contiene ptrAdd" grep -q "async function ptrAdd" <<<"$app"
check "app.js contiene ptrResolve" grep -q "async function ptrResolve" <<<"$app"
check "app.js contiene ptrVt" grep -q "const ptrVt" <<<"$app"
check "app.js contiene badge MODULE" grep -q "b-module" <<<"$app"

# --- 4) attach pointer_offset_test -------------------------------------------
( sleep 120 ) | build/pointer_offset_test > "$LOG.pt" 2>&1 &
PT_PID=$!
PP=""
for _ in $(seq 1 50); do
    PP=$(grep -m1 -o 'PID: [0-9]*' "$LOG.pt" | grep -o '[0-9]*' || true)
    [ -n "$PP" ] && break
    sleep 0.2
done
[ -n "$PP" ] || { echo "FALLO: pointer_offset_test no arranco"; exit 1; }
PTGT=$(grep -m1 '^TARGET:' "$LOG.pt" | grep -o '0x[0-9a-f]*')
[ -n "$PTGT" ] || { echo "FALLO: sin TARGET en pointer_offset_test"; exit 1; }
echo "pointer test PID=$PP TARGET=$PTGT"
api POST /api/attach "{\"pid\":\"$PP\"}"
check "attach 200" test "$code" = "200"

# --- 5-6) pointer scan depth=1 + job completa --------------------------------
api POST /api/pointer/scan "{\"target\":\"$PTGT\",\"depth\":1}"
check "pointer scan depth1 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "pointer depth1 job completed" test "$ST" = "completed"
api GET "/api/pointer/results?offset=0&limit=50"
check "pointer results 200" test "$code" = "200"
TOTAL=$(printf '%s' "$J" | json_get 'd["total"]')
check "pointer results total>=1" test "$TOTAL" -ge 1
check "pointer results row con index" grep -q '"index":0' <<<"$J"
echo "OK: depth=1 -> $TOTAL cadenas"

# --- 7-8) pointer scan depth=3 + cadena MODULE +0x20/+0x18 -------------------
api POST /api/pointer/scan "{\"target\":\"$PTGT\",\"depth\":3}"
check "pointer scan depth3 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "pointer depth3 job completed" test "$ST" = "completed"

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
[ -n "$IDX" ] || { echo "FALLO: no se encontro la cadena MODULE +0x20/+0x18"; exit 1; }
echo "OK: cadena MODULE +0x20 -> +0x18 (index $IDX)"
api GET "/api/pointer/results?offset=0&limit=50"
check "results fila con module" grep -q '"module":"' <<<"$J"
check "results fila con root_offset" grep -q '"root_offset":"' <<<"$J"
check "results fila con kind MODULE" grep -q '"kind":"MODULE"' <<<"$J"
check "results fila con persistent" grep -q '"persistent":true' <<<"$J"

# --- 9) Add to Address Table ---------------------------------------------------
api POST /api/pointer/add "{\"chain_index\":$IDX,\"description\":\"valor ui\"}"
check "pointer add 200" test "$code" = "200"
check "pointer add kind MODULE" grep -q '"kind":"MODULE"' <<<"$J"
check "pointer add persistent" grep -q '"persistent":true' <<<"$J"
TIDX=$(printf '%s' "$J" | json_get 'd["table_index"]')
echo "OK: cadena anadida a la tabla (table_index $TIDX)"
api GET /api/table
check "tabla tiene entrada pointer" grep -q '"pointer":' <<<"$J"
check "tabla pointer kind MODULE" grep -q '"kind":"MODULE"' <<<"$J"

# --- 10) Resolve ---------------------------------------------------------------
api POST /api/pointer/resolve "{\"index\":$TIDX}"
check "pointer resolve 200" test "$code" = "200"
check "pointer resolve value 4242" grep -q '"value":"4242"' <<<"$J"
RADDR=$(printf '%s' "$J" | json_get 'd["address"]')
echo "OK: pointer resolve -> $RADDR = 4242"

# --- 11) Open in Memory: leer la direccion resuelta ---------------------------
FULL=$(printf '0x%016x' "$((0x${RADDR#0x}))")
api GET "/api/memory?address=$FULL&length=32"
check "memory read 200" test "$code" = "200"
check "memory hex presente" grep -q '"hex":"' <<<"$J"
check "memory ascii presente" grep -q '"ascii":"' <<<"$J"
echo "OK: Memory Viewer lee $FULL (direccion resuelta)"

# --- 12) ASLR: reiniciar el proceso y resolver la MISMA entrada ----------------
# Mata el objetivo y espera a que muera con timeout (un `wait` directo puede
# esperar al subshell `( sleep 120 )` del pipeline y colgarse hasta 120 s).
kill "$PT_PID" 2>/dev/null || true
for _ in $(seq 1 50); do kill -0 "$PT_PID" 2>/dev/null || break; sleep 0.1; done
PT_PID=""
sleep 0.5
( sleep 120 ) | build/pointer_offset_test > "$LOG.pt2" 2>&1 &
PT_PID2=$!
PP2=""
for _ in $(seq 1 50); do
    PP2=$(grep -m1 -o 'PID: [0-9]*' "$LOG.pt2" | grep -o '[0-9]*' || true)
    [ -n "$PP2" ] && break
    sleep 0.2
done
[ -n "$PP2" ] || { echo "FALLO: pointer_offset_test (2) no arranco"; exit 1; }
api POST /api/attach "{\"pid\":\"$PP2\"}"
check "attach pid2 200" test "$code" = "200"
api POST /api/table/read "{\"index\":$TIDX}"
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

# --- 13) cancel de un pointer scan grande --------------------------------------
# Objetivo grande (18 MiB extra) y un scan pesado (depth 7, offsets amplios):
# cancelar -> el job debe terminar CANCELLED.
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
BGTGT=$(grep -m1 'dinero' "$LOG.big" | grep -o '0x[0-9a-f]*' | sed -E 's/^0x0*//')
BGTGT=$(printf '0x%016x' "$((0x${BGTGT:-0}))")
api POST /api/pointer/scan "{\"target\":\"$BGTGT\",\"depth\":7,\"max_offset\":\"0x4000\",\"offset_step\":1}"
check "pointer scan grande 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
api POST "/api/jobs/$JID/cancel"
check "cancel 200" test "$code" = "200"
ST=$(wait_job "$JID")
check "job final cancelled" test "$ST" = "cancelled"
echo "OK: pointer scan cancelado -> CANCELLED"
kill "$BIG_PID" 2>/dev/null || true
BIG_PID=""

# --- 14) detach -----------------------------------------------------------------
api POST /api/detach
check "detach 200" test "$code" = "200"
check "detach attached:false" grep -q '"attached":false' <<<"$J"

# --- terminacion limpia -----------------------------------------------------------
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true
SRV=""
sleep 0.3
if kill -0 "$SRV" 2>/dev/null; then
    echo "FALLO: el servidor sigue vivo tras la terminacion"; fails=$((fails + 1))
fi
rm -f "$LOG" "$LOG.pt" "$LOG.pt2" "$LOG.big"

if [ "$fails" = 0 ]; then
    echo
    echo "== PRUEBA WEB UI POINTER E2E SUPERADA =="
else
    echo
    echo "== PRUEBA WEB UI POINTER E2E CON $fails FALLO(S) =="
    exit 1
fi
