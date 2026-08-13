# MemoryTool

Analizador de memoria y herramientas de depuración para Linux/ChromeOS,
inspirado conceptualmente en Cheat Engine pero con **motor 100 % propio**,
escrito en C++ desde cero, sin dependencias externas y pensado para correr
bien en un Chromebook con recursos limitados.

Filosofía (en orden de prioridad):

1. **Funcionamiento real** — un escáner real, no una simulación.
2. **Seguridad y permisos** — respeta `ptrace_scope`, Yama y el aislamiento
   de ChromeOS. No intenta evadir el kernel: trabaja con procesos propios o
   que concedan permiso explícitamente.
3. **Rendimiento y bajo consumo** — CLI, sin dependencias pesadas.
4. **Modularidad** — backends separados por capa del sistema.
5. **Interfaz** — primero terminal; una GUI ligera se evaluará más adelante.

## Estado actual

Completado el **motor básico de escaneo real** (etapas 1 a 8 del plan):

| Función | Estado |
|---|---|
| Programa de prueba `objetivo` | ✅ |
| Process Manager (`list`) | ✅ |
| Regiones `/proc/PID/maps` (`maps`) | ✅ |
| Lectura autorizada `/proc/PID/mem` (ptrace) | ✅ |
| First Scan (int32) | ✅ |
| Next Scan progresivo | ✅ |
| Tipos int8/16/64, unsigned, float, double | ✅ (ETAPA 9–10) |
| Filtros changed/unchanged/increased/decreased, comparadores | ✅ (ETAPA 11) |
| Escaneo `unknown` (sin conocer el valor) | ✅ |
| Visor hexadecimal (`view`) | ✅ (ETAPA 13, versión mínima) |
| Escritura de memoria autorizada (`set`) | ✅ (ETAPA 17, versión mínima) |
| Pattern/AOB scanner (wildcards) | ✅ (ETAPA 14) |
| Strings y bytes (`first "…" string` / `first <hex> bytes` con wildcards) | ✅ |
| Address Table (`table`, persistente) | ✅ (ETAPA 16, primera versión) |
| Pointer Scanner (core + CLI) | ✅ (ETAPA 15, motor + comandos) |
| Pointer V2: offsets + resolución dinámica | ✅ (scan con offsets, `pointer resolve`, `table read/set` dinámicos) |
| GUI | ⏳ siguiente etapa |

## Requisitos

- Linux con kernel 5.x+ (probado en Debian 12 / kernel 6.6 / ChromeOS Crostini)
- `g++` con soporte C++17
- `cmake` opcional (si no está instalado, usa `./build.sh`)
- Sin root, sin dependencias externas

## Compilar

```bash
# Opcion A: CMake (si esta instalado)
cmake -S . -B build
cmake --build build

# Opcion B: script ligero con g++ directo (sin CMake)
./build.sh
```

Produce los binarios en `build/`:

- `build/memorytool` — la herramienta (escáner)
- `build/objetivo` — el programa de prueba del escáner de valores
- `build/pointer_test` — el programa de prueba del Pointer Scanner
- `build/pointer_driver` — conductor de prueba del Pointer Scanner (tests)
- `build/pointer_offset_test` — programa de prueba del Pointer Scanner V2 (cadena con offsets, raíz módulo-relative)
- `build/pointer_resolve_driver` — conductor de prueba de la resolución V2 (tests)

## Guía rápida

### 1. Lanzar el programa de prueba

En una terminal:

```bash
./build/objetivo
```

Verás algo así:

```
PID: 1234
Direcciones (para verificar contra el escaner):
  dinero     (int32) : 0x7f1234567890
  ...
Comandos: <numero> dinero | + o - incrementar/decrementar | r reset | n nivel++ | v <float> velocidad | q salir
PID: 1234 | Dinero: 12345 | Nivel: 7 | Velocidad: 1.50 | Puntuacion: 100000
...
```

Mientras se ejecuta, puedes cambiar `dinero` tecleando un número y Enter.

### 2. Escanear

En otra terminal:

```bash
./build/memorytool 1234
```

```text
mt(1234)> first 12345
First Scan: 1 coincidencias (int32 = 12345)
mt(1234)> results
[   0] 0x7f1234567890 = 12345  (0x00003039) (int32)
```

Ahora cambia el valor en la terminal de `objetivo` (teclea `15000`):

