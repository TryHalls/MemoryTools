# MemoryTools Official V1.0.2

Cliente modular para el PlaceId 107778070777162. El loader público es
roblox/official_v1.lua; descarga el bootstrap y los módulos con cache bust
1.0.2.

## Diseño

- core/: lifecycle, estado, personaje, logging y teleport central.
- game/: adaptadores protegidos para las APIs cliente confirmadas.
- features/: controles de player, teleports y worker Auto Steal cancelable.
- ui/: ventana mobile-first, componentes y pestañas aisladas.

Las dependencias del juego se resuelven con FindFirstChild y pcall(require).
Una dependencia ausente aparece como FAILED en Debug y no impide usar las
features independientes.

## Carga

    loadstring(game:HttpGet("https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/official_v1.lua?v=1.0.2"))()

La segunda ejecución destruye la instancia anterior mediante
getgenv().__MEMORYTOOLS_V1 (con fallback a _G).

## Probe pasivo compacto V1.0.2

roblox/passive_probe_v1_0_2.lua inspecciona únicamente ventanas de source
relevantes para Staff, Cmdr, RigSync y el mapeo exacto de rarity. No carga
módulos, no dispara remotes y no inspecciona conexiones ni el GC.

    loadstring(game:HttpGet("https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/passive_probe_v1_0_2.lua?v=1.0.2"))()

## Límites intencionales de V1

- Los huevos FirstAreaEgg_ se detectan y omiten porque no hay un
  FirstAreaSlotKey confirmado.
- No se calcula Highest Rarity: el campo exacto de configuración de rareza
  sigue sin confirmar.
- SpeedPower es sólo diagnóstico y no se modifica.
- No existe persistencia por filesystem.
