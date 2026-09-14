local function event()
    local records = {}
    return {
        Connect = function(_, callback)
            local record = { Callback = callback, Connected = true }
            table.insert(records, record)
            return {
                Disconnect = function()
                    record.Connected = false
                end,
            }
        end,
        Fire = function(_, ...)
            for _, record in ipairs(records) do
                if record.Connected then record.Callback(...) end
            end
        end,
    }
end

local staffVerdict = event()
local speedPowerVerdict = event()
local calls = {}
local nextStaffVerdict = true

local function remote(name, signal)
    return {
        OnClientEvent = signal,
        FireServer = function(_, ...)
            calls[name] = table.pack(...)
            if name == "ProbeStaffStatus" then staffVerdict:Fire(nextStaffVerdict) end
        end,
    }
end

local requiredRemotes = {
    StaffConsole = {
        ProbeStaffStatus = remote("ProbeStaffStatus"),
        StaffVerdict = remote("StaffVerdict", staffVerdict),
        WriteWalkSpeed = remote("WriteWalkSpeed"),
        WriteSpeedPower = remote("WriteSpeedPower"),
        SpeedPowerVerdict = remote("SpeedPowerVerdict", speedPowerVerdict),
    },
}

local values = {}
local context = {
    Dependencies = {
        Get = function(_, name)
            if name == "Remotes" then return requiredRemotes end
            return nil
        end,
    },
    Logger = { Info = function() end, Warn = function() end, Action = function() end },
    State = {
        Get = function(_, key, fallback) return values[key] == nil and fallback or values[key] end,
        Set = function(_, key, value) values[key] = value end,
    },
}

local StaffService = require("../game/StaffService")(context)
local authorized = StaffService.new(context)
assert(authorized:IsReady())
assert(authorized:SetWalkSpeed(20) == false)
assert(authorized:SetSpeedPower(20) == false)

local ok, isStaff = authorized:Probe(1)
assert(ok and isStaff == true and authorized:IsStaff() == true)
assert(authorized:SetWalkSpeed(1000) == true and calls.WriteWalkSpeed[1] == 1000)
assert(authorized:SetWalkSpeed(1001) == false)
assert(authorized:SetSpeedPower(1000000000000) == true)
assert(calls.WriteSpeedPower[1] == 1000000000000)
assert(authorized:SetSpeedPower(-1) == false)

speedPowerVerdict:Fire("opaque", { p237 = 1 }, false, nil)
assert(authorized.LastSpeedPowerVerdict.n == 4)
assert(authorized.LastSpeedPowerVerdict[1] == "opaque")
assert(authorized.LastSpeedPowerVerdict[3] == false)

nextStaffVerdict = false
local denied = StaffService.new(context)
assert(denied:IsReady())
ok, isStaff = denied:Probe(1)
assert(ok and isStaff == false and denied:IsStaff() == false)
assert(values.staffStatus == "DENIED")
assert(denied:SetWalkSpeed(20) == false)
assert(denied:SetSpeedPower(20) == false)

print("StaffService.spec: PASS")
