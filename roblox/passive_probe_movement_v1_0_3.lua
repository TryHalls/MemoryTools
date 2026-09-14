-- MemoryTools Movement Probe V1.0.3.
-- Passive source inspection only: scripts are decompiled but never loaded and
-- networking APIs are only searched as text. Gameplay state is not modified.

local VERSION = "1.0.3"
local MAX_REPORT_BYTES = 12 * 1024
local MAX_SCRIPTS = 5000
local CONTEXT_RADIUS = 20
local YIELD_EVERY = 30

local Players = game:GetService("Players")
local LocalPlayer = Players.LocalPlayer
local PlayerScripts = LocalPlayer and LocalPlayer:FindFirstChildOfClass("PlayerScripts") or nil

local result = {
    Version = VERSION,
    Scanned = 0,
    Report = "",
    Unresolved = {},
}

local sections = {
    REQUEST = { Title = "RIGSYNC CLIENT REQUEST", Budget = 2900, Parts = {}, Bytes = 0, Seen = {} },
    CORRECTION = { Title = "RIGSYNC SERVER CORRECTION", Budget = 2600, Parts = {}, Bytes = 0, Seen = {} },
    RELOCATE = { Title = "RELOCATE CONTRACT", Budget = 1900, Parts = {}, Bytes = 0, Seen = {} },
    CMDR = { Title = "CMDR TELEPORT", Budget = 1800, Parts = {}, Bytes = 0, Seen = {} },
    STAFF = { Title = "STAFF MOVEMENT", Budget = 1100, Parts = {}, Bytes = 0, Seen = {} },
}

local function unresolved(message)
    table.insert(result.Unresolved, tostring(message))
end

local function safePath(instance)
    local ok, path = pcall(function() return instance:GetFullName() end)
    return ok and path or tostring(instance)
end

local pathCache = setmetatable({}, { __mode = "k" })
local function cachedPath(instance)
    if not pathCache[instance] then pathCache[instance] = safePath(instance) end
    return pathCache[instance]
end

local function safeDescendants(root)
    if not root then return {} end
    local ok, descendants = pcall(function() return root:GetDescendants() end)
    return ok and descendants or {}
end

local function isSourceScript(instance)
    return instance and (instance:IsA("LocalScript") or instance:IsA("ModuleScript"))
end

local function splitLines(source)
    local lines = {}
    source = source:gsub("\r\n", "\n")
    if source:sub(-1) ~= "\n" then source = source .. "\n" end
    for line in source:gmatch("(.-)\n") do table.insert(lines, line) end
    return lines
end

local function decompileScript(instance)
    local ok, source = pcall(decompile, instance)
    if not ok or type(source) ~= "string" or source == "" then
        return nil, tostring(source or "empty source")
    end
    return source
end

local function add(section, key, text)
    if section.Seen[key] then return false end
    section.Seen[key] = true
    if section.Bytes + #text + 2 > section.Budget then
        if not section.Truncated then
            section.Truncated = true
            unresolved(section.Title .. " exceeded its section budget")
        end
        return false
    end
    table.insert(section.Parts, text)
    section.Bytes = section.Bytes + #text + 2
    return true
end

local function renderRange(path, lines, first, last, label)
    local output = {
        "-- " .. path .. " :: " .. label .. " :: lines " .. tostring(first) .. "-" .. tostring(last),
    }
    for index = first, last do
        table.insert(output, string.format("%5d | %s", index, lines[index]))
    end
    return table.concat(output, "\n")
end

