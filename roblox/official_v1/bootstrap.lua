-- MemoryTools Official V1.0.4.1 bootstrap
local VERSION = "1.0.4.1"
local EXPECTED_PLACE_ID = 107778070777162
local BASE_URL = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/official_v1/"

local MODULE_PATHS = {
    "core/Cleanup",
    "core/Logger",
    "core/State",
    "core/Safe",
    "core/Character",
    "core/Teleport",
    "core/FlightMath",
    "core/FlightMovement",
    "game/Dependencies",
    "game/EggService",
    "game/PlotService",
    "game/AreaService",
    "game/PlayerService",
    "game/StaffService",
    "features/PlayerController",
    "features/TeleportController",
    "features/AutoStealController",
    "ui/DropdownPool",
    "ui/Components",
    "ui/Tabs/Home",
    "ui/Tabs/Player",
    "ui/Tabs/AutoSteal",
    "ui/Tabs/Teleports",
    "ui/Tabs/Debug",
    "ui/Tabs/Settings",
    "ui/MainWindow",
}

local function compileModule(path)
    local url = BASE_URL .. path .. ".lua?v=" .. VERSION
    local ok, source
    for attempt = 1, 2 do
        ok, source = pcall(function()
            return game:HttpGet(url)
        end)
        if ok and type(source) == "string" and source ~= "" then break end
        if attempt < 2 then task.wait(0.15) end
    end
    if not ok or type(source) ~= "string" or source == "" then
        error("Download failed for " .. path .. ": " .. tostring(source))
    end
    local chunk, compileError = loadstring(source)
    if not chunk then
        error("Compile failed for " .. path .. ": " .. tostring(compileError))
    end
    local ran, factory = pcall(chunk)
    if not ran or type(factory) ~= "function" then
        error("Invalid module " .. path .. ": " .. tostring(factory))
    end
    return factory
end

local factories = {}
for _, path in ipairs(MODULE_PATHS) do
    factories[path] = compileModule(path)
end

local Context = {
    Version = VERSION,
    ExpectedPlaceId = EXPECTED_PLACE_ID,
    Modules = {},
    Services = {},
    Controllers = {},
    Config = {
        notifications = true,
        uiScale = 1,
        reapplyPlayerSettings = true,
    },
}

local function instantiate(path)
    local ok, result = pcall(factories[path], Context)
    if not ok then
        error("Initialize failed for " .. path .. ": " .. tostring(result))
    end
    Context.Modules[path] = result
    return result
end

local Cleanup = instantiate("core/Cleanup")
local Logger = instantiate("core/Logger")
Context.Logger = Logger.new(50)
Context.Cleanup = Cleanup.new()
Context.Cleanup:Add(Context.Logger)

local State = instantiate("core/State")
Context.State = State.new({
    autoStealState = "IDLE",
    currentTargetUid = "",
    currentTargetArea = "",
    completed = 0,
    failed = 0,
    walkSpeedEnabled = false,
    infiniteJumpEnabled = false,
    noclipEnabled = false,
    freezeEnabled = false,
    reapplyPlayerSettings = true,
    autoStealMessage = "",
    autoStealMovementWarning = "",
    staffStatus = "UNKNOWN",
    speedPowerVerdictRevision = 0,
})
Context.Cleanup:Add(Context.State)
Context.Safe = instantiate("core/Safe")

if game.PlaceId ~= EXPECTED_PLACE_ID then
    Context.Logger:Warn("Unexpected PlaceId " .. tostring(game.PlaceId) .. "; game-specific features may be unavailable")
end

local Dependencies = instantiate("game/Dependencies")
Context.Dependencies = Dependencies.new(Context.Logger)

local Character = instantiate("core/Character")
Context.Character = Character.new(Context.Cleanup, Context.Logger)
Context.Cleanup:Add(Context.Character)

local Teleport = instantiate("core/Teleport")
Context.Teleport = Teleport.new(Context.Character, Context.Logger)

local FlightMath = instantiate("core/FlightMath")
Context.FlightMath = FlightMath

local FlightMovement = instantiate("core/FlightMovement")
Context.FlightMovement = FlightMovement.new(Context)
Context.Cleanup:Add(Context.FlightMovement)

local serviceOrder = { "EggService", "PlotService", "AreaService", "PlayerService", "StaffService" }
for _, name in ipairs(serviceOrder) do
    local class = instantiate("game/" .. name)
    Context.Services[name] = class.new(Context)
    Context.Cleanup:Add(Context.Services[name])
end

local controllerOrder = { "PlayerController", "TeleportController", "AutoStealController" }
for _, name in ipairs(controllerOrder) do
    local class = instantiate("features/" .. name)
    Context.Controllers[name] = class.new(Context)
    Context.Cleanup:Add(Context.Controllers[name])
end

function Context:Destroy(reason)
    if self.Destroyed then
        return
    end
    self.Destroyed = true
    self.Logger:Info("Shutdown: " .. tostring(reason or "requested"))
    self.Cleanup:Destroy()
    local env = _G
    if type(getgenv) == "function" then
        local ok, value = pcall(getgenv)
        if ok and type(value) == "table" then env = value end
    end
    if env.__MEMORYTOOLS_V1 == self then
        env.__MEMORYTOOLS_V1 = nil
    end
end

instantiate("ui/DropdownPool")
instantiate("ui/Components")
for _, name in ipairs({ "Home", "Player", "AutoSteal", "Teleports", "Debug", "Settings" }) do
    instantiate("ui/Tabs/" .. name)
end
local MainWindow = instantiate("ui/MainWindow")
local uiOk, uiResult = pcall(MainWindow.new, Context)
if not uiOk then
    Context:Destroy("UI initialization failed")
    error(tostring(uiResult))
end
Context.UI = uiResult
Context.Cleanup:Add(Context.UI)

Context.Logger:Info("MemoryTools Official V" .. VERSION .. " ready")
return Context
