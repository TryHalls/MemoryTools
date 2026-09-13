-- MemoryTools V1.0.1 passive targeted probe.
-- Source inspection only: no module loading, networking calls, connection
-- enumeration, garbage-collector inspection, or filesystem writes.

local VERSION = "1.0.1"
local CONTEXT_RADIUS = 8
local MAX_SCRIPTS = 5000
local MAX_MATCHES = 100
local MAX_CONTEXTS_PER_SCRIPT = 48
local YIELD_EVERY = 20

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local LocalPlayer = Players.LocalPlayer

local result = {
    Version = VERSION,
    Matches = {},
    Failures = {},
    Scanned = 0,
}

if type(decompile) ~= "function" then
    result.Failures[1] = "decompile is unavailable in this executor"
    result.Report = "MemoryTools Passive Probe V" .. VERSION .. "\n"
        .. "FAILED: decompile is unavailable in this executor"
    warn(result.Report)
    return result
end

local function pathOf(instance)
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

local function isSourceContainer(instance)
    return instance:IsA("ModuleScript") or instance:IsA("LocalScript")
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
    for line in source:gmatch("(.-)\n") do
        table.insert(lines, line)
    end
    return lines
end

local function containsAny(lower, terms)
    for _, term in ipairs(terms) do
        if lower:find(term, 1, true) then return true end
    end
    return false
end

local MARKERS = {
    "require",
    "rigsync",
    "writewalkspeed",
    "writespeedpower",
    "setspeedpower",
    "walkspeed",
    "speedpower",
    "teleport",
    "goto",
    "callback",
    "verdict",
    "command",
    "payload",
    "argument",
    "args",
}

local function matchingTerms(line)
    local lower = line:lower()
    local hits = {}
    for _, marker in ipairs(MARKERS) do
        if lower:find(marker, 1, true) then table.insert(hits, marker) end
    end
    return hits
end

local function numbered(lines, first, last)
    local out = {}
    for index = first, last do
        table.insert(out, string.format("%5d | %s", index, lines[index]))
    end
    return table.concat(out, "\n")
end

