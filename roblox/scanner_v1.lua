-- Roblox Client Scanner V1.1 GUI loader
local Players = game:GetService("Players")
local LocalPlayer = Players.LocalPlayer
if not LocalPlayer then repeat task.wait() until Players.LocalPlayer; LocalPlayer = Players.LocalPlayer end

local function showBootstrapError(message)
    local parent
    if type(gethui) == "function" then
        local ok, result = pcall(gethui)
        if ok then parent = result end
    end
    parent = parent or LocalPlayer:WaitForChild("PlayerGui")

    local old = parent:FindFirstChild("RobloxScannerBootstrapError")
    if old then old:Destroy() end

    local gui = Instance.new("ScreenGui")
    gui.Name = "RobloxScannerBootstrapError"
    gui.ResetOnSpawn = false
    gui.IgnoreGuiInset = true
    gui.DisplayOrder = 999999
    gui.Parent = parent

    local frame = Instance.new("Frame")
    frame.AnchorPoint = Vector2.new(0.5, 0.5)
    frame.Position = UDim2.fromScale(0.5, 0.5)
    frame.Size = UDim2.fromScale(0.86, 0.44)
    frame.BackgroundColor3 = Color3.fromRGB(25, 27, 33)
    frame.BorderSizePixel = 0
    frame.Parent = gui
    Instance.new("UICorner", frame).CornerRadius = UDim.new(0, 12)

    local title = Instance.new("TextLabel")
    title.BackgroundTransparency = 1
    title.Position = UDim2.new(0, 14, 0, 10)
    title.Size = UDim2.new(1, -28, 0, 28)
    title.Font = Enum.Font.GothamBold
    title.Text = "SCANNER V1 - ERROR DE CARGA"
    title.TextColor3 = Color3.fromRGB(240, 105, 105)
    title.TextSize = 15
    title.TextXAlignment = Enum.TextXAlignment.Left
    title.Parent = frame

    local text = Instance.new("TextLabel")
    text.BackgroundColor3 = Color3.fromRGB(14, 16, 20)
    text.BorderSizePixel = 0
    text.Position = UDim2.new(0, 14, 0, 48)
    text.Size = UDim2.new(1, -28, 1, -108)
    text.Font = Enum.Font.Code
    text.Text = tostring(message)
    text.TextColor3 = Color3.fromRGB(220, 224, 232)
    text.TextSize = 12
    text.TextWrapped = true
    text.TextXAlignment = Enum.TextXAlignment.Left
    text.TextYAlignment = Enum.TextYAlignment.Top
    text.Parent = frame
    Instance.new("UICorner", text).CornerRadius = UDim.new(0, 8)

    local copy = Instance.new("TextButton")
    copy.Position = UDim2.new(0, 14, 1, -48)
    copy.Size = UDim2.new(1, -28, 0, 36)
    copy.BackgroundColor3 = Color3.fromRGB(64, 94, 158)
    copy.BorderSizePixel = 0
    copy.Font = Enum.Font.GothamBold
    copy.Text = "COPIAR ERROR"
    copy.TextColor3 = Color3.new(1, 1, 1)
    copy.TextSize = 13
    copy.Parent = frame
    Instance.new("UICorner", copy).CornerRadius = UDim.new(0, 8)

    copy.MouseButton1Click:Connect(function()
        local f = type(setclipboard) == "function" and setclipboard or (type(toclipboard) == "function" and toclipboard or nil)
        if f then
            pcall(function() f(tostring(message)) end)
            copy.Text = "ERROR COPIADO"
        else
            copy.Text = "PORTAPAPELES NO DISPONIBLE"
        end
    end)
end

local BASE = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/scanner_v1_parts/part"
local chunks = {}

for i = 1, 5 do
    local ok, result = pcall(function()
        return game:HttpGet(BASE .. tostring(i) .. ".lua.txt")
    end)

    if not ok or type(result) ~= "string" or #result == 0 then
        showBootstrapError("No se pudo descargar part" .. tostring(i) .. ":\n" .. tostring(result))
        return
    end

    chunks[i] = result
end

local source = table.concat(chunks, "\n")
local compiled, compileError = loadstring(source)

if not compiled then
    showBootstrapError("Error compilando la GUI:\n" .. tostring(compileError))
    return
end

local ok, runtimeError = pcall(compiled)
if not ok then
    showBootstrapError("Error ejecutando la GUI:\n" .. tostring(runtimeError))
end