local function context(section, path, lines, index, label)
    local first = math.max(1, index - CONTEXT_RADIUS)
    local last = math.min(#lines, index + CONTEXT_RADIUS)
    return add(section, path .. ":context:" .. tostring(first) .. ":" .. tostring(last),
        renderRange(path, lines, first, last, label .. " (+/-20)"))
end

local function countPattern(text, pattern)
    local count = 0
    for _ in text:gmatch(pattern) do count = count + 1 end
    return count
end

local function structuralLine(line)
    line = line:gsub("%-%-.*$", "")
    line = line:gsub('"[^"\\]*(\\.[^"\\]*)*"', '""')
    line = line:gsub("'[^'\\]*(\\.[^'\\]*)*'", "''")
    return line
end

local function enclosingFunction(lines, hit)
    for first = hit, 1, -1 do
        if structuralLine(lines[first]):find("%f[%a]function%f[%A]") then
            local depth = 0
            for index = first, #lines do
                local line = structuralLine(lines[index])
                local opens = countPattern(line, "%f[%a]function%f[%A]")
                    + countPattern(line, "%f[%a]then%f[%A]")
                    + countPattern(line, "%f[%a]do%f[%A]")
                    + countPattern(line, "%f[%a]repeat%f[%A]")
                local closes = countPattern(line, "%f[%a]end%f[%A]")
                    + countPattern(line, "%f[%a]until%f[%A]")
                depth = depth + opens - closes
                if depth <= 0 then
                    if index >= hit then return first, index end
                    break
                end
            end
        end
    end
    return nil
end

local function completeFunction(section, path, lines, index, label)
    local first, last = enclosingFunction(lines, index)
    if not first then
        unresolved(path .. " line " .. tostring(index) .. ": complete function boundary unresolved for " .. label)
        return false
    end
    return add(section, path .. ":function:" .. tostring(first) .. ":" .. tostring(last),
        renderRange(path, lines, first, last, label .. " complete enclosing function"))
end

local function lineHasAny(line, markers)
    for _, marker in ipairs(markers) do
        if line:find(marker, 1, true) then return marker end
    end
    return nil
end

local function captureHits(section, path, lines, markers, withFunction)
    local hits = {}
    for index, line in ipairs(lines) do
        local marker = lineHasAny(line, markers)
        if marker then
            table.insert(hits, { Index = index, Marker = marker })
        end
    end
    local windows = {}
    for _, hit in ipairs(hits) do
        local first = math.max(1, hit.Index - CONTEXT_RADIUS)
        local last = math.min(#lines, hit.Index + CONTEXT_RADIUS)
        local previous = windows[#windows]
        if previous and first <= previous.Last + 1 then
            previous.Last = math.max(previous.Last, last)
            table.insert(previous.Markers, hit.Marker .. "@" .. tostring(hit.Index))
        else
            table.insert(windows, { First = first, Last = last, Markers = { hit.Marker .. "@" .. tostring(hit.Index) } })
        end
    end
    for _, window in ipairs(windows) do
        add(section, path .. ":context:" .. tostring(window.First) .. ":" .. tostring(window.Last),
            renderRange(path, lines, window.First, window.Last,
                table.concat(window.Markers, ", ") .. " (merged +/-20 contexts)"))
    end
    if withFunction then
        for _, hit in ipairs(hits) do
            completeFunction(section, path, lines, hit.Index, hit.Marker)
        end
    end
    return #hits
end

local primary = PlayerScripts
    and PlayerScripts:FindFirstChild("Game")
    and PlayerScripts.Game:FindFirstChild("ObbyAntiTPClient")
if not isSourceScript(primary) then unresolved("Players.LocalPlayer.PlayerScripts.Game.ObbyAntiTPClient not found") end

local candidates = {}
local seen = {}
local function candidate(instance)
    if isSourceScript(instance) and not seen[instance] and #candidates < MAX_SCRIPTS then
        seen[instance] = true
        table.insert(candidates, instance)
    end
end
candidate(primary)
for _, instance in ipairs(safeDescendants(game)) do candidate(instance) end
if #candidates >= MAX_SCRIPTS then unresolved("script inspection limit reached") end
table.sort(candidates, function(left, right)
    if left == primary then return true end
    if right == primary then return false end
    local leftPath = cachedPath(left)
    local rightPath = cachedPath(right)
    local leftCmdr = leftPath:lower():find("cmdr", 1, true) ~= nil
    local rightCmdr = rightPath:lower():find("cmdr", 1, true) ~= nil
    if leftCmdr ~= rightCmdr then return leftCmdr end
    return leftPath < rightPath
end)

local REQUEST_MARKERS = {
    "RigSync.Reconcile", "RigSync.ProbeSatchel", "RigSync.SeedSatchel",
    "InvokeServer", "FireServer", "JSONEncode", "JSONDecode",
}
local CORRECTION_MARKERS = {
    "RigSync.CorrectionBegan", "CorrectionBegan", "RigSync.Primed", "PivotTo",
    "Displacement", "displacement", "Magnitude", "Distance", "distance",
}
local RELOCATE_MARKERS = { "RigSync.Refresh", '"Relocate"', "'Relocate'" }
local GLOBAL_RIG_MARKERS = { ".RigSync.Reconcile", "RigSync.Reconcile", "CorrectionBegan", "RigSync.Primed" }
local STAFF_MARKERS = { "ProbeStaffStatus", "StaffVerdict", "WriteWalkSpeed", "WriteSpeedPower", "SpeedPowerVerdict" }
local CMD_FIELDS = { "Name", "Aliases", "Args", "Run", "Description" }

local rigConsumers = 0
local cmdrCommands = 0
local primaryRead = false

if type(decompile) ~= "function" then
    unresolved("decompile is unavailable")
else
    for index, instance in ipairs(candidates) do
        result.Scanned = result.Scanned + 1
        local path = cachedPath(instance)
        local lowerPath = path:lower()
        local source, sourceError = decompileScript(instance)
        if source then
            local lines = splitLines(source)
            local lowerSource = source:lower()
            if instance == primary then
                primaryRead = true
                captureHits(sections.REQUEST, path, lines, REQUEST_MARKERS, true)
                captureHits(sections.CORRECTION, path, lines, CORRECTION_MARKERS, true)
                captureHits(sections.RELOCATE, path, lines, RELOCATE_MARKERS, true)
            else
                local hasRig = lineHasAny(source, GLOBAL_RIG_MARKERS)
                if hasRig then
                    rigConsumers = rigConsumers + 1
                    captureHits(sections.REQUEST, path, lines, GLOBAL_RIG_MARKERS, true)
                    captureHits(sections.REQUEST, path, lines, { "require", "InvokeServer", "FireServer" }, false)
                end
            end

            if lineHasAny(source, STAFF_MARKERS) then
                captureHits(sections.STAFF, path, lines, STAFF_MARKERS, true)
            end

            local movementWord = lowerSource:find("teleport", 1, true)
                or lowerSource:find("relocate", 1, true)
                or lowerSource:find("cframe", 1, true)
                or lowerSource:find("position", 1, true)
                or lowerSource:find("goto", 1, true)
                or lowerSource:find("bring", 1, true)
                or lowerSource:find("%f[%a]tp%f[%A]")
            local commandShape = lineHasAny(source, CMD_FIELDS)
                and (source:find("Run", 1, true) or source:find("run", 1, true))
            if movementWord and (lowerPath:find("cmdr", 1, true) or commandShape) then
                cmdrCommands = cmdrCommands + 1
                local whole = renderRange(path, lines, 1, #lines,
                    "command module; inspect Name/Aliases/Args/Run/Description and execution side")
                if #whole <= sections.CMDR.Budget - sections.CMDR.Bytes then
                    add(sections.CMDR, path .. ":whole", whole)
                else
                    captureHits(sections.CMDR, path, lines,
                        { "Name", "Aliases", "Args", "Run", "Description", "Remote", "Teleport", "PivotTo", "CFrame" }, false)
                end
            end
        elseif instance == primary then
            unresolved(path .. " decompile failed: " .. tostring(sourceError))
        end
        if index % YIELD_EVERY == 0 then task.wait() end
    end
end

if primaryRead and rigConsumers == 0 then unresolved("no additional RigSync consumers found") end
if cmdrCommands == 0 then unresolved("no Cmdr movement command modules found") end

local report = {
    "MEMORYTOOLS MOVEMENT PROBE V1.0.3",
    "PlaceId: " .. tostring(game.PlaceId),
    "PlaceVersion: " .. tostring(game.PlaceVersion),
    "Scripts inspected: " .. tostring(result.Scanned),
    "",
}
for _, key in ipairs({ "REQUEST", "CORRECTION", "RELOCATE", "CMDR", "STAFF" }) do
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
table.insert(report, "[UNRESOLVED]")
if #result.Unresolved == 0 then
    table.insert(report, "<none>")
else
    for _, message in ipairs(result.Unresolved) do table.insert(report, "- " .. message) end
end

result.Report = table.concat(report, "\n")
if #result.Report > MAX_REPORT_BYTES then
    result.Report = result.Report:sub(1, MAX_REPORT_BYTES - 80)
        .. "\n\n[UNRESOLVED]\n- report truncated at 12 KB"
end

print(result.Report)
local clipboard = type(setclipboard) == "function" and setclipboard
    or (type(toclipboard) == "function" and toclipboard or nil)
if clipboard then pcall(clipboard, result.Report) end

return result
