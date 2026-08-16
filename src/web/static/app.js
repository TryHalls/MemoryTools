/* MemoryTool Web UI - cliente (FASE W-5A).
 *
 * Vanilla JS, sin dependencias. La UI NUNCA bloquea esperando un scan:
 * todo scan pesado se lanza via POST -> job_id y se hace polling con
 * GET /api/jobs/<id> cada ~250 ms.
 *
 * El token lo inyecta el servidor en index.html (window.MEMORYTOOL_TOKEN);
 * se envia como cabecera X-MemoryTool-Token en TODA request de API y nunca
 * aparece en URLs.
 */
"use strict";

/* ============================ API CLIENT ================================= */

const TOKEN = window.MEMORYTOOL_TOKEN || "";

async function api(method, path, body) {
  const headers = { "X-MemoryTool-Token": TOKEN };
  let opts = { method, headers };
  if (body !== undefined) {
    headers["Content-Type"] = "application/json";
    opts.body = JSON.stringify(body);
  }
  let resp;
  try {
    resp = await fetch(path, opts);
  } catch (e) {
    const err = new Error("No se pudo conectar con el servidor: " + e.message);
    err.status = 0;
    throw err;
  }
  let data = null;
  try { data = await resp.json(); } catch (e) { /* cuerpo no JSON */ }
  if (!resp.ok) {
    const code = data && data.code ? data.code : "http_" + resp.status;
    const msg = data && data.error ? data.error : "HTTP " + resp.status;
    const err = new Error(msg);
    err.status = resp.status;
    err.code = code;
    throw err;
  }
  return data;
}

const apiGet = (p) => api("GET", p);
const apiPost = (p, b) => api("POST", p, b);

/* ============================ ESTADO ===================================== */

const state = {
  attached: false,
  pid: 0,
  jobId: 0,
  jobTimer: null,
  jobOnComplete: null, // callback al terminar el job (refresh de la tabla correcta)
  cancelInFlight: false,
  pollInFlight: false, // evita polling concurrente (fetch pendiente)
  selectedPid: null,
  resultsTotal: 0,
  scanDone: false,
};

const $ = (id) => document.getElementById(id);

function setConn(on, label) {
  const dot = $("conn-dot");
  dot.className = "dot " + (on ? "on" : "off");
  $("conn-label").textContent = label;
}

function setPid(pid) {
  state.pid = pid;
  $("pid-label").textContent = pid ? "PID: " + pid : "PID: --";
  $("btn-detach").disabled = !pid;
}

function footer(msg, isErr) {
  const el = $("footer-msg");
  el.textContent = msg;
  el.className = isErr ? "err" : "";
}

// Mensaje de error con codigo estable del backend cuando esta disponible
// (p. ej. "no_process", "scan_invalid", "busy"), evitando "HTTP 500" generico.
function errText(e) {
  if (!e) return "error";
  const code = e.code && e.code.indexOf("http_") !== 0 ? e.code : "";
  return (code ? code + ": " : "") + (e.message || String(e));
}

function showJobBox(show) {
  $("job-box").classList.toggle("hidden", !show);
}

/* ============================ STATUS ===================================== */

async function refreshStatus() {
  try {
    const d = await apiGet("/api/status");
    state.attached = !!d.attached;
    setConn(state.attached, state.attached ? "Conectado" : "Desconectado");
    setPid(d.pid && d.pid !== "0" ? d.pid : 0);
    if (d.runner_busy) {
      const j = d.job;
      if (j && j.id && !state.jobId)
        // Job recuperado tras recargar la pagina: al completar refresca la
        // tabla correspondiente segun el kind del job.
        startPolling(j.id, () => reloadTableForKind(j.kind));
    }
  } catch (e) {
    setConn(false, "Sin servidor");
  }
}

// Seleccion de la tabla virtual segun el kind de un job (pura/testeable).
function tableNameForJobKind(kind) {
  if (kind === "pattern") return "patVt";
  if (kind === "pointer") return "ptrVt";
  return "vt";
}

// Recarga la tabla correspondiente al kind de un job recuperado tras F5.
function reloadTableForKind(kind) {
  if (kind === "pattern") {
    patVt.endpoint = "/api/pattern/results";
    patVt.renderRow = (item) => [item.address, "match", ""];
    $("pat-col-val").textContent = "Match";
    $("pat-col-type").textContent = "";
    patVt.reload();
    return;
  }
  if (kind === "pointer") {
    ptrVt.endpoint = "/api/pointer/results";
    ptrVt.renderRow = ptrRender;
    ptrVt.reload();
    return;
  }
  vt.reload();
}

/* ============================ PROCESOS =================================== */

async function loadProcesses() {
  const list = $("proc-list");
  list.innerHTML = '<div class="proc-empty">Cargando...</div>';
  try {
    const d = await apiGet("/api/processes");
    const procs = d.processes || [];
    if (!procs.length) {
      list.innerHTML = '<div class="proc-empty">Sin procesos</div>';
      return;
    }
    list.innerHTML = "";
    for (const p of procs) {
      const row = document.createElement("div");
      row.className = "proc-row";
      if (state.selectedPid === p.pid) row.classList.add("selected");
      const acc = p.accessible
        ? '<span class="p-acc yes">si</span>'
        : p.accessible === false
          ? '<span class="p-acc no">denegado</span>'
          : '<span class="p-acc unk">?</span>';
      row.innerHTML =
        '<span class="p-pid">' + p.pid + "</span>" +
        '<span class="p-name" title="' + esc(p.name) + '">' + esc(p.name) + "</span>" +
        acc;
      row.addEventListener("click", () => selectProcess(p.pid, row));
      list.appendChild(row);
    }
  } catch (e) {
    list.innerHTML = '<div class="proc-empty">' + esc(e.message) + "</div>";
  }
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]));
}

