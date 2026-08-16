#!/usr/bin/env bash
# E2E de la Web UI - pestana Pattern (AOB/Bytes + String) (FASE W-5C).
#
# Sin navegador automatizable: arranca `memorytool --web --port 0`, comprueba
# que se sirven los assets con la pestana Pattern y ejercita con curl el flujo
# real contra `objetivo` (patron AOB, wildcard, string first, next changed/
# unchanged, seleccion de resultado y apertura en Memory Viewer) y la
# cancelacion de un job en curso.
# Uso: bash tests/web_ui_pattern_e2e.sh   (0 = OK, !=0 = fallo)
set -euo pipefail
cd "$(dirname "$0")/.."

command -v curl >/dev/null 2>&1 || { echo "FALLO: curl no disponible"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "FALLO: python3 no disponible"; exit 1; }

for b in build/memorytool build/objetivo; do
    [ -x "$b" ] || { echo "FALLO: falta $b (ejecuta ./build.sh)"; exit 1; }
done

LOG=$(mktemp /tmp/mt_ui_pat_XXXXXX)
SRV=""; OBJ_PID=""; BIG_PID=""
fails=0
cleanup() {
    [ -n "$SRV" ] && kill "$SRV" 2>/dev/null || true
    [ -n "$OBJ_PID" ] && kill "$OBJ_PID" 2>/dev/null || true
    [ -n "$BIG_PID" ] && kill "$BIG_PID" 2>/dev/null || true
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null || true
    rm -f "$LOG" "$LOG.obj" "$LOG.big"
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

# --- 1-3) assets: GET / + Pattern tab + String tab ----------------------------
idx=$(curl -s "$URL/")
check "GET / 200" test -n "$idx"
check "index contiene pestana Pattern" grep -q 'data-tab="pattern"' <<<"$idx"
check "index contiene panel pattern" grep -q 'id="panel-pattern"' <<<"$idx"
check "index contiene selector de modo" grep -q 'id="pat-mode"' <<<"$idx"
check "index contiene input pattern" grep -q 'id="pat-pattern"' <<<"$idx"
check "index contiene input string" grep -q 'id="pat-text"' <<<"$idx"
check "index contiene tabla pattern" grep -q 'id="vt-pattern"' <<<"$idx"

app=$(curl -s "$URL/app.js")
check "app.js contiene patScan" grep -q "async function patScan" <<<"$app"
check "app.js contiene patNext" grep -q "async function patNext" <<<"$app"
check "app.js contiene patVt" grep -q "const patVt" <<<"$app"
check "app.js contiene dynRender" grep -q "function dynRender" <<<"$app"
check "app.js contiene showRowDetail" grep -q "function showRowDetail" <<<"$app"
check "app.js contiene openInMemoryViewer" grep -q "function openInMemoryViewer" <<<"$app"

css=$(curl -s "$URL/styles.css")
check "styles.css contiene sel-detail" grep -q "\.sel-detail" <<<"$css"
check "styles.css contiene clickable" grep -q "clickable" <<<"$css"

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

# --- 4) attach objetivo (strings/bytes) ---------------------------------------
( sleep 6; echo "m mundo memorytool"; sleep 4; echo "b"; sleep 60 ) \
    | build/objetivo > "$LOG.obj" 2>&1 &
OBJ_PID=$!
PID=""
for _ in $(seq 1 50); do
    PID=$(grep -m1 -o 'PID: [0-9]*' "$LOG.obj" | grep -o '[0-9]*' || true)
    [ -n "$PID" ] && break
    sleep 0.2
done
[ -n "$PID" ] || { echo "FALLO: objetivo no arranco"; exit 1; }
MSGADDR=$(grep -m1 'mensaje' "$LOG.obj" | grep -o '0x[0-9a-f]*' || true)
DADDR=$(grep -m1 'datos' "$LOG.obj" | grep -o '0x[0-9a-f]*' || true)
[ -n "$MSGADDR" ] && [ -n "$DADDR" ] || { echo "FALLO: faltan direcciones"; exit 1; }
NMSG=$(printf '%s' "$MSGADDR" | sed -E 's/^0x0*//')
NDAT=$(printf '%s' "$DADDR" | sed -E 's/^0x0*//')
echo "objetivo PID=$PID MSGADDR=$MSGADDR DADDR=$DADDR"
api POST /api/attach "{\"pid\":\"$PID\"}"
check "attach 200" test "$code" = "200"

# --- 5-7) pattern scan AOB + polling + results --------------------------------
# REGRESION W-5E1 (bug critico 1): el pattern scan ocurre SIN scan numerico
# previo. Con el bug, VirtualTable.loadTotal() consultaba /api/results (que
# devuelve total 0 porque no hay scan numerico) y la tabla de pattern quedaba
# vacia aunque /api/pattern/results tuviese hits. El fix usa this.endpoint.
api GET "/api/results?offset=0&limit=1"
check "sin scan numerico: total 0" grep -q '"total":"0"' <<<"$J"
# "hola memorytool" en bytes: 68 6F 6C 61 20 6D 65 6D 6F 72 79 74 6F 6F 6C
api POST /api/pattern '{"pattern":"68 6F 6C 61 20 6D 65 6D"}'
check "pattern scan 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
check "pattern job queued" grep -q '"state":"queued"' <<<"$J"
ST=$(wait_job "$JID")
check "pattern job completed" test "$ST" = "completed"
api GET "/api/pattern/results?offset=0&limit=100"
check "pattern results 200" test "$code" = "200"
PTOTAL=$(printf '%s' "$J" | json_get 'd["total"]')
check "pattern results total>=1" test "$PTOTAL" -ge 1
grep -q "\"address\":\"0x0*$NMSG\"" <<<"$J" \
    || { echo "FALLO: la direccion del mensaje no esta en pattern results:"; echo "$J"; exit 1; }
echo "OK: pattern scan encontro el mensaje ($NMSG)"

# --- 8) string first -----------------------------------------------------------
api POST /api/scan/first '{"type":"string","value":"hola memorytool"}'
check "string first 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "string first completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
check "results dinamicos 200" test "$code" = "200"
check "results dinamicos con length" grep -q '"length":' <<<"$J"
grep -q "\"address\":\"0x0*$NMSG\"" <<<"$J" \
    || { echo "FALLO: la direccion del mensaje no esta en los resultados string:"; echo "$J"; exit 1; }
echo "OK: string first encontro el mensaje"

# --- 9) next changed (el objetivo cambia el mensaje a los 6s) -------------------
CH=0
for _ in $(seq 1 60); do
    grep -q 'mundo memorytool' "$LOG.obj" && { CH=1; break; }
    sleep 0.2
done
[ "$CH" = 1 ] || { echo "FALLO: el objetivo no cambio el mensaje a tiempo"; exit 1; }
api POST /api/scan/next '{"filter":"changed"}'
check "next changed 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "next changed completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NMSG\"" <<<"$J" \
    || { echo "FALLO: next changed perdio la direccion del mensaje:"; echo "$J"; exit 1; }
echo "OK: next changed conserva la direccion del mensaje"

# --- 10) next exact + next unchanged ---------------------------------------------
# Semantica del backend: el filtro dinamico compara contra el patron del scan
# actual (dyn_spec_). Tras 'changed', el patron base sigue siendo el original;
# 'exact' con el texto nuevo actualiza dyn_spec_ y luego 'unchanged' conserva
# los candidatos cuyo contenido actual iguala ese patron (el mensaje ya no
# cambia mas).
api POST /api/scan/next '{"filter":"exact","value":"mundo memorytool"}'
check "next exact 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "next exact completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NMSG\"" <<<"$J" \
    || { echo "FALLO: next exact perdio la direccion del mensaje:"; echo "$J"; exit 1; }
echo "OK: next exact conserva la direccion del mensaje"

api POST /api/scan/next '{"filter":"unchanged"}'
check "next unchanged 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "next unchanged completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NMSG\"" <<<"$J" \
    || { echo "FALLO: next unchanged perdio la direccion del mensaje:"; echo "$J"; exit 1; }
echo "OK: next unchanged conserva la direccion del mensaje"

# --- 11) bytes pattern (datos) --------------------------------------------------
api POST /api/scan/first '{"type":"bytes","value":"48 8B 05 90 90"}'
check "bytes first 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "bytes first completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NDAT\"" <<<"$J" \
    || { echo "FALLO: la direccion de datos no esta en los resultados bytes:"; echo "$J"; exit 1; }
echo "OK: bytes exacto encontro 'datos'"

# --- 12) wildcard -----------------------------------------------------------------
api POST /api/scan/first '{"type":"bytes","value":"48 8B 05 ?? ??"}'
check "bytes wildcard 200" test "$code" = "200"
JID=$(printf '%s' "$J" | json_get 'd["job_id"]')
ST=$(wait_job "$JID")
check "bytes wildcard completed" test "$ST" = "completed"
api GET "/api/results?offset=0&limit=100"
grep -q "\"address\":\"0x0*$NDAT\"" <<<"$J" \
    || { echo "FALLO: la direccion de datos no esta con wildcard:"; echo "$J"; exit 1; }
echo "OK: bytes con wildcard ?? encontro 'datos'"

# --- 13) seleccion de resultado (una fila paginada) --------------------------------
api GET "/api/results?offset=0&limit=10"
FIRSTADDR=$(printf '%s' "$J" | json_get 'd["rows"][0]["address"]')
check "fila seleccionable tiene address" test -n "$FIRSTADDR"
echo "OK: seleccion de resultado -> $FIRSTADDR"

# --- 14) abrir Memory Viewer (GET /api/memory con esa direccion) --------------------
api GET "/api/memory?address=$FIRSTADDR&length=32"
check "memory (open in viewer) 200" test "$code" = "200"
check "memory hex presente" grep -q '"hex":"' <<<"$J"
echo "OK: apertura en Memory Viewer lee la direccion seleccionada"

# --- 15) cancelacion de un job en curso -------------------------------------------
kill "$OBJ_PID" 2>/dev/null || true
OBJ_PID=""
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
ST=$(wait_job "$JID")
check "job final cancelled" test "$ST" = "cancelled"
echo "OK: el job cancelado termino en CANCELLED"
kill "$BIG_PID" 2>/dev/null || true
BIG_PID=""

# --- 16) detach ---------------------------------------------------------------------
api POST /api/detach
check "detach 200" test "$code" = "200"
check "detach attached:false" grep -q '"attached":false' <<<"$J"

# --- terminacion limpia ---------------------------------------------------------------
kill "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true
SRV=""
sleep 0.3
if kill -0 "$SRV" 2>/dev/null; then
    echo "FALLO: el servidor sigue vivo tras la terminacion"; fails=$((fails + 1))
fi
rm -f "$LOG" "$LOG.obj" "$LOG.big"

if [ "$fails" = 0 ]; then
    echo
    echo "== PRUEBA WEB UI PATTERN E2E SUPERADA =="
    exit 0
fi
echo
echo "== PRUEBA WEB UI PATTERN E2E CON FALLOS ($fails) =="
exit 1
