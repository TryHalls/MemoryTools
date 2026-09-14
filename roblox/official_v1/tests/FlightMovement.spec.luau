local FlightMath = require("../core/FlightMath")({})

local context = {
    RunService = { Heartbeat = {} },
    Modules = { ["core/FlightMath"] = FlightMath },
    Logger = { Action = function() end },
    Character = {},
    Controllers = {},
}
local FlightMovement = require("../core/FlightMovement")(context)
local movement = FlightMovement.new(context)

local initializedWithoutMath, initializationError = pcall(function()
    require("../core/FlightMovement")({
        RunService = { Heartbeat = {} },
        Modules = {},
    })
end)
assert(not initializedWithoutMath)
assert(string.find(tostring(initializationError), "FlightMath is not initialized", 1, true) ~= nil)

assert(not movement:IsActive())
movement._travel = { cancelled = false, cleaned = false, collisions = {} }
assert(movement:IsActive())
assert(movement:Cancel("manual") == true)
assert(not movement:IsActive())
assert(movement:Cancel() == false)

assert(FlightMath.TerminationReason({}) == nil)
assert(FlightMath.TerminationReason({ cancelled = true, cancelReason = "manual" }) == "manual")
assert(FlightMath.TerminationReason({ elapsed = 10, timeout = 10 }) == "Flight movement timed out")
assert(FlightMath.TerminationReason({ stalled = true }) == "Flight movement stalled")
assert(FlightMath.TerminationReason({ characterMissing = true }) == "Character is not ready")
assert(FlightMath.TerminationReason({ dead = true }) == "Humanoid is dead")
assert(FlightMath.TerminationReason({ cancelCheck = true }) == "Flight movement cancelled")
assert(FlightMath.TerminationReason({ destroyed = true }) == "MemoryTools is destroyed")

print("FlightMovement.spec: PASS")