function selectProcess(pid, row) {
  state.selectedPid = pid;
  document.querySelectorAll(".proc-row").forEach((r) => r.classList.remove("selected"));
  if (row) row.classList.add("selected");
}

/* ============================ ATTACH / DETACH ============================ */

async function attachSelected() {
  if (state.jobId) {
    footer("Otro scan esta en curso (cancelalo primero)", true);
    return;
  }
  if (!state.selectedPid) {
    footer("Selecciona un proceso de la lista", true);
    return;
  }
  try {
    await apiPost("/api/attach", { pid: String(state.selectedPid) });
    footer("Attach a " + state.selectedPid + " correcto.");
    await refreshStatus();
  } catch (e) {
    footer("Attach fallo: " + e.message, true);
    if (e.status === 409) footer("Servidor ocupado: " + e.message, true);
  }
}

// Limpia todas las vistas dependientes del proceso para que tras detach (o
// cambio de proceso) la UI no muestre datos engañosos del proceso anterior.
// Conserva configuraciones reutilizables (depth/max_offset/step/type...).
function clearProcessViews() {
  // Memory Viewer.
  $("mem-hex").textContent = "";
  $("mem-region").textContent = "";
  memState.addr = 0;
  // Address Table.
  $("tbl-body").innerHTML = "";
  $("tbl-detail").classList.add("hidden");
  $("tbl-detail").innerHTML = "";
  // Detalles de seleccion.
  $("scan-detail").classList.add("hidden");
  $("scan-detail").innerHTML = "";
  $("pat-detail").classList.add("hidden");
  $("pat-detail").innerHTML = "";
  $("ptr-detail").classList.add("hidden");
  $("ptr-detail").innerHTML = "";
  // Estados de seleccion de pointers.
  ptrState.selectedIndex = null;
  ptrState.tableIndex = null;
  ptrState.resolved = null;
}

async function detach() {
  if (state.jobId) {
    footer("Otro scan esta en curso (cancelalo primero)", true);
    return;
  }
  try {
    await apiPost("/api/detach", {});
    footer("Detach correcto.");
    await refreshStatus();
    vt.clear();
    patVt.clear();
    ptrVt.clear();
    clearProcessViews();
  } catch (e) {
    footer("Detach fallo: " + e.message, true);
  }
}

/* ============================ SCANS ====================================== */

function currentType() { return $("scan-type").value; }
function currentValue() { return $("scan-value").value.trim(); }
function currentFilter() { return $("scan-filter").value; }

async function firstScan(unknown) {
  if (state.jobId) { footer("Ya hay un scan en curso", true); return; }
  const type = currentType();
  let body = { type };
  if (unknown) {
    body.value = null;
  } else {
    const v = currentValue();
    if (!v) { footer("Introduce un valor para el First Scan", true); return; }
    body.value = v;
  }
  await launchScan("/api/scan/first", body, () => vt.reload());
}

async function nextScan() {
  if (state.jobId) { footer("Ya hay un scan en curso", true); return; }
  const filter = currentFilter();
  const body = { filter };
  if (filter === "exact") {
    body.type = currentType();
    const v = currentValue();
    if (!v) { footer("Introduce un valor para exact", true); return; }
    body.value = v;
  }
  await launchScan("/api/scan/next", body, () => vt.reload());
}

async function launchScan(path, body, onComplete) {
  try {
    const d = await apiPost(path, body);
    if (d.job_id) {
      startPolling(d.job_id, onComplete);
    } else {
      footer("Sin job_id en la respuesta", true);
    }
  } catch (e) {
    footer((e.code === "busy" ? "Servidor ocupado: " : "Scan fallo: ") + e.message, true);
  }
}

/* ============================ JOB POLLING ================================ */

function startPolling(jobId, onComplete) {
  stopPolling();
  state.jobId = jobId;
  state.jobOnComplete = onComplete || null;
  state.cancelInFlight = false;
  showJobBox(true);
  setJobState("queued", 0, 0, 0);
  pollOnce();
  state.jobTimer = setInterval(pollOnce, 250);
}

function stopPolling() {
  if (state.jobTimer) {
    clearInterval(state.jobTimer);
    state.jobTimer = null;
  }
  state.jobId = 0;
}

function setJobState(s, progress, count, elapsed) {
  $("job-state").textContent = s;
  const fill = $("job-fill");
  fill.style.width = Math.max(0, Math.min(100, progress)) + "%";
  let info = count > 0 ? "count: " + count.toLocaleString() : "";
  if (elapsed > 0) info += (info ? "  " : "") + "elapsed: " + elapsed + " ms";
  $("job-info").textContent = info;
  $("btn-cancel").disabled = state.cancelInFlight || (s !== "running" && s !== "queued");
}