```text
mt(1234)> next 15000
Next Scan: 1 coincidencias
```

También funciona sin conocer el valor:

```text
mt(1234)> first unknown
Escaneo 'unknown' completado (N posiciones legibles).
# (cambia el valor en 'objetivo')
mt(1234)> next changed
Next Scan: M coincidencias
mt(1234)> next increased
Next Scan: K coincidencias
mt(1234)> next 15000
Next Scan: 1 coincidencias
```

### 3. Inspeccionar y modificar

```text
mt(1234)> view 0x7f1234567890 32     # visor hexadecimal
mt(1234)> info 0x7f1234567890        # region, permisos, archivo
mt(1234)> set 0x7f1234567890 99999   # escribe en memoria (si la region es rw-)
```

### Referencia de comandos

```
list                                 Listar procesos
attach <pid|nombre>                  Seleccionar proceso objetivo
detach                               Quitar proceso objetivo
maps [pid]                           Mostrar regiones de memoria
first <valor> [tipo]                 Primer escaneo (valor exacto)
first unknown [tipo]                 Primer escaneo (valor desconocido)
first "<texto>" string               Buscar un texto (bytes ASCII)
first <hex...> bytes|pattern         Buscar bytes con wildcards ??
next <valor> [tipo]                  Refinar resultados (igual)
next changed|unchanged|increased|decreased
next >|<|>=|<=|!= <valor> [tipo]     Refinar por comparación
next "<texto>" string | <hex...> bytes|pattern   Refinar un escaneo dinámico (patrón nuevo)
next changed|unchanged               (string/bytes: cambio exacto)
count                                Número de coincidencias
pattern <bytes>                      Buscar secuencia de bytes (AOB)
                                     Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0
results [n]                          Mostrar las primeras n coincidencias
view <direccion> [len]               Visor hexadecimal
set <direccion> <valor> [tipo]       Escribir valor en memoria
info <direccion>                     Información de la región
table                                Listar entradas de la tabla
table add <dir> [tipo] [desc]        Añadir dirección manualmente
table add-result <idx> [desc]        Añadir entrada desde 'results'
table read [idx]                     Leer/actualizar el valor actual
table set <idx> <valor>              Escribir un nuevo valor (tipo de la entrada)
table toggle <idx> | remove <idx>    Activar-desactivar / eliminar
table clear | save <f> | load <f>    Vaciar / guardar / cargar
pointer scan <dir> [depth=N] [max_offset=X] [offset_step=S] [module-only] [code] [type=T]
                                     Buscar cadenas de punteros hacia una direccion (con offsets)
pointer results [n]                  Mostrar cadenas del ultimo escaneo
pointer chains [n]                   Alias de 'pointer results'
pointer add <idx>                    Anadir la cadena (PointerChainRef) a la tabla
pointer resolve <idx>                Resolver una entrada pointer a su direccion actual
help | quit
```

Tipos: `i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 ptr` (alias: `int`, `float`,
`double`, `byte`, `pointer`, ...). Los valores numéricos se escriben en
decimal o con prefijo `0x` para hexadecimal; las direcciones se aceptan con o
sin `0x`. El hexadecimal es el **valor numérico** del texto
(`0xFFFFFFFF` = 4294967295); los negativos se escriben con `-`
(`-0x1` = -1). Un valor fuera del rango del tipo se rechaza con un error
(no se satura silenciosamente).

## Strings y bytes (escaneo dinámico)

El escáner First/Next soporta valores de **longitud variable** (string y
patrones de bytes) sin tocar el camino numérico: los candidatos dinámicos
solo guardan `address + longitud` (más los bytes de las posiciones `?`), y el
patrón vive una sola vez en la especificación del escaneo (nada de copias por
candidato).

```text
mt(1234)> first "hola memorytool" string
First Scan: 1 coincidencias (string, len 15)
mt(1234)> results
[   0] 0x000055a92c9ab1e0  len 15  "hola memorytool" (string)

mt(1234)> first 48 8B 05 ?? ?? bytes
First Scan: 2 coincidencias (bytes, len 5)
mt(1234)> next changed                       # bytes actuales != anteriores
mt(1234)> next "mundo memorytool" string     # patrón nuevo sobre los candidatos
```

- **Encoding**: ASCII puro (los bytes del texto tal cual se almacenan en
  memoria; sin conversión Unicode).
