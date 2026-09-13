return function(Context)
    local PlayerController = {}
    PlayerController.__index = PlayerController

    local UserInputService = game:GetService("UserInputService")
    local RunService = game:GetService("RunService")

    function PlayerController.new(context)
        local Cleanup = context.Modules["core/Cleanup"]
        local self = setmetatable({
            Context = context,
            Logger = context.Logger,
            Cleanup = Cleanup.new(),
            CharacterCleanup = Cleanup.new(),
            OriginalHumanoids = setmetatable({}, { __mode = "k" }),
            OriginalGravity = workspace.Gravity,
            NoclipParts = setmetatable({}, { __mode = "k" }),
            FreezeOriginals = setmetatable({}, { __mode = "k" }),
            WalkSpeedEnabled = false,
            InfiniteJumpEnabled = false,
            NoclipEnabled = false,
            FreezeEnabled = false,
            ReapplyAfterRespawn = true,
            Values = {
                walkSpeed = nil,
                jumpPower = nil,
                jumpHeight = nil,
                gravity = workspace.Gravity,
            },
            Modified = {
                jumpPower = false,
                jumpHeight = false,
                gravity = false,
            },
            _applyingWalkSpeed = false,
        }, PlayerController)
        self.Cleanup:Add(self.CharacterCleanup)
        self.Cleanup:Add(context.Character.Changed:Connect(function(model, humanoid, root)
            self:_onCharacter(model, humanoid, root)
        end))
        self:_onCharacter(context.Character.Model, context.Character.Humanoid, context.Character.Root)
        return self
    end

    function PlayerController:_capture(humanoid)
        if not humanoid or self.OriginalHumanoids[humanoid] then return end
        local original = {}
        for _, property in ipairs({ "WalkSpeed", "JumpPower", "JumpHeight" }) do
            local ok, value = pcall(function() return humanoid[property] end)
            if ok then original[property] = value end
        end
        self.OriginalHumanoids[humanoid] = original
        if self.Values.walkSpeed == nil then self.Values.walkSpeed = original.WalkSpeed end
        if self.Values.jumpPower == nil then self.Values.jumpPower = original.JumpPower end
        if self.Values.jumpHeight == nil then self.Values.jumpHeight = original.JumpHeight end
    end

    function PlayerController:_onCharacter(model, humanoid, root)
        self.CharacterCleanup:Cleanup()
        self.NoclipParts = setmetatable({}, { __mode = "k" })
        if not humanoid then return end
        self:_capture(humanoid)
        if self.ReapplyAfterRespawn then
            if self.WalkSpeedEnabled then self:_bindWalkSpeed(humanoid) end
            if self.Modified.jumpPower and self.Values.jumpPower then self:_setHumanoid("JumpPower", self.Values.jumpPower) end
            if self.Modified.jumpHeight and self.Values.jumpHeight then self:_setHumanoid("JumpHeight", self.Values.jumpHeight) end
            if self.NoclipEnabled then self:_bindNoclip(model) end
            if self.FreezeEnabled then self:_applyFreeze(root, true) end
        end
    end

    function PlayerController:_setHumanoid(property, value)
        local humanoid = self.Context.Character.Humanoid
        if not humanoid then return false, "Humanoid is not ready" end
        self:_capture(humanoid)
        local ok, err = pcall(function() humanoid[property] = value end)
        if not ok then
            self.Logger:Error(property .. " failed: " .. tostring(err))
            return false, tostring(err)
        end
        return true
    end

    function PlayerController:_applyWalkSpeed()
        if not self.WalkSpeedEnabled then return end
        local humanoid = self.Context.Character.Humanoid
        local value = tonumber(self.Values.walkSpeed)
        if not humanoid or not value or self._applyingWalkSpeed then return end
        self._applyingWalkSpeed = true
        pcall(function()
            if humanoid.WalkSpeed ~= value then humanoid.WalkSpeed = value end
        end)
        self._applyingWalkSpeed = false
    end

    function PlayerController:_bindWalkSpeed(humanoid)
        self.CharacterCleanup:Add(humanoid:GetPropertyChangedSignal("WalkSpeed"):Connect(function()
            if self.WalkSpeedEnabled and not self._applyingWalkSpeed then
                task.defer(function() self:_applyWalkSpeed() end)
            end
        end))
        self:_applyWalkSpeed()
    end

    function PlayerController:SetWalkSpeed(value)
        value = tonumber(value)
        if not value or value < 0 or value > 500 then return false, "WalkSpeed must be 0-500" end
        self.Values.walkSpeed = value
        self:_applyWalkSpeed()
        return true
    end

    function PlayerController:EnableWalkSpeed(enabled)
        enabled = enabled == true
        if self.WalkSpeedEnabled == enabled then return true end
        self.WalkSpeedEnabled = enabled
        self.Context.State:Set("walkSpeedEnabled", enabled)
        local humanoid = self.Context.Character.Humanoid
        if enabled then
            if not humanoid then return false, "Humanoid is not ready" end
            self:_capture(humanoid)
            self:_bindWalkSpeed(humanoid)
        elseif humanoid then
            local original = self.OriginalHumanoids[humanoid]
            if original and original.WalkSpeed ~= nil then
                pcall(function() humanoid.WalkSpeed = original.WalkSpeed end)
            end
            self.CharacterCleanup:Cleanup()
            self:_restoreNoclip()
            if self.NoclipEnabled then self:_bindNoclip(self.Context.Character.Model) end
        end
        return true
    end

    function PlayerController:SetJumpPower(value)
        value = tonumber(value)
        if not value or value < 0 or value > 500 then return false, "JumpPower must be 0-500" end
        self.Values.jumpPower = value
        local ok, err = self:_setHumanoid("JumpPower", value)
        if ok then self.Modified.jumpPower = true end
        return ok, err
    end

    function PlayerController:SetJumpHeight(value)
        value = tonumber(value)
        if not value or value < 0 or value > 200 then return false, "JumpHeight must be 0-200" end
        self.Values.jumpHeight = value
        local ok, err = self:_setHumanoid("JumpHeight", value)
        if ok then self.Modified.jumpHeight = true end
        return ok, err
    end

    function PlayerController:SetGravity(value)
        value = tonumber(value)
        if not value or value < 0 or value > 1000 then return false, "Gravity must be 0-1000" end
        self.Values.gravity = value
        local ok, err = pcall(function() workspace.Gravity = value end)
        if ok then self.Modified.gravity = true else self.Logger:Error("Gravity failed: " .. tostring(err)) end
        return ok, err
    end

    function PlayerController:EnableInfiniteJump(enabled)
        enabled = enabled == true
        if self.InfiniteJumpEnabled == enabled then return end
        self.InfiniteJumpEnabled = enabled
        self.Context.State:Set("infiniteJumpEnabled", enabled)
        if enabled then
            self._infiniteJumpConnection = UserInputService.JumpRequest:Connect(function()
                if not self.InfiniteJumpEnabled then return end
                local humanoid = self.Context.Character.Humanoid
                if humanoid then pcall(function() humanoid:ChangeState(Enum.HumanoidStateType.Jumping) end) end
            end)
            self.Cleanup:Add(self._infiniteJumpConnection)
        elseif self._infiniteJumpConnection then
            self._infiniteJumpConnection:Disconnect()
            self._infiniteJumpConnection = nil
        end
    end

    function PlayerController:_trackNoclipPart(instance)
        if instance and instance:IsA("BasePart") then
            if self.NoclipParts[instance] == nil then self.NoclipParts[instance] = instance.CanCollide end
            if instance.CanCollide then instance.CanCollide = false end
        end
    end

    function PlayerController:_bindNoclip(model)
        if not model then return end
        for _, child in ipairs(model:GetDescendants()) do
            self:_trackNoclipPart(child)
        end
        self.CharacterCleanup:Add(model.DescendantAdded:Connect(function(child)
            if self.NoclipEnabled then self:_trackNoclipPart(child) end
        end))
        self.CharacterCleanup:Add(RunService.Stepped:Connect(function()
            if not self.NoclipEnabled then return end
            for part in pairs(self.NoclipParts) do
                if part.Parent and part.CanCollide then part.CanCollide = false end
            end
        end))
    end

    function PlayerController:_restoreNoclip()
        for part, canCollide in pairs(self.NoclipParts) do
            if part and part.Parent then pcall(function() part.CanCollide = canCollide end) end
        end
        self.NoclipParts = setmetatable({}, { __mode = "k" })
    end

    function PlayerController:EnableNoclip(enabled)
        enabled = enabled == true
        if self.NoclipEnabled == enabled then return end
        self.NoclipEnabled = enabled
        self.Context.State:Set("noclipEnabled", enabled)
        if enabled then
            self:_bindNoclip(self.Context.Character.Model)
        else
            self.CharacterCleanup:Cleanup()
            self:_restoreNoclip()
            local humanoid = self.Context.Character.Humanoid
            if self.WalkSpeedEnabled and humanoid then self:_bindWalkSpeed(humanoid) end
        end
    end

    function PlayerController:_applyFreeze(root, enabled)
        root = root or self.Context.Character.Root
        if not root then return false, "HumanoidRootPart is not ready" end
        if enabled and self.FreezeOriginals[root] == nil then self.FreezeOriginals[root] = root.Anchored end
        local original = self.FreezeOriginals[root]
        local ok, err = pcall(function() root.Anchored = enabled and true or (original == true) end)
        if not enabled then self.FreezeOriginals[root] = nil end
        return ok, err
    end

    function PlayerController:EnableFreeze(enabled)
        enabled = enabled == true
        self.FreezeEnabled = enabled
        self.Context.State:Set("freezeEnabled", enabled)
        return self:_applyFreeze(nil, enabled)
    end

    function PlayerController:SetReapply(enabled)
        self.ReapplyAfterRespawn = enabled == true
        self.Context.Config.reapplyPlayerSettings = self.ReapplyAfterRespawn
        self.Context.State:Set("reapplyPlayerSettings", self.ReapplyAfterRespawn)
    end

    function PlayerController:ResetMovement()
        self.WalkSpeedEnabled = false
        self.InfiniteJumpEnabled = false
        self.NoclipEnabled = false
        self.FreezeEnabled = false
        self.CharacterCleanup:Cleanup()
        if self._infiniteJumpConnection then self._infiniteJumpConnection:Disconnect() self._infiniteJumpConnection = nil end
        self:_restoreNoclip()
        local humanoid = self.Context.Character.Humanoid
        local original = humanoid and self.OriginalHumanoids[humanoid] or nil
        if humanoid and original then
            for _, property in ipairs({ "WalkSpeed", "JumpPower", "JumpHeight" }) do
                if original[property] ~= nil then pcall(function() humanoid[property] = original[property] end) end
            end
        end
        local root = self.Context.Character.Root
        local freezeOriginal = root and self.FreezeOriginals[root] or nil
        if root and freezeOriginal ~= nil then pcall(function() root.Anchored = freezeOriginal end) end
        self.FreezeOriginals = setmetatable({}, { __mode = "k" })
        pcall(function() workspace.Gravity = self.OriginalGravity end)
        self.Modified.jumpPower = false
        self.Modified.jumpHeight = false
        self.Modified.gravity = false
        self.Context.State:Patch({
            walkSpeedEnabled = false,
            infiniteJumpEnabled = false,
            noclipEnabled = false,
            freezeEnabled = false,
        })
        self.Logger:Action("Reset Movement", "original values restored")
    end

    function PlayerController:Destroy()
        self:ResetMovement()
        self.Cleanup:Destroy()
    end

    return PlayerController
end
