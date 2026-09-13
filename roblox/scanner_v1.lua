-- Roblox Client Scanner V1.3 GUI loader
local BASE = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/scanner_v1_parts/part"
local VERSION = "1.3.0"

local function showError(message)
    local Players = game:GetService("Players")
    local player = Players.LocalPlayer
    if not player then return end

    local parent = player:FindFirstChildOfClass("PlayerGui")
    if type(gethui) == "function" then
        local ok, result = pcall(gethui)
        if ok and result then parent = result end
    end
    if not parent then return end

    local old = parent:FindFirstChild("RobloxScannerLoaderError")
    if old then old:Destroy() end

    local gui = Instance.new("ScreenGui")
    gui.Name = "RobloxScannerLoaderError"
    gui.ResetOnSpawn = false
    gui.DisplayOrder = 1000000
    gui.Parent = parent

    local frame = Instance.new("Frame")
    frame.AnchorPoint = Vector2.new(0.5, 0.5)
    frame.Position = UDim2.fromScale(0.5, 0.5)
    frame.Size = UDim2.fromScale(0.86, 0.38)
    frame.BackgroundColor3 = Color3.fromRGB(24, 26, 31)
    frame.BorderSizePixel = 0
    frame.Parent = gui
    Instance.new("UICorner", frame).CornerRadius = UDim.new(0, 10)

    local title = Instance.new("TextLabel")
    title.BackgroundTransparency = 1
    title.Position = UDim2.new(0, 14, 0, 10)
    title.Size = UDim2.new(1, -28, 0, 28)
    title.Font = Enum.Font.GothamBold
    title.Text = "SCANNER V1.3 - ERROR"
    title.TextColor3 = Color3.fromRGB(255, 120, 120)
    title.TextSize = 15
    title.TextXAlignment = Enum.TextXAlignment.Left
    title.Parent = frame

    local body = Instance.new("TextLabel")
    body.BackgroundTransparency = 1
    body.Position = UDim2.new(0, 14, 0, 45)
    body.Size = UDim2.new(1, -28, 1, -100)
    body.Font = Enum.Font.Code
    body.Text = tostring(message)
    body.TextColor3 = Color3.fromRGB(225, 228, 235)
    body.TextSize = 12
    body.TextWrapped = true
    body.TextXAlignment = Enum.TextXAlignment.Left
    body.TextYAlignment = Enum.TextYAlignment.Top
    body.Parent = frame

    local copy = Instance.new("TextButton")
    copy.AnchorPoint = Vector2.new(0.5, 1)
    copy.Position = UDim2.new(0.5, 0, 1, -12)
    copy.Size = UDim2.new(1, -28, 0, 38)
    copy.BackgroundColor3 = Color3.fromRGB(65, 88, 145)
    copy.BorderSizePixel = 0
    copy.Font = Enum.Font.GothamBold
    copy.Text = "COPIAR ERROR"
    copy.TextColor3 = Color3.new(1, 1, 1)
    copy.TextSize = 13
    copy.Parent = frame
    Instance.new("UICorner", copy).CornerRadius = UDim.new(0, 8)

    copy.MouseButton1Click:Connect(function()
        local clipboard = type(setclipboard) == "function" and setclipboard or (type(toclipboard) == "function" and toclipboard or nil)
        if clipboard then
            pcall(function()
                clipboard(tostring(message))
            end)
        end
    end)
end

local ok, result = xpcall(function()
    local chunks = {}

    for i = 1, 5 do
        local url = BASE .. tostring(i) .. ".lua.txt?v=" .. VERSION
        local success, source = pcall(function()
            return game:HttpGet(url)
        end)

        if not success or type(source) ~= "string" or #source == 0 then
            error("No se pudo descargar part" .. tostring(i) .. ": " .. tostring(source))
        end

        chunks[i] = source
    end

    local source = table.concat(chunks, "\n")
    local compiled, compileError = loadstring(source)

    if not compiled then
        error("Error compilando la GUI:\n" .. tostring(compileError))
    end

    return compiled()
end, function(err)
    return tostring(err)
end)

if not ok then
    showError(result)
    return nil
end

return result
