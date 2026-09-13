return function(Context)
    local PlayerService = {}
    PlayerService.__index = PlayerService

    function PlayerService.new(context)
        return setmetatable({ Context = context }, PlayerService)
    end

    function PlayerService:ReadSpeedPower()
        local player = self.Context.Character.Player
        local details = {
            value = nil,
            equalised = player and player:GetAttribute("EqualisedSpeedPower") or nil,
            minimum = player and player:GetAttribute("MinimumSpeedPower") or nil,
        }
        local sharedPlayer = self.Context.Dependencies:Get("SharedPlayer")
        if type(sharedPlayer) == "table" then
            local save = sharedPlayer.Save
            if type(save) == "table" and type(save.Get) == "function" then
                local ok, data = pcall(save.Get)
                if ok and type(data) == "table" then
                    details.value = data.SpeedPower
                end
            end
        end
        return details
    end

    function PlayerService:Snapshot()
        local humanoid = self.Context.Character.Humanoid
        local function property(name)
            if not humanoid then return nil end
            local ok, value = pcall(function() return humanoid[name] end)
            return ok and value or nil
        end
        return {
            walkSpeed = property("WalkSpeed"),
            jumpPower = property("JumpPower"),
            jumpHeight = property("JumpHeight"),
            gravity = workspace.Gravity,
            speedPower = self:ReadSpeedPower(),
        }
    end

    function PlayerService:Destroy() end
    return PlayerService
end
