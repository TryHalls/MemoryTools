return function(Context)
    local Character = {}
    Character.__index = Character

    local Players = game:GetService("Players")

    function Character.new(cleanup, logger)
        local self = setmetatable({
            Player = Players.LocalPlayer,
            Model = nil,
            Humanoid = nil,
            Root = nil,
            _logger = logger,
            _changedEvent = Instance.new("BindableEvent"),
        }, Character)
        self.Changed = self._changedEvent.Event
        cleanup:Add(self._changedEvent)

        if not self.Player then
            logger:Error("LocalPlayer is not available")
            return self
        end

        cleanup:Add(self.Player.CharacterAdded:Connect(function(model)
            self:_bind(model)
        end))
        cleanup:Add(self.Player.CharacterRemoving:Connect(function(model)
            if self.Model == model then
                self.Model, self.Humanoid, self.Root = nil, nil, nil
                self._changedEvent:Fire(nil, nil, nil)
            end
        end))
        self:_bind(self.Player.Character)
        return self
    end

    function Character:_bind(model)
        self.Model = model
        self.Humanoid = model and model:FindFirstChildOfClass("Humanoid") or nil
        self.Root = model and model:FindFirstChild("HumanoidRootPart") or nil
        if model and (not self.Humanoid or not self.Root) then
            task.defer(function()
                if self._destroyed or self.Model ~= model then
                    return
                end
                self.Humanoid = model:FindFirstChildOfClass("Humanoid") or model:WaitForChild("Humanoid", 8)
                self.Root = model:FindFirstChild("HumanoidRootPart") or model:WaitForChild("HumanoidRootPart", 8)
                self._changedEvent:Fire(self.Model, self.Humanoid, self.Root)
            end)
        end
        self._changedEvent:Fire(self.Model, self.Humanoid, self.Root)
    end

    function Character:Refresh()
        self:_bind(self.Player and self.Player.Character or nil)
        return self.Model, self.Humanoid, self.Root
    end

    function Character:IsReady()
        return self.Model ~= nil and self.Humanoid ~= nil and self.Root ~= nil
    end

    function Character:Destroy()
        self._destroyed = true
        self.Model, self.Humanoid, self.Root = nil, nil, nil
    end

    return Character
end
