#!/usr/bin/env bash
# E2E de la Web UI - pestañas Memory Viewer y Address Table (FASE W-5B).
#
# Sin navegador automatizable: arranca `memorytool --web --port 0`, comprueba
# que se sirven los assets con las pestañas nuevas, y ejercita con curl el
# flujo real contra `objetivo` (memory read, table add/read/set/toggle/
# save/load/remove) y `pointer_offset_test` (pointer scan/add/resolve).
# Uso: bash tests/web_ui_memory_table_e2e.sh   (0 = OK, !=0 = fallo)
set -euo pipefail
cd "$(dirname "$0")/.."

command -v curl >/dev/null 2>&1 || { echo "FALLO: curl no disponible"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "FALLO: python3 no disponible"; exit 1; }

for b in build/memorytool build/objetivo build/pointer_offset_test; do
    [ -x "$b" ] || { echo "FALLO: falta $b (ejecuta ./build.sh)"; exit 1; }
done

LOG=$(mktemp /tmp/mt_ui_mt_XXXXXX)
SRV=""; OBJ_PID=""; PT_PID=""
fails=0
cleanup() {
    [ -n "$SRV" ] && kill "$SRV" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    [ -n "$PT_PID" ] && kill "$PT_PID" 2>/dev/null || true
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null || true
    rm -f "$LOG" "$LOG.obj" "$LOG.pt" tables/uitabla
    rmdir tables 2>/dev/null || true
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

# --- 1-3) assets: GET / + Memory UI + Table UI -------------------------------
idx=$(curl -s "$URL/")
check "GET / 200" test -n "$idx"
check "index contiene pestaña Memory" grep -q 'data-tab="memory"' <<<"$idx"
check "index contiene pestaña Address Table" grep -q 'data-tab="table"' <<<"$idx"
check "index contiene panel memory" grep -q 'id="panel-memory"' <<<"$idx"
check "index contiene panel table" grep -q 'id="panel-table"' <<<"$idx"

app=$(curl -s "$URL/app.js")
check "app.js contiene memRead" grep -q "async function memRead" <<<"$app"
check "app.js contiene tblRefresh" grep -q "async function tblRefresh" <<<"$app"
check "app.js contiene tblResolve" grep -q "async function tblResolve" <<<"$app"
check "app.js contiene validFileName" grep -q "function validFileName" <<<"$app"

css=$(curl -s "$URL/styles.css")
check "styles.css contiene mem-hex" grep -q "\.mem-hex" <<<"$css"
check "styles.css contiene tabla" grep -q "table\.tbl" <<<"$css"

# api <metodo> <ruta> [json] -> deja $J y $code en el shell actual
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

# --- 4) attach objetivo -------------------------------------------------------
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

# --- 5) memory read -----------------------------------------------------------
NEXP=$(grep -m1 'dinero' "$LOG.obj" | grep -o '0x[0-9a-f]*' | sed -E 's/^0x0*//')
[ -n "$NEXP" ] || { echo "FALLO: no se pudo leer la direccion de dinero"; exit 1; }
FULL=$(printf '0x%016x' "$((0x$NEXP))")
api GET "/api/memory?address=$FULL&length=32"
check "memory read 200" test "$code" = "200"
check "memory hex presente" grep -q '"hex":"' <<<"$J"
check "memory ascii presente" grep -q '"ascii":"' <<<"$J"
check "memory region presente" grep -q '"region":' <<<"$J"
REGION=$(printf '%s' "$J" | json_get 'd["region"] and d["region"]["perms"]')
check "memory region perms" test -n "$REGION"
echo "OK: memory read de $FULL (region perms=$REGION)"

# --- 6-9) table add/read/set/toggle -------------------------------------------
api POST /api/table/add "{\"address\":\"$FULL\",\"type\":\"int32\",\"description\":\"dinero ui\"}"
check "table add 200" test "$code" = "200"
TI=$(printf '%s' "$J" | json_get 'd["index"]')
check "table add index 0" test "$TI" = "0"

api POST /api/table/read '{"index":0}'
check "table read 200" test "$code" = "200"
check "table read value 12345" grep -q '"value":"12345"' <<<"$J"

api POST /api/table/set '{"index":0,"value":"77777"}'
check "table set 200" test "$code" = "200"
check "table set verified" grep -q '"verified":true' <<<"$J"
VOK=0
for _ in $(seq 1 40); do
    grep -q 'Dinero: 77777' "$LOG.obj" && { VOK=1; break; }
    sleep 0.2
done
[ "$VOK" = 1 ] || { echo "FALLO: el proceso no refleja el valor 77777"; exit 1; }
echo "OK: table set verificado en el proceso (Dinero: 77777)"

api POST /api/table/toggle '{"index":0}'
check "table toggle 200" test "$code" = "200"
check "table toggle disabled" grep -q '"enabled":false' <<<"$J"

# --- 10) pointer resolve (entrada pointer) ------------------------------------
kill "$OBJ_PID" 2>/dev/null || true
OBJ_PID=""
( sleep 120 ) | build/pointer_offset_test > "$LOG.pt" 2>&1 &
PT_PID=$!
PP2=""
for _ in $(seq 1 50); do
    PP2=$(grep -m1 -o 'PID: [0-9]*' "$LOG.pt" | grep -o '[0-9]*' || true)
    [ -n "$PP2" ] && break
    sleep 0.2
done
[ -n "$PP2" ] || { echo "FALLO: pointer_offset_test no arranco"; exit 1; }
PTGT=$(grep -m1 '^TARGET:' "$LOG.pt" | grep -o '0x[0-9a-f]*')
[ -n "$PTGT" ] || { echo "FALLO: sin TARGET en pointer_offset_test"; exit 1; }
echo "pointer test PID=$PP2 TARGET=$PTGT"
api POST /api/attach "{\"pid\":\"$PP2\"}"
check "attach pointer 200" test "$code" = "200"

api POST /api/pointer/scan "{\"target\":\"$PTGT\",\"depth\":3}"
check "pointer scan 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=""
for _ in $(seq 1 300); do
    api GET "/api/jobs/$JID"
    ST=$(printf '%s' "$J" | json_get 'd["state"]')
    case "$ST" in completed|cancelled|failed) break;; esac
    sleep 0.1