- **Límite**: 4096 bytes (`kMaxDynamicLength`), compartido con el comando
  `pattern`; un patrón mayor se rechaza.
- **Wildcards**: en `bytes` se reutiliza el parser del Pattern/AOB
  (`??` = cualquier byte); `string` no admite wildcards.
- **`next` dinámico**: `changed`/`unchanged` comparan los bytes actuales con
  los del escaneo anterior (incluidas las posiciones `?`); un patrón nuevo
  exige coincidencia exacta. Los filtros numéricos (`>`, `<`, `increased`…)
  no aplican a string/bytes y se rechazan con un mensaje claro.

## Address Table

La tabla guarda direcciones encontradas por el escáner (o añadidas a mano)
para seguirlas observando y modificando durante la sesión. Cada entrada
contiene: **address**, **type**, **description** y **enabled**. El valor
actual **no** se almacena: se relee de memoria cuando hace falta.

```text
mt(1234)> first 12345
mt(1234)> results
[   0] 0x7f1234567890 = 12345  (0x00003039) (int32)
mt(1234)> table add-result 0 "dinero"
Entrada 0 anadida desde results[0]: 0x7f1234567890 (i32)
mt(1234)> table
ID   Address              Type      Description              Enabled
0    0x00007f1234567890   i32       dinero                   yes
mt(1234)> table read 0
[0] 0x00007f1234567890 = 12345  (0x00003039) (i32)
mt(1234)> table set 0 99999
Actual: 0x7f1234567890 = 12345  (0x00003039)
Nuevo:  0x7f1234567890 = 99999  (0x0001869f) (verificado)
mt(1234)> table save tabla.txt
mt(1234)> table load tabla.txt
```

- `table read`/`table set` usan las mismas garantías de `Session` (attach →
  operación → detach) y el mismo mecanismo de escritura que `set`.
- Las entradas desactivadas (`table toggle`) se conservan pero no se leen ni
  escriben.
- Al cambiar de proceso (`attach <nuevo>`), las entradas se marcan como
  `(stale)`: las direcciones absolutas ya no son fiables. No se borran; una
  relectura exitosa las deja como verificadas.
- `detach` conserva la tabla; las operaciones de memoria piden proceso.

### Formato de archivo (texto, v1)

Una entrada por línea; `#` = comentario. La dirección se guarda como
hexadecimal de 16 dígitos (64 bits seguros); la descripción va entre
comillas dobles con escapes `\"`, `\\` y `\n`:

```
# MemoryTool Address Table v1
0x00007f1234567890 i32 1 "dinero"
0x000055a92c9ab29e float 0 "velocidad del coche"
```

Columnas: `address type enabled description`. `enabled` es `1`/`0`.

### Limitaciones

- Las entradas **absolutas** (v1) dependen de ASLR y solo son válidas mientras
  el proceso (y su mapeo) siga siendo el mismo; al cambiar de proceso se
  marcan `(stale)`. Las entradas **dinámicas** (v2, kind `pointer`), en
  cambio, guardan una cadena resoluble (`module + file offset` + offsets) y
  `table read`/`table set` la resuelven de nuevo en cada operación, así que
  sobreviven a un reinicio con ASLR (si el binario no cambia).

## Pointer Scanner (core)

El **motor** del Pointer Scanner está implementado (`src/pointer.h/.cpp`):
busca cadenas de punteros que conducen hacia una dirección objetivo
(level-scan inverso, profundidad configurable). Los comandos de CLI
(`pointer scan`, `pointer results`, ...) llegarán en la siguiente etapa;
también se integrará con la Address Table para resolver direcciones
absolutas frente a ASLR.

Concepto:

```
Node3 -> Node2 -> Node1 -> TARGET
(almacena la direccion de Node2, de Node1 y de TARGET respectivamente)
```

- **Nivel 1**: direcciones cuyo valor de 8 bytes es el TARGET.
- **Nivel 2**: direcciones cuyo valor es una de las direcciones del nivel 1.
- ... hasta `max_depth` (por defecto 3).

Cada nivel conserva la relación `source -> target` (`PointerEdge`) y las
cadenas se construyen **incrementalmente** (frontera), sin volver a leer
memoria para reconstruirlas. El control de ciclos es **por cadena**: al
extender una cadena se descarta un source que ya esté dentro de esa misma
cadena, sin `visited` global, para no perder cadenas válidas que comparten
nodos (`X->Y->T` y `Z->Y->T` conviven).