async function pollOnce() {
  // Evita varios fetch simultaneos si uno tarda mas de 250 ms.
  if (state.pollInFlight) return;
  const jid = state.jobId;
  if (!jid) return;
  state.pollInFlight = true;
  try {
    let d;
    try {
      d = await apiGet("/api/jobs/" + jid);
    } catch (e) {
      if (e.status === 404) {
        finishJob("job " + jid + " ya no existe", null);
        return;
      }
      return; // reintenta en el siguiente tick
    }
    const s = d.state || "unknown";
    const progress = typeof d.progress === "number" ? d.progress : 0;
    const count = parseInt(d.count || "0", 10);
    const elapsed = d.elapsed_ms || 0;
    setJobState(s, progress, count, elapsed);

    if (s === "completed") finishJob(null, d);
    else if (s === "cancelled") finishJob("Scan cancelado", d);
    else if (s === "failed") finishJob(d.error || "Scan fallo", d);
  } finally {
    state.pollInFlight = false;
  }
}

function finishJob(msg, d) {
  stopPolling();
  showJobBox(false);
  if (msg) {
    footer(msg, true);
    return;
  }
  // COMPLETED: el resultado ya vive en el servidor; refresca la tabla que
  // lanzo el job (scan o pattern).
  state.scanDone = true;
  footer("Scan completado.");
  const cb = state.jobOnComplete;
  state.jobOnComplete = null;
  if (cb) cb();
  else vt.reload();
  refreshStatus();
}

async function cancelJob() {
  if (!state.jobId || state.cancelInFlight) return;
  state.cancelInFlight = true;
  $("btn-cancel").disabled = true;
  try {
    await apiPost("/api/jobs/" + state.jobId + "/cancel", {});
  } catch (e) {
    footer("Cancel fallo: " + e.message, true);
    state.cancelInFlight = false;
  }
  // El polling detecta el estado final (cancelled) y limpia la UI.
}

/* ============================ VIRTUAL TABLE ============================== */

// Tabla virtual sin librerias: el backend es la fuente de verdad. Mantiene un
// pool de filas DOM reutilizadas y pide paginas de resultados bajo demanda
// (GET <endpoint>?offset=...&limit=80). Nunca crea un elemento por fila.
//
// Opciones:
//   endpoint   ruta de paginacion (default /api/results)
//   renderRow  (item) -> [addr, val, type]  (personaliza las 3 columnas)
//   onRowClick (item) -> void               (click en una fila)
//   htmlCells  true si renderRow devuelve HTML escapado (para badges)
class VirtualTable {
  constructor(containerId, opts) {
    this.el = $(containerId);
    this.endpoint = (opts && opts.endpoint) || "/api/results";
    this.htmlCells = !!(opts && opts.htmlCells);
    this.renderRow =
      (opts && opts.renderRow) ||
      ((item) => [
        item.address,
        item.value !== undefined ? item.value : "",
        item.type || "",
      ]);
    this.onRowClick = (opts && opts.onRowClick) || null;
    this.rowHeight = opts.rowHeight || 24;
    this.pageSize = opts.pageSize || 80;
    this.overscan = opts.overscan || 8;
    this.visible = Math.ceil(this.el.clientHeight / this.rowHeight) + this.overscan * 2;
    this.total = 0;
    this.rows = [];            // filas en orden (solo ventana)
    this.rowStart = 0;         // indice global de rows[0]
    this.pages = new Map();    // page index -> {offset, rows, done}
    this.pending = new Map();  // page index -> promise
    this.gen = 0;              // descarta respuestas obsoletas
    this.loading = false;
    this.setup();
  }

  setup() {
    // Pool fijo de filas DOM.
    this.pool = [];
    for (let i = 0; i < this.visible; i++) {
      const r = this.makeRow();
      this.el.appendChild(r);
      this.pool.push(r);
    }
    this.onScroll = () => this.render();
    this.el.addEventListener("scroll", this.onScroll);
    window.addEventListener("resize", () => {
      this.visible = Math.ceil(this.el.clientHeight / this.rowHeight) + this.overscan * 2;
      this.ensurePool();
      this.render();
    });
  }

  makeRow() {
    const r = document.createElement("div");
    r.className = "vt-row";
    if (this.onRowClick) {
      r.classList.add("clickable");
      r.addEventListener("click", () => {
        if (this.onRowClick && r._item) this.onRowClick(r._item);
      });
    }
    r.innerHTML =
      '<span class="col-addr"></span>' +
      '<span class="col-val"></span>' +
      '<span class="col-type"></span>';
    return r;
  }

  ensurePool() {
    while (this.pool.length < this.visible) {
      const r = this.makeRow();
      this.el.appendChild(r);
      this.pool.push(r);
    }
  }

  clear() {
    this.total = 0;
    this.rows = [];
    this.rowStart = 0;
    this.pages.clear();
    this.gen++;
    this.render();
  }

  // Invalida toda la cache y vuelve a cargar desde el principio.
  async reload() {
    this.gen++;
    this.pages.clear();
    this.rows = [];
    this.rowStart = 0;
    await this.loadTotal();
    this.el.scrollTop = 0;
    this.render();
  }

  async loadTotal() {
    try {
      const d = await apiGet(this.endpoint + "?offset=0&limit=1");
      this.total = parseInt(d.total || "0", 10);
    } catch (e) {
      this.total = 0;
    }
    if (this.total === 0) this.showEmpty("Sin resultados");
    else this.hideEmpty();
  }

  showEmpty(t) {
    if (!this.emptyEl) {
      this.emptyEl = document.createElement("div");
      this.emptyEl.className = "vt-empty";
      this.el.appendChild(this.emptyEl);
    }
    this.emptyEl.textContent = t;
  }

  hideEmpty() {
    if (this.emptyEl) {
      this.emptyEl.remove();
      this.emptyEl = null;
    }
  }

