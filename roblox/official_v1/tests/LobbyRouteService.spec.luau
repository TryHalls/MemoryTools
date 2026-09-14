local LobbyRouteMath = require("../core/LobbyRouteMath")({})

local parts = {}
local boundsByPart = {}
local function part(bounds)
    local value = {
        Name = "Part",
        Parent = true,
        IsA = function(_, className) return className == "BasePart" end,
    }
    boundsByPart[value] = bounds
    table.insert(parts, value)
    return value
end

part({ minX = 552.0217, maxX = 560.5818, minY = 64, maxY = 104, minZ = -584.9282, maxZ = -439.6219 })
part({ minX = 552.2561, maxX = 560.8162, minY = 64, maxY = 104, minZ = -283.8746, maxZ = -139.9589 })
part({ minX = 400, maxX = 408, minY = 64, maxY = 104, minZ = -584.9282, maxZ = -139.9589 })

local container = {
    Parent = true,
    GetDescendants = function() return parts end,
}
local mainMap = { FindFirstChild = function(_, name) return name == "LobbyBoundaries" and container or nil end }
local build = { FindFirstChild = function(_, name) return name == "MainMap" and mainMap or nil end }
local objects = { FindFirstChild = function(_, name) return name == "Build" and build or nil end }
local workspace = { FindFirstChild = function(_, name) return name == "__OBJECTS" and objects or nil end }

local function cframe(x, y, z)
    return { __type = "CFrame", Position = { X = x, Y = y, Z = z } }
end
local zero = { X = 0, Y = 0, Z = 0 }
local stateValues = {}
local calls = {}
local failAt
local cancelCheck = function() return false end
local context = {
    Workspace = workspace,
    Modules = { ["core/LobbyRouteMath"] = LobbyRouteMath },
    Logger = { Info = function() end },
    State = { Set = function(_, key, value) stateValues[key] = value end },
    Character = {
        Root = {
            Parent = true,
            Position = { X = 500, Y = 67, Z = -361.74825 },
            IsA = function(_, className) return className == "BasePart" end,
        },
    },
    FlightMovement = {
        TravelTo = function(_, target, options)
            table.insert(calls, { target = target, options = options })
            if failAt == #calls then return false, "leg " .. tostring(failAt) .. " failed" end
            return true, "ok"
        end,
    },
    Typeof = function(value) return value.__type or type(value) end,
    CFrameNew = cframe,
    Vector3New = function(x, y, z) return { X = x, Y = y, Z = z } end,
    Vector3Zero = zero,
    LobbyBoundsForPart = function(value)
        local source = boundsByPart[value]
        local copy = {}
        for key, number in pairs(source) do copy[key] = number end
        return copy
    end,
}

local LobbyRouteService = require("../game/LobbyRouteService")(context)
local service = LobbyRouteService.new(context)
local gate = assert(service:ResolveGate())
assert(math.abs(gate.gateCenterZ + 361.74825) < 0.001)
assert(#gate.sourceWalls == 2)
assert(gate.sourceWalls[1].source.Name == "Part" and gate.sourceWalls[2].source.Name == "Part")

local finalTarget = cframe(600, 70, -361.74825)
local options = { horizontalSpeed = 40, cancelCheck = cancelCheck, label = "test" }
local ok, result = service:TravelTo(finalTarget, options)
assert(ok and result == "ok")
assert(#calls == 3)
assert(calls[1].options.offset == zero and calls[2].options.offset == zero)
assert(calls[1].options.cancelCheck == cancelCheck and calls[2].options.cancelCheck == cancelCheck)
assert(calls[3].target == finalTarget and calls[3].options == options)
assert(calls[3].options.offset == nil)
assert(stateValues.autoStealState == "ROUTE_TO_TARGET")

calls = {}
failAt = 1
local failed, failure = service:TravelTo(finalTarget, options)
assert(not failed and failure == "leg 1 failed")
assert(#calls == 1)

local fatalState = { failed = 0 }
local fatalAttempts = 0
local fatalLogs = {}
local fatalContext = {
    Players = { LocalPlayer = { UserId = 1 } },
    Logger = {
        Warn = function() end,
        Action = function() end,
        Info = function() end,
        Error = function(_, message) table.insert(fatalLogs, message) end,
    },
    State = {
        Get = function(_, key, fallback)
            local value = fatalState[key]
            return value == nil and fallback or value
        end,
        Set = function(_, key, value) fatalState[key] = value end,
        Patch = function(_, values)
            for key, value in pairs(values) do fatalState[key] = value end
        end,
    },
    Services = {
        EggService = { ReadAll = function() return {} end },
        AreaService = {},
        PlotService = {},
    },
    FlightMovement = { Cancel = function() end },
}
local AutoStealController = require("../features/AutoStealController")(fatalContext)
local autoSteal = AutoStealController.new(fatalContext)
autoSteal.Config.enabled = true
autoSteal.Config.repeatEnabled = false
autoSteal.Config.retries = 2
autoSteal._generation = 1
autoSteal._select = function()
    return { Uid = "fatal-target", AreaId = "Area", NestId = "Nest" }
end
autoSteal._attempt = function()
    fatalAttempts = fatalAttempts + 1
    return false, "Humanoid is dead"
end
autoSteal:_run(1)
assert(fatalAttempts == 1)
assert(fatalState.failed == 1)
assert(fatalState.autoStealState == "ERROR")
assert(fatalState.autoStealMessage == "Auto Steal stopped: Humanoid is dead")
assert(autoSteal.Config.enabled == false)
assert(#fatalLogs == 1 and fatalLogs[1] == "Auto Steal stopped: Humanoid is dead")

print("LobbyRouteService.spec: PASS")
