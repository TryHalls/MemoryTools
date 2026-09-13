-- MemoryTools Compact Passive Probe V1.0.2.
-- Decompilation and source matching only. It does not load modules, send
-- remotes, enumerate live connections/GC objects, hook APIs, write files, or
-- alter gameplay.

local VERSION = "1.0.2"
local MAX_SCRIPTS = 5000
local YIELD_EVERY = 25
local MAX_REPORT_BYTES = 29500

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local LocalPlayer = Players.LocalPlayer

local result = {
    Version = VERSION,
    Scanned = 0,
    Report = "",
    MissingEvidence = {},
}

local sections = {
    STAFF = { Title = "STAFF STATUS", Budget = 3000, Parts = {}, Bytes = 0, Seen = {} },
    WALK = { Title = "WALK SPEED", Budget = 3400, Parts = {}, Bytes = 0, Seen = {} },
    SPEED = { Title = "SPEED POWER", Budget = 4000, Parts = {}, Bytes = 0, Seen = {} },
    RIG = { Title = "RIG SYNC / TELEPORT", Budget = 6200, Parts = {}, Bytes = 0, Seen = {} },
    CMDR = { Title = "CMDR MOVEMENT", Budget = 4400, Parts = {}, Bytes = 0, Seen = {} },
    RARITY = { Title = "RARITY MAPPING", Budget = 5200, Parts = {}, Bytes = 0, Seen = {} },
}

local function missing(message)
    table.insert(result.MissingEvidence, tostring(message))
end

local function safePath(instance)
    local ok, value = pcall(function() return instance:GetFullName() end)
    return ok and value or tostring(instance)
end

local function safeFind(root, parts)
    local current = root
    for _, name in ipairs(parts) do
        current = current and current:FindFirstChild(name) or nil
    end
    return current
end

local function safeDescendants(root)
    if not root then return {} end
    local ok, values = pcall(function() return root:GetDescendants() end)
    return ok and values or {}
end

local function findNamed(root, name)
    if not root then return nil end
    local direct = root:FindFirstChild(name)
    if direct then return direct end
    for _, descendant in ipairs(safeDescendants(root)) do
        if descendant.Name == name then return descendant end
    end
    return nil
end

local function isScript(instance)
    return instance and (instance:IsA("ModuleScript") or instance:IsA("LocalScript"))
end

local function sourceFor(instance)
    local ok, source = pcall(decompile, instance)
    if not ok or type(source) ~= "string" or source == "" then
        return nil, tostring(source or "empty source")
    end
    return source
end

local function splitLines(source)
    local lines = {}
    source = source:gsub("\r\n", "\n")
    if source:sub(-1) ~= "\n" then source = source .. "\n" end
    for line in source:gmatch("(.-)\n") do table.insert(lines, line) end
    return lines
end

local function containsAny(lower, terms)
    for _, term in ipairs(terms) do
        if lower:find(term, 1, true) then return true end
    end
    return false
end

local function addSection(section, key, text)
    if section.Seen[key] then return end
    section.Seen[key] = true
    if section.Bytes + #text + 2 > section.Budget then
        if not section.Truncated then
            section.Truncated = true
            missing(section.Title .. " exceeded its compact output budget")
        end
        return
    end
    table.insert(section.Parts, text)
    section.Bytes = section.Bytes + #text + 2
end

