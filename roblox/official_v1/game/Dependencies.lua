return function(Context)
    local Dependencies = {}
    Dependencies.__index = Dependencies

    local ReplicatedStorage = game:GetService("ReplicatedStorage")
    local Players = game:GetService("Players")

    local DEFINITIONS = {
        EggState = { "ReplicatedStorage", "Client", "EggState" },
        PlotState = { "ReplicatedStorage", "Client", "PlotState" },
        AreaEggSlotIdentity = { "ReplicatedStorage", "Shared", "Util", "AreaEggSlotIdentity" },
        Remotes = { "ReplicatedStorage", "Shared", "Remotes" },
        SharedPlayer = { "ReplicatedStorage", "Shared", "Player" },
        WalkSpeedGovernor = { "ReplicatedStorage", "Shared", "Util", "WalkSpeedGovernor" },
        SpeedPowerProjection = { "ReplicatedStorage", "Client", "SpeedPowerProjection" },
        NestResolver = { "PlayerScripts", "Game", "AreaEggs", "NestResolver" },
    }

    local function resolvePath(parts)
        local current
        local start = parts[1]
        if start == "ReplicatedStorage" then
            current = ReplicatedStorage
        elseif start == "PlayerScripts" then
            local player = Players.LocalPlayer
            current = player and player:FindFirstChildOfClass("PlayerScripts") or nil
        end
        return Context.Safe.Find(current, parts, 2)
    end

    function Dependencies.new(logger)
        local self = setmetatable({
            Logger = logger,
            Modules = {},
            Health = {},
            Errors = {},
        }, Dependencies)
        self:Refresh()
        return self
    end

    function Dependencies:SafeRequire(name, instance)
        local ok, result, requireError = Context.Safe.Require(instance)
        if not ok then
            self.Health[name] = false
            self.Errors[name] = tostring(requireError)
            self.Logger:Warn(name .. " FAILED: " .. tostring(requireError))
            return nil
        end
        self.Health[name] = true
        self.Errors[name] = nil
        self.Modules[name] = result
        return result
    end

    function Dependencies:Refresh()
        for name, path in pairs(DEFINITIONS) do
            if not self.Health[name] then
                self:SafeRequire(name, resolvePath(path))
            end
        end
        return self.Health
    end

    function Dependencies:Get(name)
        return self.Modules[name]
    end

    function Dependencies:IsReady(name)
        return self.Health[name] == true
    end

    function Dependencies:Status(name)
        return self:IsReady(name) and "READY" or "FAILED"
    end

    return Dependencies
end