  // Rellena la ventana visible: actualiza filas del pool y pide paginas.
  render() {
    if (!this.total) return;
    const st = this.el.scrollTop;
    const first = Math.max(0, Math.floor(st / this.rowHeight) - this.overscan);
    const last = Math.min(this.total - 1,
                          first + Math.ceil(this.el.clientHeight / this.rowHeight) +
                            this.overscan * 2);
    const need = last - first + 1;

    // Pool: recolocar y (re)llenar filas reutilizadas.
    for (let i = 0; i < this.pool.length; i++) {
      const gi = first + i;             // indice global
      if (gi > last) {
        this.pool[i].style.display = "none";
        continue;
      }
      const row = this.pool[i];
      row.style.display = "";
      row.style.top = (gi * this.rowHeight) + "px";
      const cells = row.children;
      cells[0].textContent = "";
      cells[1].textContent = "";
      cells[2].textContent = "";
    }

    // Peticiones de paginas para la ventana.
    const pFirst = Math.floor(first / this.pageSize);
    const pLast = Math.floor(last / this.pageSize);
    for (let p = pFirst; p <= pLast; p++) this.requestPage(p);

    // Rellenar desde cache (despues de pedir).
    for (let i = 0; i < this.pool.length; i++) {
      const gi = first + i;
      if (gi > last) continue;
      const row = this.pool[i];
      const cells = row.children;
      const p = Math.floor(gi / this.pageSize);
      const cache = this.pages.get(p);
      if (cache && cache.rows.length) {
        const item = cache.rows[gi - cache.offset];
        if (item) {
          row._item = item;
          const [a, v, t] = this.renderRow(item);
          if (this.htmlCells) {
            cells[0].innerHTML = a !== undefined ? a : "";
            cells[1].innerHTML = v !== undefined ? v : "";
            cells[2].innerHTML = t !== undefined ? t : "";
          } else {
            cells[0].textContent = a !== undefined ? a : "";
            cells[1].textContent = v !== undefined ? v : "";
            cells[2].textContent = t !== undefined ? t : "";
          }
        }
      } else {
        row._item = null;
        cells[0].textContent = "";
        cells[1].textContent = "...";
        cells[2].textContent = "";
      }
    }
  }

  requestPage(p) {
    if (this.pending.has(p) || this.pages.has(p)) return;
    const offset = p * this.pageSize;
    const gen = this.gen;
    const pr = apiGet(this.endpoint + "?offset=" + offset + "&limit=" + this.pageSize)
      .then((d) => {
        if (gen !== this.gen) return; // respuesta obsoleta
        this.pages.set(p, { offset, rows: d.rows || [], done: true });
      })
      .catch(() => { /* se reintentara en el siguiente scroll */ })
      .finally(() => this.pending.delete(p));
    this.pending.set(p, pr);
    // Re-render cuando llegue la pagina (si aun es relevante).
    pr.then(() => { if (gen === this.gen) this.render(); });
  }
}

/* ============================ PESTAÑAS ================================== */

function switchTab(name) {
  document.querySelectorAll(".tab").forEach((t) =>
    t.classList.toggle("active", t.dataset.tab === name));
  document.querySelectorAll(".tabpanel").forEach((p) =>
    p.classList.toggle("active", p.id === "panel-" + name));
  if (name === "table") tblRefresh();
}

/* ============================ MEMORY VIEWER ============================= */

const memState = { addr: 0, len: 256 };

function hexAddr(n) { return "0x" + n.toString(16); }

async function memRead() {
  const s = $("mem-addr").value.trim();
  if (!s) { footer("Introduce una direccion", true); return; }
  let addr;
  try { addr = BigInt(s); } catch (e) {
    footer("Direccion invalida: " + s, true); return;
  }
  const len = parseInt($("mem-len").value || "256", 10);
  if (!(len >= 1 && len <= 4096)) { footer("Length debe estar entre 1 y 4096", true); return; }
  memState.addr = addr;
  memState.len = len;
  await memFetch();
}

async function memFetch() {
  const addr = memState.addr.toString();
  try {
    const d = await apiGet(
      "/api/memory?address=" + addr + "&length=" + memState.len);
    renderMem(d);
    $("mem-addr").value = hexAddr(memState.addr);
  } catch (e) {
    footer("Memory: " + errText(e), true);
  }
}

function renderMem(d) {
  const reg = $("mem-region");
  if (d.region && d.region.start) {
    reg.innerHTML =
      "Region: <span class=\"addr\">" + esc(d.region.start) +
      "</span> - <span class=\"addr\">" + esc(d.region.end) +
      "</span> <span class=\"perms\">[" + esc(d.region.perms) +
      "]</span> <span class=\"path\">" + esc(d.region.path || "") +
      "</span>";
  } else {
    reg.textContent = "Sin region conocida para " + (d.address || "");
  }

  // Hex legible: filas de 16 bytes (address | bytes | ascii).
  const hex = d.hex || "";
  const ascii = d.ascii || "";
  const base = memState.addr;
  let out = "";
  for (let i = 0; i < hex.length; i += 32) {
    const chunkHex = hex.slice(i, i + 32);
    const chunkAsc = ascii.slice(i / 2, i / 2 + 16);
    let spaced = "";
    for (let j = 0; j < chunkHex.length; j += 2)
      spaced += chunkHex.slice(j, j + 2) + " ";
    const rowAddr = hexAddr(base + BigInt(i / 2));
    out += "<div><span class=\"addr\">" + rowAddr +
           "</span>  <span class=\"bytes\">" + spaced.trim() +
           "</span>  <span class=\"ascii\">|" + esc(chunkAsc) + "|</span></div>";
  }
  $("mem-hex").innerHTML = out || "(vacio)";
  footer("Memory: " + (d.region && d.region.start ? d.region.perms : "sin region"));
}

