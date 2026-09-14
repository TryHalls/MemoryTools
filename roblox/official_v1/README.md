# MemoryTools Official V1.0.5

Cliente modular para el PlaceId 107778070777162. El loader público es
roblox/official_v1.lua; descarga el bootstrap y los módulos con cache bust
1.0.5.

## Diseño

- core/: lifecycle, estado, personaje, logging, FlightMath, routing puro y movimiento físico.
- game/: adaptadores protegidos y routing runtime para las APIs cliente confirmadas.
- features/: controles de player, teleports y worker Auto Steal cancelable.
- ui/: ventana mobile-first, componentes y pestañas aisladas.

Las dependencias del juego se resuelven con FindFirstChild y pcall(require).
Una dependencia ausente aparece como FAILED en Debug y no impide usar las
features independientes.

## Carga

    loadstring(game:HttpGet("https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/official_v1.lua?v=1.0.5"))()

La segunda ejecución destruye la instancia anterior mediante
getgenv().__MEMORYTOOLS_V1 (con fallback a _G).

## Probe pasivo de movimiento V1.0.3

roblox/passive_probe_movement_v1_0_3.lua inspecciona source de movimiento
relevante para RigSync, Cmdr y Staff, priorizando ObbyAntiTPClient. No carga
módulos, no dispara remotes ni altera el movimiento.

    loadstring(game:HttpGet("https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/passive_probe_movement_v1_0_3.lua?v=1.0.3"))()

## Límites intencionales de V1

- Los huevos FirstAreaEgg_ se detectan y omiten porque no hay un
  FirstAreaSlotKey confirmado.
- No se calcula Highest Rarity: el campo exacto de configuración de rareza
  sigue sin confirmar.
- Las escrituras server-side de WalkSpeed y SpeedPower requieren StaffVerdict
  confirmado; el verdict de SpeedPower se registra sin asumir su semántica.
- Auto Steal y FLY TO BASE usan exclusivamente velocidad física continua en
  Heartbeat. Los cruces este del lobby pasan por la abertura derivada de
  LobbyBoundaries. La velocidad horizontal configurable está limitada a 30-150
  y Auto Steal usa 40 por defecto.
- LOCAL DEBUG TP conserva el teleport local únicamente para depuración manual.
- Auto Steal nunca solicita el carry: espera a que el jugador agarre manualmente
  el huevo y confirma el estado mediante CarryChanged o ReadFieldEgg.
- No existe persistencia por filesystem.
