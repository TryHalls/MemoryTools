--[[
    Roblox Client Scanner V1
    Passive client-side reconnaissance scanner.
    Does not invoke remotes, require modules, decompile scripts, or modify game instances.
]]

local CONFIG = {
    VERSION = "1.0.0",
    MAX_REPORT_CHARS = 450000,
    MAX_STRING_LENGTH = 220,
    YIELD_EVERY = 150,
    MAX_CHILDREN_PER_NODE = 150,
    INCLUDE_TREE = true,
    PRINT_FULL_REPORT = false,

    ROOT_LIMITS = {
        ReplicatedStorage = { maxDepth = 7, maxObjects = 2500 },
        ReplicatedFirst = { maxDepth = 6, maxObjects = 1000 },
        Workspace = { maxDepth = 5, maxObjects = 2500 },
        Character = { maxDepth = 6, maxObjects = 1200 },
        Backpack = { maxDepth = 5, maxObjects = 800 },
        PlayerGui = { maxDepth = 7, maxObjects = 2200 },
        PlayerScripts = { maxDepth = 6, maxObjects = 1200 },
        Lighting = { maxDepth = 4, maxObjects = 600 },
        SoundService = { maxDepth = 5, maxObjects = 800 },
        Teams = { maxDepth = 4, maxObjects = 400 },
    }
}

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local ReplicatedFirst = game:GetService("ReplicatedFirst")
local CollectionService = game:GetService("CollectionService")
local Lighting = game:GetService("Lighting")
local SoundService = game:GetService("SoundService")
local Teams = game:GetService("Teams")
local Workspace = game:GetService("Workspace")

local LocalPlayer = Players.LocalPlayer
if not LocalPlayer then
    repeat
        task.wait()
        LocalPlayer = Players.LocalPlayer
    until LocalPlayer
end

local reportLines = {}
local reportChars = 0
local reportTruncated = false
local processedObjects = 0

local globalStats = {
    totalObjects = 0,
    RemoteEvent = 0,
    RemoteFunction = 0,
    ModuleScript = 0,
    LocalScript = 0,
    Script = 0,
    Tool = 0,
    ProximityPrompt = 0,
    ClickDetector = 0,
    GuiObject = 0,
    ValueBase = 0,
    Attributes = 0,
    TaggedObjects = 0,
}

local interestingObjects = {}
local warnings = {}

local function safeCall(callback, fallback)
    local success, result = pcall(callback)
    if success then
        return result
    end
    return fallback
end

local function sanitizeString(value)
    local text = tostring(value)
    text = text:gsub("\r", "\\r")
    text = text:gsub("\n", "\\n")
    text = text:gsub("\t", "\\t")

    if #text > CONFIG.MAX_STRING_LENGTH then
        text = text:sub(1, CONFIG.MAX_STRING_LENGTH) .. "...[TRUNCATED]"
    end

    return text
end

local function addLine(text)
    if reportTruncated then
        return
    end

    text = tostring(text or "")
    local requiredLength = #text + 1

    if reportChars + requiredLength > CONFIG.MAX_REPORT_CHARS then
        reportTruncated = true
        table.insert(reportLines, "[REPORT TRUNCATED: MAX_REPORT_CHARS reached]")
        return
    end

    table.insert(reportLines, text)
    reportChars += requiredLength
end

local function addWarning(text)
    table.insert(warnings, tostring(text))
end

local function safeFullName(instance)
    return safeCall(function()
        return instance:GetFullName()
    end, instance.Name)
end

local function formatVector3(vector)
    return string.format("%.2f, %.2f, %.2f", vector.X, vector.Y, vector.Z)
end

local function serializeValue(value)
    local valueType = typeof(value)

    if value == nil then
        return "nil"
    elseif valueType == "string" then
        return '"' .. sanitizeString(value) .. '"'
    elseif valueType == "number" or valueType == "boolean" then
        return tostring(value)
    elseif valueType == "Vector3" then
        return "Vector3(" .. formatVector3(value) .. ")"
    elseif valueType == "Vector2" then
        return string.format("Vector2(%.2f, %.2f)", value.X, value.Y)
    elseif valueType == "CFrame" then
        return "CFrame(" .. formatVector3(value.Position) .. ")"
    elseif valueType == "Color3" then
        return string.format("Color3(%.3f, %.3f, %.3f)", value.R, value.G, value.B)
    elseif valueType == "UDim2" then
        return tostring(value)
    elseif valueType == "EnumItem" then
        return tostring(value)
    elseif valueType == "Instance" then
        return safeFullName(value)
    end

    return sanitizeString(value)
