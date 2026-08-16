#!/usr/bin/env bash
# E2E de la Web UI local (FASE W-3): arranca `memorytool --web --port 0`,
# obtiene URL y token de la salida estandar y verifica los endpoints con curl.
# Uso: bash tests/web_e2e.sh   (0 = OK, !=0 = fallo)
set -euo pipefail
cd "$(dirname "$0")/.."

command -v curl >/dev/null 2>&1 || { echo "FALLO: curl no disponible"; exit 1; }

LOG=$(mktemp /tmp/mt_web_e2e_XXXXXX)
SRV=""
cleanup() {
    if [ -n "$SRV" ]; then
        kill "$SRV" 2>/dev/null || true
        wait "$SRV" 2>/dev/null || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT

# --- Arrancar el servidor ----------------------------------------------------
./build/memorytool --web --port 0 >"$LOG" 2>&1 &
SRV=$!

URL=""
TOKEN=""
for _ in $(seq 1 100); do
    URL=$(grep -m1 '^URL:' "$LOG" | awk '{print $2}' || true)
    TOKEN=$(grep -m1 '^Token:' "$LOG" | awk '{print $2}' || true)
    if [ -n "$URL" ] && [ -n "$TOKEN" ]; then break; fi
    sleep 0.1
done
if [ -z "$URL" ] || [ -z "$TOKEN" ]; then
    echo "FALLO: no se obtuvo URL/Token del servidor"
    cat "$LOG"
    exit 1
fi
PORT="${URL##*:}"
echo "== servidor en $URL (token ${TOKEN:0:8}...) =="

fails=0
check() { # nombre condicion...
    local name="$1"; shift
    if "$@"; then
        echo "ok: $name"
    else
        echo "FALLO: $name"
        fails=$((fails + 1))
    fi
}

# 1) GET /api/status con token -> 200 y JSON con "ok":true
out=$(curl -s -w '\n%{http_code}' -H "X-MemoryTool-Token: $TOKEN" "$URL/api/status")
code=$(printf '%s' "$out" | tail -1)
body=$(printf '%s' "$out" | sed '$d')
check "status 200" test "$code" = "200"
check "status ok:true" grep -q '"ok":true' <<<"$body"
check "status attached" grep -q '"attached":' <<<"$body"

# 2) GET /api/processes -> 200 y array JSON no vacio
out=$(curl -s -w '\n%{http_code}' -H "X-MemoryTool-Token: $TOKEN" "$URL/api/processes")
code=$(printf '%s' "$out" | tail -1)
body=$(printf '%s' "$out" | sed '$d')
check "processes 200" test "$code" = "200"
check "processes ok" grep -q '"ok":true' <<<"$body"
check "processes array" grep -q '"processes":\[' <<<"$body"
check "processes contenido" grep -q '"pid":"' <<<"$body"

# 3) GET /api/jobs/1 inexistente -> 404
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-MemoryTool-Token: $TOKEN" "$URL/api/jobs/1")
check "job inexistente 404" test "$code" = "404"

# 4) token incorrecto -> 401
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-MemoryTool-Token: token-incorrecto" "$URL/api/status")
check "token incorrecto 401" test "$code" = "401"

# 5) sin token -> 401
code=$(curl -s -o /dev/null -w '%{http_code}' "$URL/api/status")
check "sin token 401" test "$code" = "401"

# 6) Host incorrecto -> 400
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-MemoryTool-Token: $TOKEN" -H "Host: evil.com" "$URL/api/status")
check "host incorrecto 400" test "$code" = "400"

# 7) body grande (70 KiB) -> 413
big=$(python3 -c "print('x'*70000)")
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST \
    -H "X-MemoryTool-Token: $TOKEN" --data "$big" "$URL/api/status")
check "body grande 413" test "$code" = "413"

# 8) metodo no soportado (DELETE) -> 405
code=$(curl -s -o /dev/null -w '%{http_code}' -X DELETE \
    -H "X-MemoryTool-Token: $TOKEN" "$URL/api/status")
check "DELETE 405" test "$code" = "405"

# 9) ruta desconocida -> 404
code=$(curl -s -o /dev/null -w '%{http_code}' -H "X-MemoryTool-Token: $TOKEN" "$URL/api/noexiste")
check "ruta desconocida 404" test "$code" = "404"

# --- Terminacion limpia -------------------------------------------------------
kill "$SRV"
wait "$SRV" 2>/dev/null || true
SRV=""

sleep 0.3
if kill -0 "$SRV" 2>/dev/null; then
    echo "FALLO: el servidor sigue vivo tras la terminacion"
    fails=$((fails + 1))
fi

if [ "$fails" = 0 ]; then
    echo
    echo "== PRUEBA WEB E2E SUPERADA =="
    exit 0
fi
echo
echo "== PRUEBA WEB E2E CON FALLOS ($fails) =="
exit 1