Regiones fuente por defecto: `[heap]`, `[stack]`, anónimas rw y data/BSS con
respaldo de archivo (incluida la del ejecutable principal). Se excluyen
code, `[vdso]`/`[vsyscall]` y archivos de solo lectura; `include_code`
opcionalmente añade las ejecutables. Límites: `max_edges_per_level`
(500 000, truncado conservando lo ya encontrado) y `max_chains` (100 000).
El escaneo usa `for_each_window` con `stride=8` (solo posiciones alineadas
a 8 bytes) y un `FlatHashSet` plano (sin `std::unordered_set`) para
mantener el consumo bajo: ~8 MiB por nivel más buffer en el peor caso.

El módulo **no** hace attach/detach: recibe un `Memory` ya abierto (la
Session/CLI se encarga del ciclo ptrace).

### Comandos CLI (integración con Session y Address Table)

```
pointer scan <direccion> [depth=N] [max_offset=X] [offset_step=S] [module-only] [code] [type=T]
pointer results [n]
pointer chains [n]        # alias de results
pointer add <indice>
pointer resolve <indice>
```

- `pointer scan` valida que la dirección pertenezca a una región legible y
  hace **un solo attach** alrededor de todo el escaneo
  (`Session::with_memory`). Opciones: `depth=` entre 1 y 7 (por defecto 3),
  `code` para incluir regiones ejecutables, `module-only` para quedarse solo
  con cadenas cuya raíz es de módulo (persistente), y `type=T` para fijar el
  tipo del valor final (por defecto: el tipo de sesión). Muestra un resumen
  (target, depth, levels, chains, ventana de offsets) y avisa si se truncó
  por límite de aristas o de cadenas.
- `pointer results` muestra las primeras cadenas (10 por defecto) del último
  escaneo; las cadenas se reconstruyen sin volver a escanear (frontera
  incremental) e intercalan sus offsets (`0xA -> +0x20 -> 0xB -> +0x18 ->
  TARGET`). Cada cadena muestra además su raíz: `[root MODULE ...]
  (persistente)` o `[root ABSOLUTE ...] (no persistente)`.
- `pointer add <idx>` guarda en la Address Table una **cadena dinámica**
  (`PointerChainRef`): raíz (`MODULE` = pathname + offset de archivo, o
  `ABSOLUTE` como fallback no persistente) + offsets + tipo del valor final.
  Si la raíz está en heap/stack/anónima se marca como no persistente.
- `pointer resolve <idx>` resuelve una entrada dinámica de la tabla contra
  el proceso actual y muestra la dirección resultante y el valor leído
  (errores explícitos: módulo no encontrado, offset fuera del módulo,
  cadena rota, valor no legible).

```text
mt(1234)> pointer scan 0x7f1234567890 depth=3
Pointer Scan:
target: 0x00007f1234567890
depth: 3
levels: 3
chains: 427
offsets: 0x0..0x100 (step 0x8)
mt(1234)> pointer results 2
[0] depth 3:
  [root MODULE /home/u/MemoryTool/build/pointer_offset_test +0x4110] (persistente)
  0x000055a92c9ab200 -> +0x20 -> 0x000055a92c9ab1e0 -> +0x18 -> 0x00007f1234567890
[1] depth 1:
  [root ABSOLUTE 0x00007ffd12345678] (no persistente)
  0x00007ffd12345678 -> +0x18 -> 0x00007f1234567890
mt(1234)> pointer add 0
Entrada 0 anadida desde la cadena 0:
  pointer[module]  (persistente)
  tipo: int32
  0x000055a92c9ab200 -> +0x20 -> 0x000055a92c9ab1e0 -> +0x18 -> 0x00007f1234567890
mt(1234)> pointer resolve 0
Entrada 0 resuelta: 0x00007f1234567890 = 4242 (int32)
```

**Ventana de offsets:** al leer un puntero `V` en `source` se comprueba
`V + o` para cada `o = 0, offset_step, ..., max_offset` (por defecto
`max_offset=0x100`, `offset_step=8`). `max_offset=0` es exactamente el
comportamiento V1 (cadenas directas). El offset se aplica **después** del
dereference y se guarda en la arista y en la cadena; no tiene nada que ver
con la localización persistente de la raíz (que es `module + file offset` y
se calcula después de encontrar la cadena).