end

local function getAttributes(instance)
    local attributes = safeCall(function()
        return instance:GetAttributes()
    end, {})

    if type(attributes) ~= "table" then
        return {}
    end

    return attributes
end

local function getTags(instance)
    return safeCall(function()
        return CollectionService:GetTags(instance)
    end, {})
end

local function sortedKeys(tbl)
    local result = {}
    for key in pairs(tbl) do
        table.insert(result, key)
    end

    table.sort(result, function(a, b)
        return tostring(a) < tostring(b)
    end)

    return result
end

local function isInteresting(instance)
    if instance:IsA("RemoteEvent") then return true end
    if instance:IsA("RemoteFunction") then return true end
    if instance:IsA("ModuleScript") then return true end
    if instance:IsA("LocalScript") then return true end
    if instance:IsA("Script") then return true end
    if instance:IsA("Tool") then return true end
    if instance:IsA("ProximityPrompt") then return true end
    if instance:IsA("ClickDetector") then return true end
    if instance:IsA("ValueBase") then return true end
    if instance:IsA("ScreenGui") then return true end
    if instance:IsA("TextButton") then return true end
    if instance:IsA("TextLabel") then return true end
    if instance:IsA("TextBox") then return true end
    if instance:IsA("Humanoid") then return true end

    local attributes = getAttributes(instance)
    if next(attributes) ~= nil then return true end

    local tags = getTags(instance)
    if #tags > 0 then return true end

    return false
end

local function registerStats(instance)
    globalStats.totalObjects += 1

    if instance:IsA("RemoteEvent") then globalStats.RemoteEvent += 1 end
    if instance:IsA("RemoteFunction") then globalStats.RemoteFunction += 1 end
    if instance:IsA("ModuleScript") then globalStats.ModuleScript += 1 end
    if instance:IsA("LocalScript") then globalStats.LocalScript += 1 end
    if instance:IsA("Script") then globalStats.Script += 1 end
    if instance:IsA("Tool") then globalStats.Tool += 1 end
    if instance:IsA("ProximityPrompt") then globalStats.ProximityPrompt += 1 end
    if instance:IsA("ClickDetector") then globalStats.ClickDetector += 1 end
    if instance:IsA("GuiObject") then globalStats.GuiObject += 1 end
    if instance:IsA("ValueBase") then globalStats.ValueBase += 1 end

    local attributes = getAttributes(instance)
    if next(attributes) ~= nil then globalStats.Attributes += 1 end

    local tags = getTags(instance)
    if #tags > 0 then globalStats.TaggedObjects += 1 end
end