done
check "pointer job completed" test "$ST" = "completed"

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
api POST /api/pointer/add "{\"chain_index\":$IDX,\"description\":\"valor ui\"}"
check "pointer add 200" test "$code" = "200"
check "pointer add kind MODULE" grep -q '"kind":"MODULE"' <<<"$J"

api POST /api/pointer/resolve '{"index":1}'
check "pointer resolve 200" test "$code" = "200"
check "pointer resolve value 4242" grep -q '"value":"4242"' <<<"$J"
echo "OK: pointer resolve -> valor 4242"

# la tabla ahora tiene 2 entradas (absolute + pointer)
api GET /api/table
check "table tiene 2 entradas" grep -q '"count":2' <<<"$J"
check "tabla tiene entrada pointer" grep -q '"pointer":' <<<"$J"

# --- 11-12) save / load -------------------------------------------------------
api POST /api/table/save '{"name":"uitabla"}'
check "table save 200" test "$code" = "200"
api POST /api/table/load '{"name":"uitabla"}'
check "table load 200" test "$code" = "200"
CNT=$(printf '%s' "$J" | json_get 'd["count"]')
check "table load count 2" test "$CNT" = "2"

# --- 13) remove ---------------------------------------------------------------
api POST /api/table/remove '{"index":0}'
check "table remove 200" test "$code" = "200"
api GET /api/table
check "table quedan 1 entrada" grep -q '"count":1' <<<"$J"

# --- 14) detach ----------------------------------------------------------------
api POST /api/detach
check "detach 200" test "$code" = "200"
check "detach attached:false" grep -q '"attached":false' <<<"$J"

# --- terminacion limpia ---------------------------------------------------------
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true
SRV=""
sleep 0.3
if kill -0 "$SRV" 2>/dev/null; then
    echo "FALLO: el servidor sigue vivo tras la terminacion"; fails=$((fails + 1))
fi
kill "$PT_PID" 2>/dev/null || true
PT_PID=""
rm -f "$LOG" "$LOG.obj" "$LOG.pt" tables/uitabla
rmdir tables 2>/dev/null || true

if [ "$fails" = 0 ]; then
    echo
    echo "== PRUEBA WEB UI MEMORY/TABLE E2E SUPERADA =="
    exit 0
fi
echo
echo "== PRUEBA WEB UI MEMORY/TABLE E2E CON FALLOS ($fails) =="
exit 1