### Pointer V2: cadenas persistentes y resolución (infraestructura)

La FASE 3 añade la **representación persistente** de una cadena
(`PointerChainRef`) y su **resolver**, para que una cadena siga siendo útil
cuando el proceso cambia de layout (ASLR):

```
PointerChainRef = base (module + file offset | absoluta) + offsets + value_type

root (module + 0x123456)
  ↓ dereference
+0x20
  ↓ dereference
+0x18
  ↓
valor final (según value_type)
```

- **`PointerBase`** localiza el *primer* puntero: `MODULE` (pathname exacto
de `/proc/PID/maps` + offset de archivo) o `ABSOLUTE` (fallback no
persistente). Heap/stack/anónimas **no** se fingen como persistentes: una
raíz ahí se guarda como `ABSOLUTE`.
- **`PointerChainRef.offsets`** son los desplazamientos *después de cada*
*dereference* (el último localiza el valor final; no se dereferencia). La
localización de la raíz (`module + file offset`) es independiente y se
calcula después de encontrar la cadena.
- **`value_type`** es el tipo del **valor final** (i32, f32, ...), separado
del *kind* `pointer` (que solo describe el mecanismo de resolución).
- **`PointerResolver`** (`src/pointer_resolver.h/.cpp`) convierte la raíz a
una dirección absoluta actual y sigue la cadena leyendo punteros de 8
bytes. No hace attach/detach (recibe `Memory` y regiones abiertas), no
cachea direcciones entre procesos y devuelve errores explícitos: módulo no
encontrado, offset fuera del módulo, puntero no legible, cadena rota, valor
final no legible.

**Formato de archivo v2** (compatible con v1: las líneas `0x...` siguen
igual; una línea que empieza por `pointer` es una cadena dinámica):

```
pointer type=int32 module=/home/u/MemoryTool/build/pointer_offset_test root=0x4110 steps=0x20,0x18 enabled=1 "player health"
```

- `type` = tipo del **valor final** (nunca `pointer`).
- `module` = pathname exacto de `/proc/PID/maps`; `root` = offset de archivo
de la raíz (para bases `MODULE`) o dirección absoluta (para `ABSOLUTE`).
- `steps` = offsets post-deref separados por coma (vacío si no hay derefs).
- No se persisten `stale`, el valor actual ni la dirección resuelta temporal.

### FASE 4: uso completo (scan con offsets, resolve y tabla dinámica)

La FASE 4 activa todo el flujo V2 en la CLI:

- **`pointer scan` con offsets**: `max_offset` (0..0x10000, por defecto
  0x100), `offset_step` (> 0, por defecto 8), `module-only` y `type=`.
  `max_offset=0` reproduce el comportamiento V1.
- **`pointer add` V2**: guarda la `PointerChainRef` completa (raíz
  `MODULE`/`ABSOLUTE` + offsets + tipo final), indicando si es persistente.
- **`pointer resolve <idx>`**: resuelve una entrada dinámica de la tabla en
  el proceso actual (un solo attach).
- **`table read <idx>` / `table set <idx> <valor>`** sobre una entrada
  dinámica: resuelven la cadena en **cada** operación (nunca se guarda la
  dirección resuelta; esto es lo que hace que la entrada sobreviva a ASLR) y
  leen/escriben el valor final con el mismo mecanismo que `set`/`table set`
  absolutos (`write_value_at`, sin duplicar lógica).

**MODULE vs ABSOLUTE (ASLR):**

- `MODULE` (raíz en un mapeo con pathname, p. ej. `.data`/`.bss` del
  ejecutable o de una librería) guarda `pathname + file offset` y **puede
  resolverse de nuevo tras reiniciar el proceso** (si el binario no cambia:
  recompilar puede invalidar el offset).
- `ABSOLUTE` (heap/stack/anónimas) guarda la dirección cruda: es
  informativa pero **no sobrevive a ASLR**; no se finge persistencia.

Ejemplo del flujo completo (probado en la E2E): `pointer scan` → `pointer
results` → `pointer add 0` → `pointer resolve 0` → `table set 0 9001` →
`table save` → reiniciar el proceso → `attach` → `table load` → `table read
0` devuelve una dirección **nueva** (ASLR) con el valor correcto.