async function memPage(delta) {
  if (!memState.addr) { footer("Primero lee una direccion", true); return; }
  memState.addr = memState.addr + BigInt(delta * memState.len);
  await memFetch();
}

async function memCopyHex() {
  const txt = $("mem-hex").textContent;
  if (!txt) { footer("Nada que copiar", true); return; }
  try {
    await navigator.clipboard.writeText(txt);
    footer("Hex copiado.");
  } catch (e) {
    footer("No se pudo copiar: " + e.message, true);
  }
}

/* ============================ ADDRESS TABLE ============================= */

function tblDetail(html, isErr) {
  const el = $("tbl-detail");
  el.classList.remove("hidden");
  el.innerHTML = isErr
    ? '<span class="err">' + esc(html) + "</span>"
    : html;
}

async function tblRefresh() {
  try {
    const d = await apiGet("/api/table");
    renderTbl(d.entries || []);
  } catch (e) {
    footer("Table: " + e.message, true);
    renderTbl([]);
  }
}

function kindLabel(e) {
  if (!e.pointer) return "absolute";
  const p = e.pointer;
  const pers = p.persistent ? "persistente" : "no persistente";
  return "pointer[" + (p.kind || "ABSOLUTE") + "] " + pers;
}

function addrLabel(e) {
  if (!e.pointer) return esc(e.address);
  const p = e.pointer;
  if (p.module) {
    return esc(p.module) + " +" + esc(p.root_offset || "0x0") +
      (p.offsets && p.offsets.length ? " [" + p.offsets.map((o) => "+" + o).join(" ") + "]" : "");
  }
  return "anon +" + esc(p.root_offset || "0x0");
}

function renderTbl(entries) {
  const body = $("tbl-body");
  body.innerHTML = "";
  if (!entries.length) {
    body.innerHTML = '<tr><td colspan="8" class="ellip">(vacía)</td></tr>';
    return;
  }
  for (const e of entries) {
    const tr = document.createElement("tr");
    const kind = kindLabel(e);
    const isPtr = !!e.pointer;
    tr.innerHTML =
      "<td>" + e.index + "</td>" +
      "<td class=\"addr ellip\" title=\"" + addrLabel(e) + "\">" + addrLabel(e) + "</td>" +
      "<td>" + esc(e.type) + "</td>" +
      "<td class=\"ellip\" title=\"" + esc(e.description) + "\">" + esc(e.description) + "</td>" +
      "<td class=\"enabled-" + (e.enabled ? "y" : "n") + "\">" + (e.enabled ? "si" : "no") + "</td>" +
      "<td class=\"stale-" + (e.stale ? "y" : "n") + "\">" + (e.stale ? "si" : "no") + "</td>" +
      "<td class=\"kind-" + (isPtr ? "module" : "absolute") + "\">" + esc(kind) + "</td>" +
      "<td class=\"actions\"></td>";
    const act = tr.querySelector(".actions");
    act.appendChild(btn("Read", () => tblRead(e)));
    act.appendChild(btn("Set", () => tblSetRow(tr, e)));
    act.appendChild(btn("Toggle", () => tblToggle(e)));
    if (isPtr) act.appendChild(btn("Resolve", () => tblResolve(e)));
    act.appendChild(btn("Remove", () => tblRemove(e)));
    body.appendChild(tr);
  }
}

function btn(label, fn) {
  const b = document.createElement("button");
  b.className = "btn small";
  b.textContent = label;
  b.addEventListener("click", fn);
  return b;
}

async function tblAdd() {
  const addr = $("tbl-addr").value.trim();
  const type = $("tbl-type").value;
  const desc = $("tbl-desc").value.trim();
  if (!addr) { footer("Introduce una direccion para la tabla", true); return; }
  try {
    const d = await apiPost("/api/table/add",
                            { address: addr, type, description: desc });
    footer("Entrada " + d.index + " añadida.");
    $("tbl-addr").value = "";
    $("tbl-desc").value = "";
    await tblRefresh();
  } catch (e) {
    footer("Table add: " + e.message, true);
  }
}

async function tblRead(e) {
  try {
    const d = await apiPost("/api/table/read", { index: e.index });
    tblDetail(
      "[" + d.index + "] " + d.address + " " + d.type +
      " = <b>" + esc(d.value || "?") + "</b>" +
      (d.stale ? " <span class=\"err\">(stale)</span>" : ""));
  } catch (err) {
    tblDetail(err.message, true);
  }
}

function tblSetRow(tr, e) {
  const act = tr.querySelector(".actions");
  act.innerHTML = "";
  act.classList.add("set-row");
  const input = document.createElement("input");
  input.placeholder = "valor " + e.type;
  const ok = btn("OK", async () => {
    const v = input.value.trim();
    if (!v) { footer("Valor vacio", true); return; }
    try {
      const d = await apiPost("/api/table/set", { index: e.index, value: v });
      tblDetail("[" + d.index + "] " + d.address +
                " <span class=\"old\">" + esc(d.old_value || "?") +
                "</span> -> <span class=\"new\">" + esc(d.new_value || "?") +
                "</span> " + (d.verified ? "(verificado)" : "(NO verificado)"));
      await tblRefresh();
    } catch (err) {
      tblDetail(err.message, true);
      await tblRefresh();
    }
  });
  const cancel = btn("X", tblRefresh);
  act.appendChild(input);
  act.appendChild(ok);
  act.appendChild(cancel);
  input.focus();
}