local function collectExtraProperties(instance)
    local properties = {}

    if instance:IsA("ValueBase") then
        local value = safeCall(function() return instance.Value end)
        properties.Value = serializeValue(value)
    end

    if instance:IsA("Tool") then
        properties.RequiresHandle = tostring(safeCall(function() return instance.RequiresHandle end, "unavailable"))
        properties.CanBeDropped = tostring(safeCall(function() return instance.CanBeDropped end, "unavailable"))
        properties.ToolTip = sanitizeString(safeCall(function() return instance.ToolTip end, ""))
    end

    if instance:IsA("ProximityPrompt") then
        properties.ActionText = sanitizeString(safeCall(function() return instance.ActionText end, ""))
        properties.ObjectText = sanitizeString(safeCall(function() return instance.ObjectText end, ""))
        properties.HoldDuration = tostring(safeCall(function() return instance.HoldDuration end, "unavailable"))
        properties.MaxActivationDistance = tostring(safeCall(function() return instance.MaxActivationDistance end, "unavailable"))
        properties.RequiresLineOfSight = tostring(safeCall(function() return instance.RequiresLineOfSight end, "unavailable"))
    end

    if instance:IsA("ClickDetector") then
        properties.MaxActivationDistance = tostring(safeCall(function() return instance.MaxActivationDistance end, "unavailable"))
    end

    if instance:IsA("ScreenGui") then
        properties.Enabled = tostring(safeCall(function() return instance.Enabled end, "unavailable"))
        properties.DisplayOrder = tostring(safeCall(function() return instance.DisplayOrder end, "unavailable"))
    end

    if instance:IsA("GuiObject") then
        properties.Visible = tostring(safeCall(function() return instance.Visible end, "unavailable"))
    end

    if instance:IsA("TextLabel") or instance:IsA("TextButton") or instance:IsA("TextBox") then
        properties.Text = sanitizeString(safeCall(function() return instance.Text end, ""))
    end

    if instance:IsA("Humanoid") then
        properties.Health = tostring(safeCall(function() return instance.Health end, "unavailable"))
        properties.MaxHealth = tostring(safeCall(function() return instance.MaxHealth end, "unavailable"))
        properties.RigType = tostring(safeCall(function() return instance.RigType end, "unavailable"))
    end

    if instance:IsA("LocalScript") or instance:IsA("Script") then
        properties.Enabled = tostring(safeCall(function() return instance.Enabled end, "unavailable"))
    end

    return properties
end

local function registerInterestingObject(instance)
    table.insert(interestingObjects, {
        className = instance.ClassName,
        name = instance.Name,
        path = safeFullName(instance),
        attributes = getAttributes(instance),
        tags = getTags(instance),
        properties = collectExtraProperties(instance),
    })
end