local function windowsFor(lines, markers, radius, maxWindows)
    local hits = {}
    for index, line in ipairs(lines) do
        local lower = line:lower()
        if containsAny(lower, markers) then table.insert(hits, index) end
    end
    local windows = {}
    for _, index in ipairs(hits) do
        local first = math.max(1, index - radius)
        local last = math.min(#lines, index + radius)
        local previous = windows[#windows]
        if previous and first <= previous.Last + 1 then
            previous.Last = math.max(previous.Last, last)
        else
            table.insert(windows, { First = first, Last = last })
            if #windows >= maxWindows then break end
        end
    end
    return windows
end

local function contextText(path, lines, window)
    local out = { "-- " .. path .. " :: lines " .. window.First .. "-" .. window.Last }
    for index = window.First, window.Last do
        table.insert(out, string.format("%5d | %s", index, lines[index]))
    end
    return table.concat(out, "\n")
end

local function capture(section, path, lines, markers, radius, maxWindows)
    local windows = windowsFor(lines, markers, radius, maxWindows)
    for _, window in ipairs(windows) do
        local key = path .. ":" .. window.First .. ":" .. window.Last
        addSection(section, key, contextText(path, lines, window))
    end
    return #windows
end

local exact = {}
local function exactTarget(instance, label)
    if not instance then
        missing(label .. " not found")
    elseif isScript(instance) then
        exact[instance] = label
    else
        local found = 0
        for _, descendant in ipairs(safeDescendants(instance)) do
            if isScript(descendant) then
                exact[descendant] = label .. " descendant"
                found = found + 1
            end
        end
        if found == 0 then missing(label .. " has no decompilable script") end
    end
end

local playerScripts = LocalPlayer and LocalPlayer:FindFirstChildOfClass("PlayerScripts") or nil
exactTarget(safeFind(playerScripts, { "GUI", "AdminPanel" }), "PlayerScripts.GUI.AdminPanel")
exactTarget(safeFind(ReplicatedStorage, { "Client", "AdminPanelEntry" }), "Client.AdminPanelEntry")
exactTarget(safeFind(ReplicatedStorage, { "Shared", "Staff" }), "Shared.Staff")
exactTarget(safeFind(ReplicatedStorage, { "Client", "SpeedPowerProjection" }), "Client.SpeedPowerProjection")
exactTarget(safeFind(ReplicatedStorage, { "Shared", "Util", "TreadmillUtil" }), "Shared.Util.TreadmillUtil")
local rareEggHighlight = findNamed(safeFind(ReplicatedStorage, { "Client" }), "RareEggHighlight")
    or findNamed(safeFind(ReplicatedStorage, { "Shared" }), "RareEggHighlight")
    or findNamed(playerScripts, "RareEggHighlight")
exactTarget(rareEggHighlight, "RareEggHighlight")

local candidates = {}
local seen = {}
local function addCandidate(instance)
    if isScript(instance) and not seen[instance] and #candidates < MAX_SCRIPTS then
        seen[instance] = true
        table.insert(candidates, instance)
    end
end
for instance in pairs(exact) do addCandidate(instance) end
for _, root in ipairs({
    safeFind(ReplicatedStorage, { "Client" }),
    safeFind(ReplicatedStorage, { "Shared" }),
    playerScripts,
}) do
    addCandidate(root)
    for _, descendant in ipairs(safeDescendants(root)) do addCandidate(descendant) end
end
if #candidates >= MAX_SCRIPTS then missing("script inspection limit reached: " .. MAX_SCRIPTS) end

local ADMIN_STAFF = { "probestaffstatus", "staffverdict", "setadminstatus" }
local ADMIN_WALK = { "writewalkspeed", "speedpowertowalkspeed" }
local ADMIN_SPEED = {
    "writespeedpower", "speedpowerverdict", "speedpowertowalkspeed", "normalizespeedpower",
}
local STAFF_MODULE = { "require", "staff", "admin", "permission", "rank", "verdict" }
local SPEED_MODULE = { "require", "speedpower", "walkspeed", "treadmill", "attribute" }
local RIG_TRIGGER = { "rigsync" }
local RIG_CONTEXT = {
    "require", "remoteevent", "remotefunction", "fireserver", "invokeserver",
    "onclientevent", "onclientinvoke", "pivotto", "cframe", "position", "humanoid", "root",
}
local CMDR_TERMS = { "teleport", "goto", "bring", "walkspeed", "speedpower", "setspeedpower" }
local CMDR_CONTEXT = {
    "name", "args", "run", "client", "server", "remote", "network", "teleport",
    "target", "player", "character", "pivotto", "cframe", "position",
}
local RARITY_MARKERS = {
    "data.assets", "data.rarity", "directory", "rarity", "rarityname",
    "assetcategory", "highlight", "color",
}

local rarityFound = false
local rigConsumers = 0
local cmdrCommands = 0

if type(decompile) ~= "function" then
    missing("decompile is unavailable in this executor")
else
    for index, instance in ipairs(candidates) do
        result.Scanned = result.Scanned + 1
        local path = safePath(instance)
        local lowerPath = path:lower()
        local source, sourceError = sourceFor(instance)
        if source then
            local lowerSource = source:lower()
            local lines = splitLines(source)
            local label = exact[instance]

            if label and (label:find("AdminPanel", 1, true) or label:find("Shared.Staff", 1, true)) then
                capture(sections.STAFF, path, lines, ADMIN_STAFF, 12, 5)
                capture(sections.WALK, path, lines, ADMIN_WALK, 12, 5)
                capture(sections.SPEED, path, lines, ADMIN_SPEED, 12, 6)
            end
            if label and label:find("Shared.Staff", 1, true) then
                capture(sections.STAFF, path, lines, STAFF_MODULE, 12, 5)
            end
            if label and (label:find("Client.SpeedPowerProjection", 1, true)
                or label:find("Shared.Util.TreadmillUtil", 1, true)) then
                capture(sections.WALK, path, lines, ADMIN_WALK, 12, 4)
                capture(sections.SPEED, path, lines, ADMIN_SPEED, 12, 4)
                capture(sections.SPEED, path, lines, SPEED_MODULE, 12, 5)
            end

            if containsAny(lowerSource, RIG_TRIGGER) then
                rigConsumers = rigConsumers + 1
                capture(sections.RIG, path, lines, RIG_TRIGGER, 12, 3)
                capture(sections.RIG, path, lines, RIG_CONTEXT, 8, 6)
            end

            if lowerPath:find("cmdr", 1, true) and containsAny(lowerSource, CMDR_TERMS) then
                cmdrCommands = cmdrCommands + 1
                capture(sections.CMDR, path, lines, CMDR_TERMS, 12, 4)
                capture(sections.CMDR, path, lines, CMDR_CONTEXT, 8, 6)
            end

            if instance.Name:lower() == "rareegghighlight" or lowerPath:find("rareegghighlight", 1, true) then
                rarityFound = true
                capture(sections.RARITY, path, lines, RARITY_MARKERS, 12, 8)
            end
        elseif exact[instance] then
            missing(path .. " decompile failed: " .. tostring(sourceError))
        end
        if index % YIELD_EVERY == 0 then task.wait() end
    end
end

if rigConsumers == 0 then missing("no RigSync consumer source found") end
if cmdrCommands == 0 then missing("no matching Cmdr movement command source found") end
if not rarityFound then missing("RareEggHighlight source not found") end

local report = {
    "MEMORYTOOLS COMPACT PROBE V1.0.2",
    "PlaceId: " .. tostring(game.PlaceId),
    "PlaceVersion: " .. tostring(game.PlaceVersion),
    "Scripts inspected: " .. tostring(result.Scanned),
    "",
}
for _, key in ipairs({ "STAFF", "WALK", "SPEED", "RIG", "CMDR", "RARITY" }) do
    local section = sections[key]
    table.insert(report, "[" .. section.Title .. "]")
    if #section.Parts == 0 then
        table.insert(report, "<no matching evidence captured>")
    else
        for _, part in ipairs(section.Parts) do
            table.insert(report, part)
            table.insert(report, "")
        end
    end
end
table.insert(report, "[MISSING EVIDENCE]")
if #result.MissingEvidence == 0 then
    table.insert(report, "<none>")
else
    for _, message in ipairs(result.MissingEvidence) do table.insert(report, "- " .. message) end
end

result.Report = table.concat(report, "\n")
if #result.Report > MAX_REPORT_BYTES then
    result.Report = result.Report:sub(1, MAX_REPORT_BYTES - 96)
        .. "\n\n[MISSING EVIDENCE]\n- report truncated at compact size limit"
end

print(result.Report)
local clipboard = type(setclipboard) == "function" and setclipboard
    or (type(toclipboard) == "function" and toclipboard or nil)
if clipboard then pcall(clipboard, result.Report) end

return result