local function extractContexts(lines)
    local contexts = {}
    local coveredThrough = 0
    for index, line in ipairs(lines) do
        local terms = matchingTerms(line)
        if #terms > 0 and index > coveredThrough then
            local first = math.max(1, index - CONTEXT_RADIUS)
            local last = math.min(#lines, index + CONTEXT_RADIUS)
            coveredThrough = last
            table.insert(contexts, {
                Line = index,
                Terms = terms,
                Text = numbered(lines, first, last),
            })
            if #contexts >= MAX_CONTEXTS_PER_SCRIPT then break end
        end
    end
    return contexts
end

local function classifyLines(lines)
    local classified = {
        Requires = {},
        Calls = {},
        Payloads = {},
        Callbacks = {},
        Commands = {},
    }
    for index, line in ipairs(lines) do
        local lower = line:lower()
        local entry = string.format("%5d | %s", index, line)
        if lower:find("require", 1, true) then table.insert(classified.Requires, entry) end
        if line:match("[%w_%.%]%)]%s*[:%.][%w_]+%s*%(") then table.insert(classified.Calls, entry) end
        if containsAny(lower, { "payload", "argument", "args" }) or line:find("{", 1, true) then
            table.insert(classified.Payloads, entry)
        end
        if containsAny(lower, { "callback", "verdict" }) then table.insert(classified.Callbacks, entry) end
        if containsAny(lower, { "command", "setspeedpower", "walkspeed", "teleport", "goto" }) then
            table.insert(classified.Commands, entry)
        end
    end
    return classified
end

local exactTargets = {}
local function addExact(instance, label)
    if not instance then
        table.insert(result.Failures, label .. " :: not found")
        return
    end
    if isSourceContainer(instance) then
        exactTargets[instance] = label
        return
    end
    for _, descendant in ipairs(instance:GetDescendants()) do
        if isSourceContainer(descendant) then
            exactTargets[descendant] = label .. " descendant"
        end
    end
end

addExact(safeFind(ReplicatedStorage, { "Client", "AdminPanelEntry" }), "Client.AdminPanelEntry")
addExact(safeFind(ReplicatedStorage, { "Client", "StaffEntryHotkey" }), "Client.StaffEntryHotkey")
addExact(safeFind(ReplicatedStorage, { "Shared", "Staff" }), "Shared.Staff")

local roots = {}
local function addRoot(instance)
    if instance then table.insert(roots, instance) end
end
addRoot(safeFind(ReplicatedStorage, { "Client" }))
addRoot(safeFind(ReplicatedStorage, { "Shared" }))
addRoot(LocalPlayer and LocalPlayer:FindFirstChildOfClass("PlayerScripts") or nil)

local candidates = {}
local seen = {}
local function addCandidate(instance)
    if instance and isSourceContainer(instance) and not seen[instance] then
        seen[instance] = true
        table.insert(candidates, instance)
    end
end

for instance in pairs(exactTargets) do addCandidate(instance) end
for _, descendant in ipairs(ReplicatedStorage:GetDescendants()) do
    if isSourceContainer(descendant) and pathOf(descendant):lower():find("cmdr", 1, true) then
        addCandidate(descendant)
    end
end
for _, root in ipairs(roots) do
    if root then
        addCandidate(root)
        for _, descendant in ipairs(root:GetDescendants()) do
            addCandidate(descendant)
            if #candidates >= MAX_SCRIPTS then break end
        end
    end
    if #candidates >= MAX_SCRIPTS then break end
end
if #candidates >= MAX_SCRIPTS then
    table.insert(result.Failures, "Script inspection limit reached: " .. tostring(MAX_SCRIPTS))
end

local CONSUMER_TERMS = { "rigsync", "writewalkspeed", "writespeedpower" }
local CMDR_TERMS = { "setspeedpower", "walkspeed", "teleport", "goto" }

for index, instance in ipairs(candidates) do
    result.Scanned = result.Scanned + 1
    local instancePath = pathOf(instance)
    local source, sourceError = sourceFor(instance)
    if source then
        local lowerSource = source:lower()
        local lowerPath = instancePath:lower()
        local reasons = {}
        if exactTargets[instance] then table.insert(reasons, exactTargets[instance]) end
        for _, term in ipairs(CONSUMER_TERMS) do
            if lowerSource:find(term, 1, true) then table.insert(reasons, "consumer:" .. term) end
        end
        if lowerPath:find("cmdr", 1, true) and containsAny(lowerSource, CMDR_TERMS) then
            table.insert(reasons, "Cmdr-related command")
        end

        if #reasons > 0 then
            local lines = splitLines(source)
            local classified = classifyLines(lines)
            table.insert(result.Matches, {
                Path = instancePath,
                ClassName = instance.ClassName,
                Reasons = reasons,
                SourceCharacters = #source,
                SourceLines = #lines,
                Requires = classified.Requires,
                Calls = classified.Calls,
                Payloads = classified.Payloads,
                Callbacks = classified.Callbacks,
                Commands = classified.Commands,
                Contexts = extractContexts(lines),
            })
            if #result.Matches >= MAX_MATCHES then
                table.insert(result.Failures, "Match limit reached: " .. tostring(MAX_MATCHES))
                break
            end
        end
    elseif exactTargets[instance] or instancePath:lower():find("cmdr", 1, true) then
        table.insert(result.Failures, instancePath .. " :: " .. tostring(sourceError))
    end
    if index % YIELD_EVERY == 0 then task.wait() end
end

table.sort(result.Matches, function(a, b) return a.Path < b.Path end)

local function appendSection(lines, title, values)
    table.insert(lines, title .. " (" .. tostring(#values) .. ")")
    if #values == 0 then
        table.insert(lines, "  <none>")
    else
        for _, value in ipairs(values) do table.insert(lines, value) end
    end
end

local report = {
    "MemoryTools Passive Probe V" .. VERSION,
    "PlaceId: " .. tostring(game.PlaceId),
    "PlaceVersion: " .. tostring(game.PlaceVersion),
    "Scripts inspected: " .. tostring(result.Scanned),
    "Target matches: " .. tostring(#result.Matches),
    "",
}

for _, match in ipairs(result.Matches) do
    table.insert(report, string.rep("=", 78))
    table.insert(report, match.Path .. " [" .. match.ClassName .. "]")
    table.insert(report, "Reasons: " .. table.concat(match.Reasons, ", "))
    table.insert(report, "Source: " .. tostring(match.SourceLines) .. " lines / "
        .. tostring(match.SourceCharacters) .. " chars")
    appendSection(report, "REQUIRES", match.Requires)
    appendSection(report, "CALLS", match.Calls)
    appendSection(report, "PAYLOADS / ARG TABLES", match.Payloads)
    appendSection(report, "CALLBACKS / VERDICTS", match.Callbacks)
    appendSection(report, "COMMAND NAMES", match.Commands)
    table.insert(report, "CONTEXTS (+/-" .. tostring(CONTEXT_RADIUS) .. ")")
    for _, context in ipairs(match.Contexts) do
        table.insert(report, "-- line " .. tostring(context.Line) .. " ["
            .. table.concat(context.Terms, ", ") .. "]")
        table.insert(report, context.Text)
    end
    table.insert(report, "")
end

appendSection(report, "FAILURES / NOT FOUND", result.Failures)
result.Report = table.concat(report, "\n")

local chunkSize = 12000
for offset = 1, #result.Report, chunkSize do
    print(result.Report:sub(offset, offset + chunkSize - 1))
end

local clipboard = type(setclipboard) == "function" and setclipboard
    or (type(toclipboard) == "function" and toclipboard or nil)
if clipboard then
    pcall(clipboard, result.Report)
end

return result