local function scanRoot(rootName, root, limits)
    if not root then
        addWarning(rootName .. " unavailable")
        return { name = rootName, scanned = 0, truncated = false }
    end

    local queue = {{ object = root, depth = 0 }}
    local head = 1
    local scanned = 0
    local truncated = false
    local classCounts = {}

    addLine("")
    addLine("==================================================")
    addLine("[ROOT] " .. rootName)
    addLine("Path: " .. safeFullName(root))
    addLine("Limits: depth=" .. tostring(limits.maxDepth) .. " objects=" .. tostring(limits.maxObjects))
    addLine("==================================================")

    while head <= #queue do
        if scanned >= limits.maxObjects then
            truncated = true
            break
        end

        local current = queue[head]
        head += 1

        local instance = current.object
        local depth = current.depth

        scanned += 1
        processedObjects += 1
        registerStats(instance)
        classCounts[instance.ClassName] = (classCounts[instance.ClassName] or 0) + 1

        if CONFIG.INCLUDE_TREE then
            local indent = string.rep("  ", depth)
            addLine(indent .. "- " .. sanitizeString(instance.Name) .. " [" .. instance.ClassName .. "]")
        end

        local success, interesting = pcall(isInteresting, instance)
        if success and interesting then
            registerInterestingObject(instance)
        end

        if depth < limits.maxDepth then
            local children = safeCall(function()
                return instance:GetChildren()
            end, {})

            local childCount = #children

            if childCount > CONFIG.MAX_CHILDREN_PER_NODE then
                addLine(string.rep("  ", depth + 1) .. "[CHILDREN TRUNCATED: " .. tostring(childCount) .. " -> " .. tostring(CONFIG.MAX_CHILDREN_PER_NODE) .. "]")
                childCount = CONFIG.MAX_CHILDREN_PER_NODE
            end

            for i = 1, childCount do
                if scanned + (#queue - head + 1) >= limits.maxObjects then
                    truncated = true
                    break
                end

                table.insert(queue, {
                    object = children[i],
                    depth = depth + 1,
                })
            end
        end

        if processedObjects % CONFIG.YIELD_EVERY == 0 then
            task.wait()
        end
    end

    addLine("")
    addLine("[ROOT SUMMARY]")
    addLine("Scanned objects: " .. tostring(scanned))
    addLine("Truncated: " .. tostring(truncated))
    addLine("")
    addLine("[CLASS COUNTS]")

    for _, className in ipairs(sortedKeys(classCounts)) do
        addLine(className .. ": " .. tostring(classCounts[className]))
    end

    if truncated then
        addWarning(rootName .. " reached scanning limits; result is incomplete.")
    end

    return { name = rootName, scanned = scanned, truncated = truncated }
end

addLine("ROBLOX CLIENT SCANNER V1")
addLine("Version: " .. CONFIG.VERSION)
addLine("")
addLine("==================================================")
addLine("[ENVIRONMENT]")
addLine("==================================================")
addLine("PlaceId: " .. tostring(game.PlaceId))
addLine("GameId: " .. tostring(game.GameId))
addLine("JobId: " .. sanitizeString(safeCall(function() return game.JobId end, "unavailable")))
addLine("PlaceVersion: " .. tostring(safeCall(function() return game.PlaceVersion end, "unavailable")))
addLine("CreatorId: " .. tostring(safeCall(function() return game.CreatorId end, "unavailable")))

local streamingEnabled = safeCall(function()
    return Workspace.StreamingEnabled
end, "unavailable")

addLine("Workspace.StreamingEnabled: " .. tostring(streamingEnabled))

if streamingEnabled == true then
    addWarning("Workspace.StreamingEnabled=true. Workspace represents only content currently replicated/streamed to this client.")
end

addLine("")
addLine("==================================================")
addLine("[LOCAL PLAYER]")
addLine("==================================================")
addLine("Name: " .. sanitizeString(LocalPlayer.Name))
addLine("DisplayName: " .. sanitizeString(safeCall(function() return LocalPlayer.DisplayName end, "")))
addLine("UserId: " .. tostring(LocalPlayer.UserId))
addLine("Character: " .. (LocalPlayer.Character and safeFullName(LocalPlayer.Character) or "nil"))

addLine("")
addLine("==================================================")
addLine("[DATAMODEL TOP LEVEL]")
addLine("==================================================")

local gameChildren = safeCall(function()
    return game:GetChildren()
end, {})

for _, child in ipairs(gameChildren) do
    addLine("- " .. sanitizeString(child.Name) .. " [" .. child.ClassName .. "]")
end

local roots = {}

local function addRoot(name, instance)
    if instance then
        table.insert(roots, { name = name, instance = instance })
    else
        addWarning(name .. " not available")
    end
end

addRoot("ReplicatedStorage", ReplicatedStorage)
addRoot("ReplicatedFirst", ReplicatedFirst)
addRoot("Workspace", Workspace)
addRoot("Lighting", Lighting)
addRoot("SoundService", SoundService)
addRoot("Teams", Teams)

local Character = LocalPlayer.Character
local Backpack = safeCall(function() return LocalPlayer:FindFirstChildOfClass("Backpack") end)
local PlayerGui = safeCall(function() return LocalPlayer:FindFirstChildOfClass("PlayerGui") end)
local PlayerScripts = safeCall(function() return LocalPlayer:FindFirstChild("PlayerScripts") end)

addRoot("Character", Character)
addRoot("Backpack", Backpack)
addRoot("PlayerGui", PlayerGui)
addRoot("PlayerScripts", PlayerScripts)

local rootResults = {}

for _, rootData in ipairs(roots) do
    local limits = CONFIG.ROOT_LIMITS[rootData.name]
    if limits then
        table.insert(rootResults, scanRoot(rootData.name, rootData.instance, limits))
    end
end

addLine("")
addLine("")
addLine("##################################################")
addLine("[INTERESTING OBJECTS]")
addLine("##################################################")

for index, data in ipairs(interestingObjects) do
    addLine("")
    addLine("#" .. tostring(index) .. " [" .. data.className .. "]")
    addLine("Name: " .. sanitizeString(data.name))
    addLine("Path: " .. sanitizeString(data.path))

    if next(data.attributes) ~= nil then
        addLine("Attributes:")
        for _, key in ipairs(sortedKeys(data.attributes)) do
            addLine("  " .. tostring(key) .. " = " .. serializeValue(data.attributes[key]))
        end
    end

    if #data.tags > 0 then
        addLine("Tags: " .. table.concat(data.tags, ", "))
    end

    if next(data.properties) ~= nil then
        addLine("Properties:")
        for _, key in ipairs(sortedKeys(data.properties)) do
            addLine("  " .. tostring(key) .. " = " .. sanitizeString(data.properties[key]))
        end
    end
end

addLine("")
addLine("")
addLine("##################################################")
addLine("[COLLECTION SERVICE TAGS]")
addLine("##################################################")

local allTags = safeCall(function()
    return CollectionService:GetAllTags()
end, {})

table.sort(allTags)

if #allTags == 0 then
    addLine("No tags visible.")
else
    for _, tag in ipairs(allTags) do
        local taggedInstances = safeCall(function()
            return CollectionService:GetTagged(tag)
        end, {})
        addLine(tag .. ": " .. tostring(#taggedInstances) .. " visible instance(s)")
    end
end

addLine("")
addLine("")
addLine("##################################################")
addLine("[GLOBAL SUMMARY]")
addLine("##################################################")
addLine("Objects scanned: " .. tostring(globalStats.totalObjects))
addLine("Interesting objects: " .. tostring(#interestingObjects))
addLine("RemoteEvents: " .. tostring(globalStats.RemoteEvent))
addLine("RemoteFunctions: " .. tostring(globalStats.RemoteFunction))
addLine("ModuleScripts: " .. tostring(globalStats.ModuleScript))
addLine("LocalScripts: " .. tostring(globalStats.LocalScript))
addLine("Scripts visible: " .. tostring(globalStats.Script))
addLine("Tools: " .. tostring(globalStats.Tool))
addLine("ProximityPrompts: " .. tostring(globalStats.ProximityPrompt))
addLine("ClickDetectors: " .. tostring(globalStats.ClickDetector))
addLine("GUI objects: " .. tostring(globalStats.GuiObject))
addLine("ValueBase objects: " .. tostring(globalStats.ValueBase))
addLine("Objects with attributes: " .. tostring(globalStats.Attributes))
addLine("Tagged objects: " .. tostring(globalStats.TaggedObjects))

addLine("")
addLine("")
addLine("##################################################")
addLine("[WARNINGS / LIMITATIONS]")
addLine("##################################################")
addLine("This report contains only information visible to the current client.")
addLine("ServerStorage and ServerScriptService contents are not expected to be available to a normal client.")
addLine("No RemoteEvents or RemoteFunctions were invoked.")
addLine("No ModuleScripts were required.")
addLine("No scripts were decompiled.")
addLine("No game instances were intentionally modified.")

if #warnings == 0 then
    addLine("No scanner warnings.")
else
    for _, warningText in ipairs(warnings) do
        addLine("- " .. sanitizeString(warningText))
    end
end

if reportTruncated then
    addLine("- Report reached MAX_REPORT_CHARS.")
end

local reportText = table.concat(reportLines, "\n")
local exportedToClipboard = false
local exportedToFile = false

if type(setclipboard) == "function" then
    local success = pcall(function()
        setclipboard(reportText)
    end)
    exportedToClipboard = success
end

if type(writefile) == "function" then
    local fileName = "RobloxScannerV1_" .. tostring(game.PlaceId) .. ".txt"
    local success = pcall(function()
        writefile(fileName, reportText)
    end)
    exportedToFile = success

    if success then
        print("[Scanner V1] Saved as: " .. fileName)
    end
end

print("")
print("==============================================")
print(" ROBLOX CLIENT SCANNER V1 COMPLETE")
print("==============================================")
print("Objects scanned: " .. tostring(globalStats.totalObjects))
print("Interesting objects: " .. tostring(#interestingObjects))
print("Remotes: " .. tostring(globalStats.RemoteEvent + globalStats.RemoteFunction))
print("Modules: " .. tostring(globalStats.ModuleScript))
print("Report characters: " .. tostring(#reportText))
print("Clipboard: " .. tostring(exportedToClipboard))
print("File: " .. tostring(exportedToFile))
print("==============================================")

if CONFIG.PRINT_FULL_REPORT or (not exportedToClipboard and not exportedToFile) then
    print(reportText)
end

return reportText
