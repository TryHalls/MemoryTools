#!/usr/bin/env bash
# web_ui_e2e.sh - E2E de la Web UI estatica (FASE W-5A).
#
# Sin navegador automatizable: comprueba con curl que el servidor sirve los
# assets de la UI (GET /, /app.js, /styles.css), que index.html recibe el
# token inyectado (bootstrap) y que la API sigue exigiendo X-MemoryTool-Token.
#
# Uso: bash tests/web_ui_e2e.sh   (desde la raiz del repo; usa build/memorytool)
set -u

cd "$(dirname "$0")/.." || exit 1
LOG=$(mktemp)
PID=""
fail=0
ok=0

cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    [ -n "$PID" ] && wait "$PID" 2>/dev/null
    rm -f "$LOG"
}
trap cleanup EXIT

# --- arrancar servidor ------------------------------------------------------
./build/memorytool --web --port 0 >"$LOG" 2>&1 &
PID=$!
sleep 1.5

PORT=$(sed -n 's/^URL: http:\/\/127.0.0.1:\([0-9]*\)/\1/p' "$LOG")
TOK=$(sed -n 's/^Token: //p' "$LOG")
if [ -z "$PORT" ] || [ -z "$TOK" ]; then
    echo "FALLO: no se pudo arrancar el servidor o leer puerto/token"
    cat "$LOG"
    exit 1
fi
echo "== servidor en http://127.0.0.1:$PORT (token ${TOK:0:8}...) =="

# --- GET / ------------------------------------------------------------------
code=$(curl -s -o /tmp/ui_idx.html -w "%{http_code}" "http://127.0.0.1:$PORT/")
if [ "$code" = "200" ]; then echo "ok: GET / 200"; else echo "FAIL: GET / -> $code"; fail=1; fi

if grep -q "MemoryTool" /tmp/ui_idx.html; then
    echo "ok: index.html contiene MemoryTool"
else
    echo "FAIL: index.html no contiene MemoryTool"; fail=1
fi

# Token inyectado en la pagina (bootstrap), nunca en URLs.
if grep -q "window.MEMORYTOOL_TOKEN = \"$TOK\"" /tmp/ui_idx.html; then
    echo "ok: token inyectado en index.html"
else
    echo "FAIL: token no inyectado en index.html"; fail=1
fi

# El token NO aparece en las URLs de los assets referenciados.
if grep -q 'src="[^"]*?token=' /tmp/ui_idx.html ||
   grep -q 'href="[^"]*?token=' /tmp/ui_idx.html; then
    echo "FAIL: token aparece en alguna URL"; fail=1
else
    echo "ok: token no aparece en URLs"
fi

# --- GET /app.js ------------------------------------------------------------
code=$(curl -s -o /tmp/ui_app.js -w "%{http_code}" "http://127.0.0.1:$PORT/app.js")
if [ "$code" = "200" ]; then echo "ok: GET /app.js 200"; else echo "FAIL: GET /app.js -> $code"; fail=1; fi
if grep -q "VirtualTable" /tmp/ui_app.js; then
    echo "ok: app.js contiene VirtualTable"
else
    echo "FAIL: app.js no contiene VirtualTable"; fail=1
fi

# --- GET /styles.css ---------------------------------------------------------
code=$(curl -s -o /tmp/ui_css.css -w "%{http_code}" "http://127.0.0.1:$PORT/styles.css")
if [ "$code" = "200" ]; then echo "ok: GET /styles.css 200"; else echo "FAIL: GET /styles.css -> $code"; fail=1; fi

# --- la API sigue protegida --------------------------------------------------
code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/status")
if [ "$code" = "401" ]; then
    echo "ok: /api/status sin token -> 401"
else
    echo "FAIL: /api/status sin token -> $code (esperado 401)"; fail=1
fi

code=$(curl -s -o /dev/null -w "%{http_code}" -H "X-MemoryTool-Token: $TOK" \
    "http://127.0.0.1:$PORT/api/status")
if [ "$code" = "200" ]; then
    echo "ok: /api/status con token -> 200"
else
    echo "FAIL: /api/status con token -> $code"; fail=1
fi

# --- ruta no-API no reconocida -------------------------------------------------
# Sin token -> 401 (la seguridad protege todo lo que no es un asset whitelisted);
# con token -> 404 (ya cubierto en web_e2e.sh).
code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/nope.js")
if [ "$code" = "401" ]; then
    echo "ok: ruta no-API sin token -> 401"
else
    echo "FAIL: ruta no-API sin token -> $code (esperado 401)"; fail=1
fi

rm -f /tmp/ui_idx.html /tmp/ui_app.js /tmp/ui_css.css

if [ "$fail" = "0" ]; then
    echo ""
    echo "== PRUEBA WEB UI E2E SUPERADA =="
    exit 0
else
    echo ""
    echo "== PRUEBA WEB UI E2E: FALLOS =="
    exit 1
fi