### Programa de prueba `pointer_test`

`build/pointer_test` genera deliberadamente la cadena
`Node3 -> Node2 -> Node1 -> TARGET` y un ciclo controlado (`CycleA <->
CycleB`), muestra su PID y las direcciones (en `0x%016llx`), y concede
ptrace explícitamente (`PR_SET_PTRACER`) igual que `objetivo`. Es el
objetivo controlado del Pointer Scanner.

`build/pointer_driver` es un conductor de prueba (no es la CLI): adjunta un
proceso, ejecuta `pointer_scan` con las opciones dadas y vuelca resumen y
cadenas en texto plano:

```bash
./build/pointer_driver <pid> <target_hex> [depth] [max_edges] [max_chains]
```

## Permisos y seguridad (importante)

Este equipo tiene `ptrace_scope = 1` (Yama). Eso significa que un proceso
solo puede trazar (y por tanto leer la memoria de) a:

- sus **hijos directos**, o
- procesos que le **concedan permiso explícitamente** con `PR_SET_PTRACER`.

`MemoryTool` respeta ese mecanismo:

- Usa `PTRACE_SEIZE`/`PTRACE_ATTACH` + `/proc/PID/mem`. Si el kernel niega
  el permiso, informa del error y no insiste.
- Nuestro programa de prueba `objetivo` llama a
  `prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)`: es una **concesión explícita
  y documentada** del propio proceso de prueba, no una evasión.
- **No** se puede acceder (sin root) a procesos de otros usuarios ni a
  procesos del sistema (PID 1, Chrome, etc.). `memorytool list` marca como
  `[no accesible]` los procesos que no comparten nuestro UID.
- La escritura solo se permite en regiones con permiso `w` (se comprueba
  contra `/proc/PID/maps` antes de escribir).

### Capas de aislamiento de ChromeOS

```
ChromeOS
 └── Crostini/Linux   <-- MemoryTool Core (LinuxBackend)
      └── Android     <-- aislado: requiere interfaces de depuración (futuro)
      └── Chrome      <-- aislado: usar APIs de inspección (futuro)
```

No se asume acceso a la memoria de Chrome ni de Android desde Crostini. Esos
módulos (BrowserInspector, AndroidBackend) se diseñarán con las interfaces
de depuración permitidas en fases posteriores.

## Arquitectura

```
MemoryTool/
├── src/
│   ├── main.cpp        # CLI interactiva (REPL) y subcomandos
│   ├── types.h         # Tipos de datos y comparación de valores
│   ├── process.h/.cpp  # Process Manager (listado desde /proc)
│   ├── memory.h/.cpp   # Regiones /maps + acceso /mem con ptrace
│   ├── scanner.h/.cpp  # Motor First/Next Scan con filtros
│   ├── pattern.h/.cpp  # Pattern/AOB scanner (wildcards ??)
│   ├── address_table.h/.cpp  # Address Table (almacenamiento + save/load)
│   ├── session.h/.cpp  # Estado de sesión (proceso, escáner, tabla)
│   ├── command.h/.cpp  # Capa de comandos (dispatcher + handlers)
│   ├── pointer.h/.cpp  # Pointer Scanner (level-scan inverso) + refs V2
│   ├── pointer_resolver.h/.cpp  # Resolución de cadenas persistentes (V2)
│   └── chunk.h         # Recorrido por bloques con solapamiento (y stride)
├── tests/
│   ├── objetivo.cpp    # Programa de prueba (variable conocida)
│   ├── pointer_test.cpp    # Programa de prueba (cadenas de punteros)
│   ├── pointer_driver.cpp  # Conductor de prueba del Pointer Scanner
│   ├── pointer_offset_test.cpp  # Programa de prueba V2 (offsets, raíz módulo)
│   ├── pointer_resolve_driver.cpp  # Conductor de prueba del resolver V2
│   ├── e2e.sh          # Prueba de extremo a extremo (proceso real)
│   ├── test_types.cpp  # Tests unitarios de src/types.h
│   ├── test_memory.cpp # Tests unitarios de parse_maps_line/region_at
│   ├── test_address_table.cpp  # Tests unitarios de AddressTable
│   ├── test_pointer.cpp # Tests unitarios del Pointer Scanner (parte pura)
│   ├── test_pointer_cmd.cpp  # Tests de la integracion pointer + CLI + tabla
│   ├── test_pointer_v2.cpp  # Tests de la infraestructura V2 (bases, resolver, formatos)
│   ├── test_dynamic.cpp # Tests de string/bytes (patrones, wildcards, límites, overlap)
│   └── unit_tests.sh   # Compila y ejecuta los tests unitarios
├── CMakeLists.txt
├── build.sh            # Compilación sin CMake (g++ directo)
└── README.md
```

