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
| Punteros, tabla de direcciones, GUI | ⏳ siguientes etapas |

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

Produce dos binarios en `build/`:

- `build/memorytool` — la herramienta (escáner)
- `build/objetivo` — el programa de prueba (nuestro "objetivo")

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
next <valor> [tipo]                  Refinar resultados (igual)
next changed|unchanged|increased|decreased
next >|<|>=|<=|!= <valor> [tipo]     Refinar por comparación
count                                Número de coincidencias
pattern <bytes>                      Buscar secuencia de bytes (AOB)
                                     Ej: 48 8B 05 ?? ?? ?? ?? 48 85 C0
results [n]                          Mostrar las primeras n coincidencias
view <direccion> [len]               Visor hexadecimal
set <direccion> <valor> [tipo]       Escribir valor en memoria
info <direccion>                     Información de la región
help | quit
```

Tipos: `i8 u8 i16 u16 i32 u32 i64 u64 f32 f64` (alias: `int`, `float`,
`double`, `byte`, ...). Los valores numéricos se escriben en decimal o con
prefijo `0x` para hexadecimal; las direcciones se aceptan con o sin `0x`.

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
│   └── pointer.h/.cpp  # (siguiente etapa) Punteros y offsets
├── tests/
│   ├── objetivo.cpp    # Programa de prueba (variable conocida)
│   └── e2e.sh          # Prueba automatizada de extremo a extremo
├── CMakeLists.txt
├── build.sh            # Compilación sin CMake (g++ directo)
└── README.md
```

Detalles de diseño:

- El escáner trabaja con **bytes y direcciones absolutas**; la interpretación
  del valor (int con signo, unsigned, float, double) se hace bajo demanda
  según el tipo. Por eso añadir tipos es barato.
- `first_scan` lee la memoria **por bloques** de 4 MiB con solapamiento, y
  guarda cada posición de byte que coincide (búsqueda no alineada, como un
  escáner real).
- `next_scan` re-lee por bloques en lugar de un `pread` por dirección, así
  que `first unknown` + `next changed` es rápido aunque haya millones de
  candidatos. Cada candidato guarda su valor anterior para los filtros.
- Cada comando hace `attach → operación → detach`: el proceso objetivo se
  detiene brevemente solo durante la lectura/escritura y sigue su curso.
- Límite de seguridad de 50 M candidatos (≈ 800 MiB) con aviso, para no
  agotar la RAM del Chromebook.

## Pruebas

```bash
./build.sh
bash tests/e2e.sh
```

La prueba lanza `objetivo`, le cambia el valor programáticamente y verifica
que el escáner real encuentra la dirección exacta de `dinero` (comparándola
con `&dinero`), la inspecciona y la modifica, comprobando que el proceso
refleja el cambio.

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
| 12 | Strings y bytes | ⏳ (requiere ampliar `types`) |
| 13 | Memory Viewer completo | ◐ (versión mínima: `view`) |
| 14 | Pattern/AOB Scanner | ✅ |
| 15 | Pointer Scanner | ⏳ |
| 16 | Address Table | ⏳ |
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