async function tblToggle(e) {
  try {
    const d = await apiPost("/api/table/toggle", { index: e.index });
    footer("Entrada " + d.index + " -> " + (d.enabled ? "ON" : "OFF"));
    await tblRefresh();
  } catch (err) {
    footer("Table toggle: " + err.message, true);
  }
}

async function tblRemove(e) {
  try {
    await apiPost("/api/table/remove", { index: e.index });
    footer("Entrada " + e.index + " eliminada.");
    await tblRefresh();
  } catch (err) {
    footer("Table remove: " + err.message, true);
  }
}

async function tblResolve(e) {
  try {
    const d = await apiPost("/api/pointer/resolve", { index: e.index });
    tblDetail("[" + d.index + "] pointer -> " + d.address +
              " = <b>" + esc(d.value) + "</b> (" + d.type + ")");
  } catch (err) {
    tblDetail("Resolve: " + err.message, true);
  }
}

function validFileName(n) {
  // Nombres simples: sin separadores de ruta ni subidas de directorio.
  return /^[A-Za-z0-9._-]+$/.test(n) &&
         !n.startsWith(".") && !n.includes("..");
}

async function tblSave() {
  const name = $("tbl-file").value.trim();
  if (!validFileName(name)) {
    footer("Nombre de archivo invalido (usa solo letras/numeros/._-)", true);
    return;
  }
  try {
    const d = await apiPost("/api/table/save", { name });
    footer("Guardada en " + d.path);
  } catch (e) {
    footer("Table save: " + e.message, true);
  }
}

async function tblLoad() {
  const name = $("tbl-file").value.trim();
  if (!validFileName(name)) {
    footer("Nombre de archivo invalido (usa solo letras/numeros/._-)", true);
    return;
  }
  try {
    const d = await apiPost("/api/table/load", { name });
    footer("Cargadas " + d.count + " entradas de " + d.path);
    await tblRefresh();
  } catch (e) {
    footer("Table load: " + e.message, true);
  }
}

/* ============================ PATTERN / STRINGS / BYTES ================= */

// Render de una fila dinamica: la API devuelve address/type/length (sin
// contenido); el contenido se lee bajo demanda con /api/memory al seleccionar.
function dynRender(item) {
  return [item.address, item.length !== undefined ? "len " + item.length : "",
          item.type || ""];
}

async function patScan() {
  if (state.jobId) { footer("Ya hay un scan en curso", true); return; }
  const mode = $("pat-mode").value;
  let path, body, onComplete;
  if (mode === "aob") {
    const pat = $("pat-pattern").value.trim();
    if (!pat) { footer("Introduce un patron (p. ej. 48 8B ?? ??)", true); return; }
    path = "/api/pattern";
    body = { pattern: pat };
    onComplete = () => {
      patVt.endpoint = "/api/pattern/results";
      patVt.renderRow = (item) => [item.address, pat, ""];
      $("pat-col-val").textContent = "Match";
      $("pat-col-type").textContent = "";
      patVt.reload();
    };
  } else {
    const text = $("pat-text").value;
    if (!text) { footer("Introduce un texto para el String Scan", true); return; }
    path = "/api/scan/first";
    body = { type: "string", value: text };
    onComplete = () => {
      patVt.endpoint = "/api/results";
      patVt.renderRow = dynRender;
      $("pat-col-val").textContent = "Length";
      $("pat-col-type").textContent = "Type";
      patVt.reload();
    };
  }
  await launchScan(path, body, onComplete);
}

async function patNext() {
  if (state.jobId) { footer("Ya hay un scan en curso", true); return; }
  const mode = $("pat-mode").value;
  const filter = $("pat-filter").value;
  const body = { filter };
  if (filter === "exact") {
    // El backend hereda el tipo dinamico del scan previo (parse_type no
    // acepta "string"/"bytes"); solo enviamos el valor exacto.
    const v = mode === "string" ? $("pat-text").value : $("pat-pattern").value.trim();
    if (!v) { footer("Introduce el valor exacto", true); return; }
    body.value = v;
  }
  const onComplete = () => {
    patVt.endpoint = "/api/results";
    patVt.renderRow = dynRender;
    $("pat-col-val").textContent = "Length";
    $("pat-col-type").textContent = "Type";
    patVt.reload();
  };
  await launchScan("/api/scan/next", body, onComplete);
}

function switchPatMode() {
  const mode = $("pat-mode").value;
  $("pat-pattern-label").classList.toggle("hidden", mode !== "aob");
  $("pat-text-label").classList.toggle("hidden", mode !== "string");
}

/* ============================ POINTERS ================================= */

// Estado del ultimo scan de pointers (para repetirlo facilmente).
const ptrState = { opts: null, selectedIndex: null, tableIndex: null,
                    resolved: null };

// Nombre corto de un modulo (basename) para celdas legibles.
function moduleBase(p) {
  const s = String(p);
  const i = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"));
  return i >= 0 ? s.slice(i + 1) : s;
}