Detalles de diseño:

- El escáner trabaja con **bytes y direcciones absolutas**; la interpretación
  del valor (int con signo, unsigned, float, double) se hace bajo demanda
  según el tipo. Por eso añadir tipos es barato.
- El **tipo del `next` numérico debe coincidir con el del `first`**: los
  valores y filtros se interpretan con el tipo original del escaneo; un tipo
  distinto se rechaza con un mensaje claro en lugar de reinterpretar
  silenciosamente los candidatos.
- `first_scan` lee la memoria **por bloques** de 4 MiB con solapamiento, y
  guarda cada posición de byte que coincide (búsqueda no alineada, como un
  escáner real).
- `next_scan` re-lee por bloques en lugar de un `pread` por dirección, así
  que `first unknown` + `next changed` es rápido aunque haya millones de
  candidatos. Cada candidato guarda su valor anterior para los filtros.
- Cada comando hace `attach → operación → detach`: el proceso objetivo se
  detiene brevemente solo durante la lectura/escritura y sigue su curso.
- Límite de seguridad de 20 M candidatos (≈ 320 MiB) con aviso a partir de
  10 M, para no agotar la RAM del Chromebook. Un `first unknown` sobre
  procesos grandes (navegadores, IDEs) puede truncarse.

## Pruebas

### Tests unitarios (rápidos, sin proceso externo)

```bash
bash tests/unit_tests.sh
```

Compila y ejecuta `build/test_types` (parseo de valores, comparación,
`value_from_bytes`, tipos), `build/test_memory` (parseo de líneas de
`/proc/PID/maps` con strings simulados y selección de región por dirección)
y `build/test_address_table` (add/remove/clear/get, enabled, save/load con
round-trip, descripciones con espacios y comillas, tipos, direcciones de 64
bits) y `build/test_pointer` (clasificación y selección de regiones,
`FlatHashSet`, y reconstrucción de cadenas con datos sintéticos:
profundidad 1/2/3, nodos compartidos, ciclos por cadena, límites)`build/test_pointer_cmd` (parsing/validación de `pointer scan` —depth y
opciones—, descripción textual de cadenas, `Session` conservando el último
`PointerScanResult`, `pointer add` V2 creando la cadena dinámica
(`PointerChainRef`) con su tipo final, `pointer resolve` y sus validaciones,
y `table read`/`table set` dinámicos)y `build/test_pointer_v2` (conversión de raíz absoluta a `module+offset`,
cálculo inverso con módulo inexistente y offset fuera del módulo,
resolución de cadenas con offsets `[0]`, `[0x20]` y `[0x20,0x18]` sobre
buffers sintéticos, independencia entre el kind `pointer` y el `value_type`,
y round-trip save/load v1+v2) y `build/test_dynamic` (parseo de string y
bytes, wildcards, límites de longitud, `pattern_window_matches`, el overlap
por bloques con un `Memory` falso, `changed`/`unchanged` y los casos de
borde del parseo de valores: UINT64_MAX, overflow, hex negativo). Cada
binario devuelve 0 si todo pasa y != 0 si hay fallos; también pueden
compilarse y ejecutarse a mano:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_types.cpp -o build/test_types
./build/test_types
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_memory.cpp src/memory.cpp -o build/test_memory
./build/test_memory
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_address_table.cpp src/address_table.cpp -o build/test_address_table
./build/test_address_table
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer.cpp src/pointer.cpp src/memory.cpp -o build/test_pointer
./build/test_pointer
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer_cmd.cpp \
    src/command.cpp src/session.cpp src/scanner.cpp src/memory.cpp \
    src/pattern.cpp src/process.cpp src/address_table.cpp src/pointer.cpp -o build/test_pointer_cmd
./build/test_pointer_cmd
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_pointer_v2.cpp \
    src/pointer_resolver.cpp src/address_table.cpp src/memory.cpp -o build/test_pointer_v2
