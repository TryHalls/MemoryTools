local EggServiceFactory = require("../game/EggService")

local localPlayer = { UserId = 2468 }
local waitHandler = function() end
local taskMock = {
    wait = function(seconds)
        return waitHandler(seconds)
    end,
}

local carryRequests = 0
local eggState
local callback
local disconnected
local signal = {
    Connect = function(_, nextCallback)
        callback = nextCallback
        disconnected = false
        return {
            Disconnect = function()
                disconnected = true
            end,
        }
    end,
}

local context = {
    Logger = { Error = function() end },
    Players = { LocalPlayer = localPlayer },
    Task = taskMock,
    Dependencies = {
        Get = function(_, name)
            if name == "EggState" then return eggState end
            return nil
        end,
    },
}

local EggService = EggServiceFactory(context)
local service = EggService.new(context)

local function reset(overrides)
    callback = nil
    disconnected = nil
    waitHandler = function() end
    eggState = {
        ReadFieldEggs = function() return { Records = {} } end,
        CarryFieldEgg = function()
            carryRequests = carryRequests + 1
        end,
        CarryChanged = signal,
    }
    for key, value in pairs(overrides or {}) do eggState[key] = value end
end

reset()
local waits = 0
waitHandler = function()
    waits = waits + 1
    if waits == 1 then
        callback({ IsCarrying = false, Uid = "target" })
    elseif waits == 2 then
        callback({ IsCarrying = true, Uid = "other" })
    elseif waits == 3 then
        callback("ignored", { IsCarrying = true, Uid = "target" })
    end
end
local waitingShown = false
local carried, carryInfo, mismatch = service:WaitForManualCarry("target", nil, function()
    waitingShown = true
end)
assert(carried == true)
assert(carryInfo.Uid == "target")
assert(mismatch == "other")
assert(waits == 3)
assert(waitingShown == true)
assert(disconnected == true)
assert(carryRequests == 0)

reset()
local cancelled, cancelReason = service:WaitForManualCarry("target", function()
    return true
end)
assert(cancelled == false and cancelReason == "cancelled")
assert(disconnected == true)
assert(carryRequests == 0)

reset({
    ReadFieldEgg = function(uid)
        assert(uid == "target")
        return { Uid = uid, CarrierUserId = localPlayer.UserId }
    end,
})
local polled, pollInfo = service:WaitForManualCarry("target")
assert(polled == true)
assert(pollInfo.CarrierUserId == localPlayer.UserId)
assert(disconnected == true)
assert(carryRequests == 0)

reset({ CarryChanged = false })
local unavailable, unavailableReason = service:WaitForManualCarry("target")
assert(unavailable == false and unavailableReason == "EggState.CarryChanged unavailable")

reset()
local invalid, invalidReason = service:WaitForManualCarry(123)
assert(invalid == false and invalidReason == "invalid egg uid")

print("ManualCarry.spec: PASS")