// Render de una fila de pointer results (HTML escapado).
function ptrRender(item) {
  const kind = item.kind === "MODULE" ? "MODULE" : "ABSOLUTE";
  const pers = item.persistent ? "persistente" : "no persistente";
  const badgeKind =
    '<span class="badge ' + (kind === "MODULE" ? "b-module" : "b-absolute") +
    '">' + kind + "</span>";
  const badgePers =
    '<span class="badge ' + (item.persistent ? "b-pers" : "b-nopers") +
    '">' + pers + "</span>";
  // Root legible: MODULE -> base+offset ; ABSOLUTE -> direccion.
  let root;
  if (kind === "MODULE")
    root = esc(moduleBase(item.module || "")) +
           '<span class="muted">+' + esc(item.root_offset || "0x0") + "</span>";
  else
    root = '<span class="addr">' + esc(item.root_offset || item.nodes[0] || "") + "</span>";
  // Cadena: root -> +off0 -> +off1 ... -> TARGET (nodes.back()).
  let chain = root;
  const offs = item.offsets || [];
  for (const o of offs)
    chain += ' <span class="chain-arrow">→</span> <span class="muted">+' + esc(o) + "</span>";
  const nodes = item.nodes || [];
  if (nodes.length)
    chain += ' <span class="chain-arrow">→</span> <span class="addr">' + esc(nodes[nodes.length - 1]) + "</span>";
  return [String(item.index),
          "d" + item.depth + " " + badgeKind + " " + badgePers,
          '<span class="mono">' + chain + "</span>"];
}

async function ptrScan() {
  if (state.jobId) { footer("Ya hay un scan en curso", true); return; }
  const target = $("ptr-target").value.trim();
  if (!target) { footer("Introduce la direccion target", true); return; }
  const opts = {
    target,
    depth: parseInt($("ptr-depth").value || "3", 10),
    max_offset: $("ptr-maxoff").value.trim() || "0x100",
    offset_step: parseInt($("ptr-step").value || "8", 10),
    module_only: $("ptr-module").checked,
    code: $("ptr-code").checked,
    type: $("ptr-vtype").value,
  };
  ptrState.opts = opts;
  ptrState.selectedIndex = null;
  ptrState.tableIndex = null;
  ptrState.resolved = null;
  $("ptr-detail").classList.add("hidden");
  try {
    const d = await apiPost("/api/pointer/scan", opts);
    if (d.job_id) {
      startPolling(d.job_id, () => {
        ptrVt.endpoint = "/api/pointer/results";
        ptrVt.renderRow = ptrRender;
        ptrVt.reload();
      });
    } else {
      footer("Pointer scan: sin job_id", true);
    }
  } catch (e) {
    footer((e.code === "busy" ? "Servidor ocupado: " : "Pointer scan: ") +
           e.message, true);
  }
}

// Render del detalle de una cadena (sin tocar el estado de resolucion:
// Add/Resolve conservan tableIndex/resolved entre re-renders).
function ptrRenderDetail(item) {
  const el = $("ptr-detail");
  el.classList.remove("hidden");
  const kind = item.kind === "MODULE" ? "MODULE" : "ABSOLUTE";
  const pers = item.persistent ? "persistente" : "no persistente";
  const nodes = item.nodes || [];
  const offs = item.offsets || [];
  let rootInfo;
  if (kind === "MODULE")
    rootInfo = "modulo <b>" + esc(item.module || "") +
               "</b> +<b>" + esc(item.root_offset || "0x0") + "</b>";
  else
    rootInfo = "raiz <b>" + esc(item.root_offset || nodes[0] || "") + "</b>";
  let html =
    '<div class="sel-line">' +
    "<span class=\"badge " + (kind === "MODULE" ? "b-module" : "b-absolute") +
    '">' + kind + "</span> " + pers +
    " &nbsp;depth <b>" + item.depth + "</b>" +
    " &nbsp;type <b>" + esc(item.value_type || "") + "</b></div>" +
    '<div class="sel-line">' + rootInfo + "</div>" +
    '<div class="sel-line mono">' +
    'offsets: ' + (offs.length
                   ? offs.map((o) => "+" + esc(o)).join(" → ")
                   : "(ninguno)") +
    "</div>" +
    (nodes.length
     ? '<div class="sel-line mono">target: <span class="addr">' +
       esc(nodes[nodes.length - 1]) + "</span></div>"
     : "");
  if (ptrState.resolved)
    html += '<div class="sel-line">resuelto: <span class="addr">' +
            esc(ptrState.resolved.address) + "</span> = <b>" +
            esc(ptrState.resolved.value) + "</b> (" +
            esc(ptrState.resolved.type) + ")</div>";
  html += ' <button class="btn small primary" id="ptr-add-btn">Add to Address Table</button>';
  html += ' <button class="btn small" id="ptr-resolve-btn">Resolve</button>';
  html += ' <button class="btn small" id="ptr-mem-btn">Open in Memory</button>';
  el.innerHTML = html;
  $("ptr-add-btn").addEventListener("click", () => ptrAdd(item));
  $("ptr-resolve-btn").addEventListener("click", () => ptrResolve(item));
  $("ptr-mem-btn").addEventListener("click", () => {
    const a = ptrState.resolved && ptrState.resolved.address;
    if (a) { openInMemoryViewer(a); return; }
    footer("Primero resuelve la cadena", true);
  });
}

