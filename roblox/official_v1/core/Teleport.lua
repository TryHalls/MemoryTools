return function(Context)
    local Teleport = {}
    Teleport.__index = Teleport

    function Teleport.new(character, logger)
        return setmetatable({
            Character = character,
            Logger = logger,
            Offset = Vector3.new(0, 3, 0),
            Backend = "LOCAL",
            BackendName = "LocalTeleportBackend",
        }, Teleport)
    end

    function Teleport:GetMovementBackend()
        return self.Backend
    end

    function Teleport:IsLocalBackend()
        return self:GetMovementBackend() == "LOCAL"
    end

    function Teleport:To(targetCFrame, label, useOffset)
        if typeof(targetCFrame) ~= "CFrame" then
            return false, "Teleport target is not a CFrame"
        end
        local model = self.Character.Model
        if not model or not model.Parent then
            self.Character:Refresh()
            model = self.Character.Model
        end
        if not model or not model.Parent then
            return false, "Character is not ready"
        end
        local destination = useOffset == false and targetCFrame or (targetCFrame + self.Offset)
        local ok, err = pcall(function()
            model:PivotTo(destination)
        end)
        if not ok then
            local root = self.Character.Root or model:FindFirstChild("HumanoidRootPart")
            if root and root:IsA("BasePart") then
                ok, err = pcall(function()
                    root.CFrame = destination
                end)
            end
        end
        if ok then
            self.Logger:Action("Teleport", label or "destination")
            return true
        end
        self.Logger:Error("Teleport failed: " .. tostring(err))
        return false, tostring(err)
    end

    return Teleport
end
