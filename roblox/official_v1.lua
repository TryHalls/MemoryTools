-- MemoryTools Official V1.0.2 stable loader
local VERSION = "1.0.2"
local BOOTSTRAP_URL = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/official_v1/bootstrap.lua?v=" .. VERSION

local function environment()
    if type(getgenv) == "function" then
        local ok, result = pcall(getgenv)
        if ok and type(result) == "table" then
            return result
        end
    end
    return _G
end

local function errorGui(message)
    local ok = pcall(function()
        local Players = game:GetService("Players")
        local player = Players.LocalPlayer
        local parent
        if type(gethui) == "function" then
            local gotHui, hui = pcall(gethui)
            if gotHui then parent = hui end
        end
        parent = parent or (player and player:FindFirstChildOfClass("PlayerGui"))
        if not parent then
            return
        end
        local old = parent:FindFirstChild("MemoryToolsLoaderError")
        if old then old:Destroy() end
        local gui = Instance.new("ScreenGui")
        gui.Name = "MemoryToolsLoaderError"
        gui.ResetOnSpawn = false
        gui.DisplayOrder = 1000000
        gui.Parent = parent
        local frame = Instance.new("Frame")
        frame.AnchorPoint = Vector2.new(0.5, 0.5)
        frame.Position = UDim2.fromScale(0.5, 0.5)
        frame.Size = UDim2.new(0.88, 0, 0, 210)
        frame.BackgroundColor3 = Color3.fromRGB(22, 25, 31)
        frame.BorderSizePixel = 0
        frame.Parent = gui
        Instance.new("UICorner", frame).CornerRadius = UDim.new(0, 12)
        local label = Instance.new("TextLabel")
        label.BackgroundTransparency = 1
        label.Position = UDim2.new(0, 16, 0, 14)
        label.Size = UDim2.new(1, -32, 1, -28)
        label.Font = Enum.Font.Code
        label.Text = "MEMORYTOOLS V" .. VERSION .. " - LOAD ERROR\n\n" .. tostring(message)
        label.TextColor3 = Color3.fromRGB(255, 170, 170)
        label.TextSize = 13
        label.TextWrapped = true
        label.TextXAlignment = Enum.TextXAlignment.Left
        label.TextYAlignment = Enum.TextYAlignment.Top
        label.Parent = frame
    end)
    if not ok then
        warn("MemoryTools loader error: " .. tostring(message))
    end
end

local env = environment()
if type(env.__MEMORYTOOLS_V1) == "table" and type(env.__MEMORYTOOLS_V1.Destroy) == "function" then
    pcall(function()
        env.__MEMORYTOOLS_V1:Destroy("reloaded")
    end)
end

local ok, result = xpcall(function()
    local downloaded, source
    for attempt = 1, 2 do
        downloaded, source = pcall(function()
            return game:HttpGet(BOOTSTRAP_URL)
        end)
        if downloaded and type(source) == "string" and source ~= "" then break end
        if attempt < 2 then task.wait(0.15) end
    end
    if not downloaded or type(source) ~= "string" or source == "" then
        error("Could not download bootstrap: " .. tostring(source))
    end
    local chunk, compileError = loadstring(source)
    if not chunk then
        error("Could not compile bootstrap: " .. tostring(compileError))
    end
    return chunk()
end, function(err)
    return tostring(err)
end)

if not ok or type(result) ~= "table" then
    errorGui(ok and "Bootstrap returned an invalid result" or result)
    return nil
end

env.__MEMORYTOOLS_V1 = result
return result
