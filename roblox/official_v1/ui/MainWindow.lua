return function(Context)
    local MainWindow = {}
    MainWindow.__index = MainWindow

    local Players = game:GetService("Players")
    local UserInputService = game:GetService("UserInputService")
    local RunService = game:GetService("RunService")

    local TAB_NAMES = {
        { key = "Home", text = "HOME" },
        { key = "Player", text = "PLAYER" },
        { key = "AutoSteal", text = "AUTO STEAL" },
        { key = "Teleports", text = "TELEPORTS" },
        { key = "Debug", text = "DEBUG" },
        { key = "Settings", text = "SETTINGS" },
    }

    local function parentForGui()
        if type(gethui) == "function" then
            local ok, value = pcall(gethui)
            if ok and value then return value end
        end
        local player = Players.LocalPlayer
        return player and (player:FindFirstChildOfClass("PlayerGui") or player:WaitForChild("PlayerGui", 8))
    end

    local function tracebackError(err)
        if type(debug) == "table" and type(debug.traceback) == "function" then
            return debug.traceback(tostring(err), 2)
        end
        return tostring(err)
    end

    function MainWindow.new(context)
        local Cleanup = context.Modules["core/Cleanup"]
        local C = context.Modules["ui/Components"]
        local self = setmetatable({
            Context = context,
            Cleanup = Cleanup.new(),
            Pages = {},
            TabButtons = {},
            PulseCallbacks = {},
            Scale = nil,
            _toastGeneration = 0,
        }, MainWindow)

        local parent = parentForGui()
        if not parent then error("No GUI parent available") end
        local old = parent:FindFirstChild("MemoryToolsOfficialV1")
        if old then old:Destroy() end

        local gui = Instance.new("ScreenGui")
        gui.Name = "MemoryToolsOfficialV1"
        gui.ResetOnSpawn = false
        gui.IgnoreGuiInset = false
        gui.DisplayOrder = 999999
        gui.ZIndexBehavior = Enum.ZIndexBehavior.Sibling
        gui.Parent = parent
        self.Gui = gui
        self.Cleanup:Add(gui)

        local main = Instance.new("Frame")
        main.Name = "Window"
        main.AnchorPoint = Vector2.new(0.5, 0.5)
        main.Position = UDim2.fromScale(0.5, 0.5)
        main.Size = UDim2.fromScale(0.94, 0.82)
        main.BackgroundColor3 = C.Theme.background
        main.BorderSizePixel = 0
        main.Parent = gui
        self.Main = main
        local constraint = Instance.new("UISizeConstraint")
        constraint.MinSize = Vector2.new(300, 360)
        constraint.MaxSize = Vector2.new(680, 620)
        constraint.Parent = main
        Instance.new("UICorner", main).CornerRadius = UDim.new(0, 14)
        local scale = Instance.new("UIScale")
        scale.Scale = 1
        scale.Parent = main
        self.Scale = scale

        local top = Instance.new("Frame")
        top.Name = "DragBar"
        top.Size = UDim2.new(1, 0, 0, 52)
        top.BackgroundColor3 = C.Theme.panel
        top.BorderSizePixel = 0
        top.Parent = main
        Instance.new("UICorner", top).CornerRadius = UDim.new(0, 14)

        local title = C.Label(top, "MemoryTools  •  V" .. context.Version, 52, "title")
        title.Position = UDim2.new(0, 14, 0, 0)
        title.Size = UDim2.new(1, -112, 1, 0)
        title.TextSize = 15

        local minimize = Instance.new("TextButton")
        minimize.Position = UDim2.new(1, -92, 0, 8)
        minimize.Size = UDim2.new(0, 38, 0, 36)
        minimize.BackgroundColor3 = C.Theme.raised
        minimize.BorderSizePixel = 0
        minimize.Font = Enum.Font.GothamBold
        minimize.Text = "—"
        minimize.TextColor3 = C.Theme.text
        minimize.TextSize = 18
        minimize.Parent = top
        Instance.new("UICorner", minimize).CornerRadius = UDim.new(0, 8)

        local close = Instance.new("TextButton")
        close.Position = UDim2.new(1, -48, 0, 8)
        close.Size = UDim2.new(0, 38, 0, 36)
        close.BackgroundColor3 = C.Theme.danger
        close.BorderSizePixel = 0
        close.Font = Enum.Font.GothamBold
        close.Text = "×"
        close.TextColor3 = C.Theme.text
        close.TextSize = 20
        close.Parent = top
        Instance.new("UICorner", close).CornerRadius = UDim.new(0, 8)

        local restore = Instance.new("TextButton")
        restore.AnchorPoint = Vector2.new(1, 0)
        restore.Position = UDim2.new(1, -12, 0, 12)
        restore.Size = UDim2.new(0, 142, 0, 48)
        restore.BackgroundColor3 = C.Theme.accent
        restore.BorderSizePixel = 0
        restore.Font = Enum.Font.GothamBold
        restore.Text = "MEMORYTOOLS"
        restore.TextColor3 = C.Theme.text
        restore.TextSize = 13
        restore.Visible = false
        restore.Parent = gui
        Instance.new("UICorner", restore).CornerRadius = UDim.new(0, 12)

        local toast = Instance.new("TextLabel")
        toast.AnchorPoint = Vector2.new(0.5, 1)
        toast.Position = UDim2.new(0.5, 0, 1, -12)
        toast.Size = UDim2.new(0.84, 0, 0, 48)
        toast.BackgroundColor3 = C.Theme.raised
        toast.BorderSizePixel = 0
        toast.Font = Enum.Font.GothamBold
        toast.TextColor3 = C.Theme.text
        toast.TextSize = 12
        toast.TextWrapped = true
        toast.Visible = false
        toast.ZIndex = 20
        toast.Parent = gui
        Instance.new("UICorner", toast).CornerRadius = UDim.new(0, 10)
        self.Toast = toast

        self:Connect(minimize.Activated, function()
            main.Visible = false
            restore.Visible = true
        end)
        self:Connect(restore.Activated, function()
            restore.Visible = false
            main.Visible = true
        end)
        self:Connect(close.Activated, function()
            context:Destroy("UI closed")
        end)
        self:Connect(context.Logger.Changed, function(entry)
            if context.Config.notifications and entry and entry.level ~= "INFO" then
                self:Notify(entry.level .. ": " .. entry.message,
                    entry.level == "ERROR" and C.Theme.danger or C.Theme.warning)
            end
        end)

        local navigation = Instance.new("ScrollingFrame")
        navigation.Position = UDim2.new(0, 8, 0, 60)
        navigation.Size = UDim2.new(1, -16, 0, 46)
        navigation.BackgroundTransparency = 1
        navigation.BorderSizePixel = 0
        navigation.ScrollBarThickness = 0
        navigation.ScrollingDirection = Enum.ScrollingDirection.X
        navigation.AutomaticCanvasSize = Enum.AutomaticSize.X
        navigation.CanvasSize = UDim2.new()
        navigation.Parent = main
        local navLayout = Instance.new("UIListLayout")
        navLayout.FillDirection = Enum.FillDirection.Horizontal
        navLayout.Padding = UDim.new(0, 7)
        navLayout.Parent = navigation

        local content = Instance.new("Frame")
        content.Position = UDim2.new(0, 8, 0, 112)
        content.Size = UDim2.new(1, -16, 1, -120)
        content.BackgroundColor3 = C.Theme.panel
        content.BorderSizePixel = 0
        content.ClipsDescendants = true
        content.Parent = main
        Instance.new("UICorner", content).CornerRadius = UDim.new(0, 12)

        for _, tab in ipairs(TAB_NAMES) do
            local button = Instance.new("TextButton")
            button.Size = UDim2.new(0, tab.key == "AutoSteal" and 104 or 82, 0, 42)
            button.BackgroundColor3 = C.Theme.raised
            button.BorderSizePixel = 0
            button.Font = Enum.Font.GothamBold
            button.Text = tab.text
            button.TextColor3 = C.Theme.muted
            button.TextSize = 11
            button.Parent = navigation
            Instance.new("UICorner", button).CornerRadius = UDim.new(0, 9)
            self.TabButtons[tab.key] = button
            local page = C.Page(content)
            page.Name = tab.key
            self.Pages[tab.key] = page
            local builder = context.Modules["ui/Tabs/" .. tab.key]
            local built, buildError = xpcall(function()
                builder(page, self)
            end, tracebackError)
            if not built then
                context.Controllers.AutoStealController:Stop(
                    "UI construction failed (" .. tab.text .. "):\n" .. tostring(buildError),
                    true
                )
                local failure = C.Label(page, tab.text .. " unavailable\n" .. tostring(buildError), 100, "muted")
                failure.AutomaticSize = Enum.AutomaticSize.Y
            end
            self:Connect(button.Activated, function() self:ShowTab(tab.key) end)
        end

        local dragging, dragStart, startPosition
        self:Connect(top.InputBegan, function(input)
            if input.UserInputType == Enum.UserInputType.Touch
                or input.UserInputType == Enum.UserInputType.MouseButton1 then
                dragging = true
                dragStart = input.Position
                startPosition = main.Position
            end
        end)
        self:Connect(UserInputService.InputChanged, function(input)
            if dragging and (input.UserInputType == Enum.UserInputType.Touch
                or input.UserInputType == Enum.UserInputType.MouseMovement) then
                local delta = input.Position - dragStart
                main.Position = UDim2.new(startPosition.X.Scale, startPosition.X.Offset + delta.X,
                    startPosition.Y.Scale, startPosition.Y.Offset + delta.Y)
            end
        end)
        self:Connect(UserInputService.InputEnded, function(input)
            if input.UserInputType == Enum.UserInputType.Touch
                or input.UserInputType == Enum.UserInputType.MouseButton1 then
                dragging = false
            end
        end)

        local accumulator = 0
        self:Connect(RunService.Heartbeat, function(delta)
            accumulator = accumulator + delta
            if accumulator < 1 then return end
            accumulator = 0
            for _, callback in ipairs(self.PulseCallbacks) do
                local ok, err = pcall(callback)
                if not ok then context.Logger:Warn("UI refresh failed: " .. tostring(err)) end
            end
        end)
        self:ShowTab("Home")
        return self
    end

    function MainWindow:Connect(signal, callback)
        return self.Cleanup:Add(signal:Connect(callback))
    end

    function MainWindow:OnPulse(callback)
        table.insert(self.PulseCallbacks, callback)
    end

    function MainWindow:SetScale(value)
        value = math.clamp(tonumber(value) or 1, 0.75, 1.25)
        self.Scale.Scale = value
    end

    function MainWindow:Notify(message, color)
        if not self.Toast or not self.Toast.Parent then return end
        self._toastGeneration = self._toastGeneration + 1
        local generation = self._toastGeneration
        self.Toast.Text = tostring(message)
        self.Toast.BackgroundColor3 = color
        self.Toast.Visible = true
        task.delay(3, function()
            if generation == self._toastGeneration and self.Toast and self.Toast.Parent then
                self.Toast.Visible = false
            end
        end)
    end

    function MainWindow:ShowTab(name)
        local C = self.Context.Modules["ui/Components"]
        for key, page in pairs(self.Pages) do
            local active = key == name
            page.Visible = active
            self.TabButtons[key].BackgroundColor3 = active and C.Theme.accent or C.Theme.raised
            self.TabButtons[key].TextColor3 = active and C.Theme.text or C.Theme.muted
        end
    end

    function MainWindow:Destroy()
        self.PulseCallbacks = {}
        self.Cleanup:Destroy()
    end

    return MainWindow
end
