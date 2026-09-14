    end
    if terminalText then
        terminalText.Text = terminalBuffer
        updateTerminalCanvas(false)
    end
end


local function terminalLine(level, message)
    terminalAppend(string.format("[%s] %s", level, tostring(message)))
end

local function setFeedback(message, seconds)
    if not feedbackText then
        return
    end
    feedbackText.Text = message
    local expected = message
    if seconds then
        task.delay(seconds, function()
            if session.Active and feedbackText and feedbackText.Text == expected then
                feedbackText.Text = ""
            end
        end)
    end
end

local function stylePanel(instance, radius)
    local corner = Instance.new("UICorner")
    corner.CornerRadius = UDim.new(0, radius)
    corner.Parent = instance
    local stroke = Instance.new("UIStroke")
    stroke.Color = Color3.fromRGB(65, 88, 112)
    stroke.Thickness = 1
    stroke.Parent = instance
end

local function makeButton(parent, text, position, size)
    local button = Instance.new("TextButton")
    button.BackgroundColor3 = Color3.fromRGB(35, 62, 83)
    button.Position = position
    button.Size = size
    button.Font = Enum.Font.Code
    button.Text = text
    button.TextColor3 = Color3.fromRGB(225, 240, 250)
    button.TextSize = 12
    button.AutoButtonColor = true
    button.Parent = parent
    stylePanel(button, 7)
    return button
end

local function createGui()
    local parent
    local getHiddenUi = globalFunction("gethui")
    if getHiddenUi then
        local ok, value = pcall(getHiddenUi)
        if ok and typeof(value) == "Instance" then
            parent = value
        end
    end
    if not parent then
        parent = LocalPlayer:FindFirstChildOfClass("PlayerGui")
            or LocalPlayer:WaitForChild("PlayerGui", 10)
    end
    if not parent then
        return nil
    end

    local gui = Instance.new("ScreenGui")
    gui.Name = "MemoryToolsCarryRunbackProbe"
    gui.ResetOnSpawn = false
    gui.DisplayOrder = 750
    gui.IgnoreGuiInset = false
    gui.Enabled = true
    gui.Parent = parent

    mainFrame = Instance.new("Frame")
    mainFrame.Name = "Main"
    mainFrame.AnchorPoint = Vector2.new(0.5, 0.5)
    mainFrame.Position = UDim2.fromScale(0.5, 0.5)
    mainFrame.Size = UDim2.fromScale(0.88, 0.7)
    mainFrame.BackgroundColor3 = Color3.fromRGB(15, 23, 31)
    mainFrame.Parent = gui
    stylePanel(mainFrame, 10)

    local header = Instance.new("TextLabel")
    header.BackgroundTransparency = 1
    header.Position = UDim2.new(0, 12, 0, 5)
    header.Size = UDim2.new(1, -24, 0, 34)
    header.Font = Enum.Font.Code
    header.Text = "MemoryTools Carry Runback Probe V1.0.6"
    header.TextColor3 = Color3.fromRGB(110, 230, 170)
    header.TextSize = 16
    header.TextXAlignment = Enum.TextXAlignment.Left
    header.Parent = mainFrame

    statusText = Instance.new("TextLabel")
    statusText.Name = "Status"
    statusText.BackgroundColor3 = Color3.fromRGB(20, 31, 42)
    statusText.Position = UDim2.new(0, 10, 0, 42)
    statusText.Size = UDim2.new(1, -20, 0, 72)
    statusText.Font = Enum.Font.Code
    statusText.TextColor3 = Color3.fromRGB(205, 220, 230)
    statusText.TextSize = 12
    statusText.TextWrapped = true
    statusText.TextXAlignment = Enum.TextXAlignment.Left
    statusText.TextYAlignment = Enum.TextYAlignment.Top
    statusText.Parent = mainFrame
    stylePanel(statusText, 6)
    local statusPadding = Instance.new("UIPadding")
    statusPadding.PaddingLeft = UDim.new(0, 8)
    statusPadding.PaddingTop = UDim.new(0, 6)
    statusPadding.Parent = statusText

    minimizeButton = makeButton(mainFrame, "MINIMIZE", UDim2.new(0, 10, 0, 121), UDim2.new(0.29, -5, 0, 38))
    generateButton = makeButton(mainFrame, "GENERATE REPORT", UDim2.new(0.29, 10, 0, 121), UDim2.new(0.42, -10, 0, 38))
    copyButton = makeButton(mainFrame, "COPY REPORT", UDim2.new(0.71, 5, 0, 121), UDim2.new(0.29, -15, 0, 38))

    feedbackText = Instance.new("TextLabel")
    feedbackText.BackgroundTransparency = 1
    feedbackText.Position = UDim2.new(0, 12, 0, 163)
    feedbackText.Size = UDim2.new(1, -24, 0, 22)
    feedbackText.Font = Enum.Font.Code
    feedbackText.Text = ""
    feedbackText.TextColor3 = Color3.fromRGB(255, 215, 95)
    feedbackText.TextSize = 12
    feedbackText.TextXAlignment = Enum.TextXAlignment.Left
    feedbackText.Parent = mainFrame

    terminalScroll = Instance.new("ScrollingFrame")
    terminalScroll.Name = "Terminal"
    terminalScroll.BackgroundColor3 = Color3.fromRGB(7, 12, 17)
    terminalScroll.Position = UDim2.new(0, 10, 0, 187)
    terminalScroll.Size = UDim2.new(1, -20, 1, -197)
    terminalScroll.CanvasSize = UDim2.fromOffset(0, 0)
    terminalScroll.ScrollBarThickness = 8
    terminalScroll.ScrollingDirection = Enum.ScrollingDirection.XY
    terminalScroll.Parent = mainFrame
    stylePanel(terminalScroll, 6)

    terminalText = Instance.new("TextLabel")
    terminalText.BackgroundTransparency = 1
    terminalText.Position = UDim2.fromOffset(8, 8)
    terminalText.Size = UDim2.new(1, -16, 1, -16)
    terminalText.Font = Enum.Font.Code
    terminalText.Text = terminalBuffer
    terminalText.TextColor3 = Color3.fromRGB(178, 225, 195)
    terminalText.TextSize = 13
    terminalText.TextWrapped = false
    terminalText.TextXAlignment = Enum.TextXAlignment.Left
    terminalText.TextYAlignment = Enum.TextYAlignment.Top
    terminalText.Parent = terminalScroll

    miniButton = makeButton(gui, "RUNBACK PROBE", UDim2.new(1, -154, 0.18, 0), UDim2.fromOffset(144, 42))
    miniButton.Name = "Restore"
    miniButton.Visible = false

    rememberConnection(minimizeButton.Activated:Connect(function()
        if session.Active then
            mainFrame.Visible = false
            miniButton.Visible = true
        end
    end))
    rememberConnection(miniButton.Activated:Connect(function()
        if session.Active then
            mainFrame.Visible = true
            miniButton.Visible = false