./build/test_pointer_v2
g++ -std=c++17 -O2 -Wall -Wextra -I src tests/test_dynamic.cpp \
    src/pattern.cpp src/scanner.cpp src/memory.cpp -o build/test_dynamic
./build/test_dynamic
```

### Test de extremo a extremo (proceso real)

```bash
./build.sh
bash tests/e2e.sh
```

La prueba lanza `objetivo`, le cambia el valor programáticamente y verifica
que el escáner real encuentra la dirección exacta de `dinero` (comparándola
con `&dinero`), la inspecciona y la modifica, comprobando que el proceso
refleja el cambio. Cubre también First/Next Scan, `unknown`, `changed`,
pattern scanner con y sin wildcards, strings y bytes (`first "…" string` /
`first <hex> bytes` con wildcards, `next changed`/`next unchanged`, y la
validación de errores: string vacía, patrón inválido, filtros numéricos en
dinámicos), los límites de candidatos (aviso a 10 M, truncado a 20 M), la Address Table completa (add-result → read →
set verificado en el proceso → save → clear → load → toggle) y el Pointer
Scanner contra `pointer_test` (depth 1/2/3 reconstruyendo
`Node3->Node2->Node1->TARGET`, ciclo controlado sin cuelgues y sin cadenas
cíclicas, truncado por límite de aristas, y la CLI completa: `pointer scan`
con depth 1/2/3 (caso `max_offset=0`), `pointer results`, `pointer add`
creando una entrada dinámica en la Address Table y su save/clear/load,
además de los errores de target inválido, índice inválido, `pointer
results` sin escaneo previo y `depth=0`/`depth=8`) y el flujo V2 completo
contra `pointer_offset_test`: `pointer scan` con offsets (por defecto
`max_offset=0x100`, step 8), `pointer results` mostrando la cadena con
`+0x20 -> +0x18` y raíz `[root MODULE]`, `pointer add 0` (persistente),
`pointer resolve 0`, `table read 0`, `table set 0 9001` (verificado en el
proceso), `table save` (línea `pointer type=int32 ... steps=0x18`),
reinicio del proceso y, tras `attach` + `table load`, comprobación de que
`table read 0` resuelve una dirección **nueva** (ASLR) con el valor
correcto, `table set 0 1234` en el proceso nuevo, y errores del resolver
(módulo inexistente, offset fuera del módulo, índice inválido).

## Hoja de ruta

Siguiendo el plan original (ETAPAS del documento de diseño):

| Etapa | Descripción | Estado |
|---|---|---|
| 1 | Comprobar entorno | ✅ |
| 2 | Crear `objetivo.cpp` | ✅ |
| 3 | Process Manager básico | ✅ |
| 4 | `/proc/PID/maps` | ✅ |
| 5 | Lectura de memoria autorizada | ✅ |
| 6 | Búsqueda int32 | ✅ |
| 7 | First Scan | ✅ |
| 8 | Next Scan | ✅ |
| 9 | int8/16/64 y unsigned | ✅ |
| 10 | float/double | ✅ |
| 11 | Changed/Unchanged/Increased/Decreased | ✅ |
| 12 | Strings y bytes | ✅ (`first "<texto>" string` y `first <hex> bytes` con wildcards, límite 4096, ASCII) |
| 13 | Memory Viewer completo | ◐ (versión mínima: `view`) |
| 14 | Pattern/AOB Scanner | ✅ |
| 15 | Pointer Scanner | ✅ (core + CLI; V2: offsets, resolver, `table read/set` dinámicos) |
| 16 | Address Table | ✅ (primera versión) |
| 17 | Modificar/restaurar memoria | ◐ (versión mínima: `set`) |
| 18 | Optimizar velocidad y RAM | ⏳ |
| 19 | GUI ligera | ⏳ |
| 20 | BrowserInspector | ⏳ |
| 21 | AndroidBackend | ⏳ |

## Notas

- Compila con `-O0 -g` el programa de prueba para que las variables queden
  visibles sin optimización (importante en las primeras pruebas).
- Los archivos de compilación van a `build/` (ignorado por git); si necesitas
  espacio, `rm -rf build` y recompila.
- Este proyecto es **educativo**: úsalo solo sobre programas propios o
  procesos sobre los que tengas permiso.