async function ptrAdd(item) {
  const desc = "pointer";
  try {
    const d = await apiPost("/api/pointer/add",
                            { chain_index: item.index, description: desc });
    ptrState.tableIndex = d.table_index;
    footer("Cadena " + item.index + " añadida a la tabla (id " + d.table_index + ").");
    // Refresca la tabla y re-renderiza el detalle SIN perder tableIndex.
    await tblRefresh();
    ptrRenderDetail(item);
  } catch (e) {
    footer("Pointer add: " + e.message, true);
  }
}

async function ptrResolve(item) {
  let idx = ptrState.tableIndex;
  if (idx === null) {
    footer("Primero anade la cadena a la Address Table", true);
    return;
  }
  try {
    const d = await apiPost("/api/pointer/resolve", { index: idx });
    ptrState.resolved = d;
    footer("Resuelto: " + d.address + " = " + d.value + " (" + d.type + ")");
    ptrRenderDetail(item);
  } catch (e) {
    footer("Pointer resolve: " + e.message, true);
  }
}

// Seleccion de una cadena: solo al CAMBIAR de cadena se resetea el estado de
// resolucion; la cadena ya anadida conserva tableIndex/resolved.
function ptrSelect(item) {
  if (ptrState.selectedIndex !== item.index) {
    ptrState.selectedIndex = item.index;
    ptrState.tableIndex = null;
    ptrState.resolved = null;
  }
  ptrRenderDetail(item);
}

/* ============================ SELECCION DE FILA ========================= */

// Muestra el detalle de una fila seleccionada y ofrece abrirla en el viewer.
function showRowDetail(item, boxId, isDynamic) {
  const el = $(boxId);
  el.classList.remove("hidden");
  let html = '<span class="addr">' + esc(item.address) + "</span>";
  if (item.type) html += " <span class=\"muted\">" + esc(item.type) + "</span>";
  if (isDynamic && item.length !== undefined)
    html += " <span class=\"muted\">len " + item.length + "</span>";
  if (item.value !== undefined && item.value !== "")
    html += " = <b>" + esc(item.value) + "</b>";
  html += ' <button class="btn small" id="' + boxId + '-open">Open in Memory Viewer</button>';
  el.innerHTML = html;
  const btn = el.querySelector("#" + boxId + "-open");
  if (btn) btn.addEventListener("click", () => openInMemoryViewer(item.address));
}

function openInMemoryViewer(addr) {
  switchTab("memory");
  $("mem-addr").value = addr;
  memRead();
}

/* ============================ INIT ======================================= */

const vt = new VirtualTable("vt", {
  rowHeight: 24, pageSize: 80, overscan: 8,
  onRowClick: (item) => showRowDetail(item, "scan-detail", false),
});
const patVt = new VirtualTable("vt-pattern", {
  rowHeight: 24, pageSize: 80, overscan: 8, endpoint: "/api/pattern/results",
  onRowClick: (item) => showRowDetail(item, "pat-detail", true),
});
const ptrVt = new VirtualTable("vt-pointer", {
  rowHeight: 28, pageSize: 50, overscan: 6, endpoint: "/api/pointer/results",
  htmlCells: true,
  onRowClick: (item) => ptrSelect(item),
});

document.addEventListener("DOMContentLoaded", () => {
  $("btn-refresh").addEventListener("click", loadProcesses);
  $("btn-detach").addEventListener("click", detach);
  $("btn-first").addEventListener("click", () => firstScan(false));
  $("btn-unknown").addEventListener("click", () => firstScan(true));
  $("btn-next").addEventListener("click", nextScan);
  $("btn-cancel").addEventListener("click", cancelJob);

  // Pointers.
  $("btn-ptr-scan").addEventListener("click", ptrScan);
  $("ptr-target").addEventListener("keydown", (e) => {
    if (e.key === "Enter") ptrScan();
  });

  // Pattern / Strings / Bytes.
  $("btn-pat-scan").addEventListener("click", patScan);
  $("btn-pat-next").addEventListener("click", patNext);
  $("pat-mode").addEventListener("change", switchPatMode);
  $("pat-pattern").addEventListener("keydown", (e) => {
    if (e.key === "Enter") patScan();
  });
  $("pat-text").addEventListener("keydown", (e) => {
    if (e.key === "Enter") patScan();
  });

  // Pestañas.
  document.querySelectorAll(".tab").forEach((t) =>
    t.addEventListener("click", () => switchTab(t.dataset.tab)));

  // Memory Viewer.
  $("btn-mem-read").addEventListener("click", memRead);
  $("btn-mem-prev").addEventListener("click", () => memPage(-1));
  $("btn-mem-next").addEventListener("click", () => memPage(1));
  $("btn-mem-refresh").addEventListener("click", memFetch);
  $("btn-mem-copy").addEventListener("click", memCopyHex);
  $("mem-addr").addEventListener("keydown", (e) => {
    if (e.key === "Enter") memRead();
  });

  // Address Table.
  $("btn-tbl-add").addEventListener("click", tblAdd);
  $("btn-tbl-refresh").addEventListener("click", tblRefresh);
  $("btn-tbl-save").addEventListener("click", tblSave);
  $("btn-tbl-load").addEventListener("click", tblLoad);
  $("tbl-addr").addEventListener("keydown", (e) => {
    if (e.key === "Enter") tblAdd();
  });

  // Click en proceso -> attach directo.
  $("proc-list").addEventListener("click", (e) => {
    const row = e.target.closest(".proc-row");
    if (row) {
      selectProcess(row.querySelector(".p-pid").textContent, row);
      attachSelected();
    }
  });

  refreshStatus();
  loadProcesses();
  vt.loadTotal().then(() => vt.render());
});
