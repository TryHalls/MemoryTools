-- MemoryTools Flight Boundary Probe V1.0.4.3.
-- Passive inspection of geometry and client-visible script references.

local VERSION = "1.0.4.3"
local MAX_REPORT_BYTES = 20 * 1024
local MAX_SCRIPTS_SCANNED = 5000
local MAX_RELEVANT_SCRIPTS = 12
local SCRIPT_CONTEXT_RADIUS = 12

local Workspace = game:GetService("Workspace")
local CollectionService = game:GetService("CollectionService")

local death = Vector3.new(554.53, 67.04, -448.52)

local targets = {
    {
        Id = "LOBBY_BOUNDARY",
        Path = { "__OBJECTS", "Build", "MainMap", "LobbyBoundaries", "Part" },
    },
    {
        Id = "COLL_GUARD",
        Path = { "__OBJECTS", "Build", "1", "COLLISIONS", "COLL GUARD", "WALL LEFT (COLL GUARD)", "Part" },
    },
    {
        Id = "COLL2",
        Path = { "__OBJECTS", "Build", "1", "COLLISIONS", "COLL2", "WALL LEFT", "Part" },
    },
    {
        Id = "BASES_PART",
        Path = { "__OBJECTS", "Build", "MainMap", "Bases", "Part" },
    },
    {
        Id = "AREAS_GROUND",
        Path = { "__OBJECTS", "Areas", "Ground" },
    },
}

local nameKeywords = {
    "Kill",
    "Death",
    "Boundary",
    "Lobby",
    "Guard",
    "Collision",
    "Anti",
    "Damage",
    "Void",
    "Restricted",
    "Zone",
}

local searchLiterals = {
    "LobbyBoundaries",
    "COLL GUARD",
    "WALL LEFT",
    "Bases",
    "Ground",
    "Humanoid.Health",
    "Health = 0",
    "TakeDamage",
    "Touched",
    "touch",
}

local targetReferenceLiterals = {
    "LobbyBoundaries",
    "COLL GUARD",
    "WALL LEFT",
}

local killReferenceLiterals = {
    "Humanoid.Health",
    "Health = 0",
    "TakeDamage",
    "Touched",
    "touch",
}

local function clip(value, maximum)
    value = tostring(value)
    if #value <= maximum then
        return value
    end
    return value:sub(1, math.max(0, maximum - 3)) .. "..."
end

local function quote(value)
    return string.format("%q", tostring(value))
end

local function fmtNumber(value)
    if typeof(value) ~= "number" then
        return "?"
    end
    return string.format("%.3f", value)
end

local function fmtVector(value)
    if typeof(value) ~= "Vector3" then
        return "(?, ?, ?)"
    end
    return string.format("(%.3f, %.3f, %.3f)", value.X, value.Y, value.Z)
end

local function fmtCFrame(value)
    if typeof(value) ~= "CFrame" then
        return "?"
    end
    local components = { value:GetComponents() }
    for index, component in ipairs(components) do
        components[index] = string.format("%.5f", component)
    end
    return "CFrame.new(" .. table.concat(components, ", ") .. ")"
end

local function fullName(instance)
    local ok, result = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(result, 260) or clip(instance, 260)
end

local function resolvePath(path)
    local current = Workspace
    for _, name in ipairs(path) do
        current = current:FindFirstChild(name)
        if not current then
            return nil
        end
    end
    return current
end

local function valueString(value)
    local valueType = typeof(value)
    if valueType == "string" then
        return quote(value)
    elseif valueType == "Vector3" then
        return fmtVector(value)
    elseif valueType == "CFrame" then
        return fmtCFrame(value)
    elseif valueType == "Color3" then
        return string.format("Color3(%.3f, %.3f, %.3f)", value.R, value.G, value.B)
    end
    return clip(tostring(value), 240)
end

local function formatAttributes(instance)
    local ok, attributes = pcall(function()
        return instance:GetAttributes()
    end)
    if not ok or type(attributes) ~= "table" then
        return "<unavailable>"
    end
    local names = {}
    for name in pairs(attributes) do
        table.insert(names, name)
    end
    table.sort(names)
    if #names == 0 then
        return "{}"
    end
    local parts = {}
    for _, name in ipairs(names) do
        table.insert(parts, name .. "=" .. valueString(attributes[name]))
    end
    return "{" .. table.concat(parts, ", ") .. "}"
end

local function formatTags(instance)
    local ok, tags = pcall(function()
        return CollectionService:GetTags(instance)
    end)
    if not ok then
        return "<unavailable>"
    end
    table.sort(tags)
    return #tags > 0 and table.concat(tags, ", ") or "none"
end

local sections = {}
local function newSection()
    return {
        Lines = {},
        Bytes = 0,
    }
end

local geometry = newSection()
local deathTests = newSection()
local ancestors = newSection()
local references = newSection()

local function addLine(section, line)
    line = tostring(line)
    if section.Bytes + #line + 1 > 8 * 1024 then
        return false
    end
    table.insert(section.Lines, line)
    section.Bytes = section.Bytes + #line + 1
    return true
end

local resolvedTargets = {}
local insideResults = {}

for _, target in ipairs(targets) do
    local instance = resolvePath(target.Path)
    resolvedTargets[target.Id] = instance

    addLine(geometry, target.Id .. ":")
    if not instance then
        addLine(geometry, "  Status: NOT FOUND")
        addLine(geometry, "")
    elseif not instance:IsA("BasePart") then
        addLine(geometry, "  Status: FOUND BUT NOT BASEPART")
        addLine(geometry, "  FullName: " .. fullName(instance))
        addLine(geometry, "  ClassName: " .. instance.ClassName)
        addLine(geometry, "")
    else
        addLine(geometry, "  Status: FOUND")
        addLine(geometry, "  FullName: " .. fullName(instance))
        addLine(geometry, "  ClassName: " .. instance.ClassName)
        addLine(geometry, "  Position: " .. fmtVector(instance.Position))
        addLine(geometry, "  Size: " .. fmtVector(instance.Size))
        addLine(geometry, "  CFrame: " .. fmtCFrame(instance.CFrame))
        addLine(geometry, "  Orientation: " .. fmtVector(instance.Orientation))
        addLine(geometry, "  CanCollide: " .. tostring(instance.CanCollide))
        addLine(geometry, "  CanTouch: " .. tostring(instance.CanTouch))
        addLine(geometry, "  CanQuery: " .. tostring(instance.CanQuery))
        addLine(geometry, "  Transparency: " .. fmtNumber(instance.Transparency))
        addLine(geometry, "  CollisionGroup: " .. tostring(instance.CollisionGroup))
        addLine(geometry, "  Anchored: " .. tostring(instance.Anchored))
        addLine(geometry, "  Massless: " .. tostring(instance.Massless))
        local shapeOk, shape = pcall(function()
            return instance.Shape
        end)
        addLine(geometry, "  Shape: " .. (shapeOk and tostring(shape) or "N/A"))
        addLine(geometry, "  Attributes: " .. formatAttributes(instance))
        addLine(geometry, "  Tags: " .. formatTags(instance))
        local half = instance.Size / 2
        addLine(geometry, "  ApproxAABB.Min: " .. fmtVector(instance.Position - half))
        addLine(geometry, "  ApproxAABB.Max: " .. fmtVector(instance.Position + half))
        addLine(geometry, "")
    end
end

for _, target in ipairs(targets) do
    local instance = resolvedTargets[target.Id]
    addLine(deathTests, target.Id .. ":")
    if not instance or not instance:IsA("BasePart") then
        addLine(deathTests, "  Test: unavailable")
        insideResults[target.Id] = false
    else
        local localPos = instance.CFrame:PointToObjectSpace(death)
        local halfSize = instance.Size / 2
        local absolute = Vector3.new(math.abs(localPos.X), math.abs(localPos.Y), math.abs(localPos.Z))
        local margin = halfSize - absolute
        local inside = absolute.X <= halfSize.X
            and absolute.Y <= halfSize.Y
            and absolute.Z <= halfSize.Z
        insideResults[target.Id] = inside
        addLine(deathTests, "  Distance death->Position: " .. fmtNumber((death - instance.Position).Magnitude))
        addLine(deathTests, "  Death local/object-space: " .. fmtVector(localPos))
        addLine(deathTests, "  HalfSize: " .. fmtVector(halfSize))
        addLine(deathTests, "  Axis margin (HalfSize-AbsLocal): " .. fmtVector(margin))
        addLine(deathTests, "  insideOBB: " .. tostring(inside))
    end
    addLine(deathTests, "")
end

local lowerKeywords = {}
for _, keyword in ipairs(nameKeywords) do
    table.insert(lowerKeywords, string.lower(keyword))
end

for _, target in ipairs(targets) do
    local current = resolvedTargets[target.Id]
    addLine(ancestors, target.Id .. ":")
    if not current then
        addLine(ancestors, "  NOT FOUND")
        addLine(ancestors, "")
    end
    while current do
        local lowerName = string.lower(current.Name)
        local keywordHits = {}
        for index, lowerKeyword in ipairs(lowerKeywords) do
            if string.find(lowerName, lowerKeyword, 1, true) then
                table.insert(keywordHits, nameKeywords[index])
            end
        end
        addLine(ancestors, "- " .. fullName(current))
        addLine(ancestors, "  ClassName: " .. current.ClassName)
        addLine(ancestors, "  Attributes: " .. formatAttributes(current))
        addLine(ancestors, "  Tags: " .. formatTags(current))
        addLine(ancestors, "  Name keyword matches only: " .. (#keywordHits > 0 and table.concat(keywordHits, ", ") or "none"))

        if current == Workspace then
            break
        end
        current = current.Parent
    end
    addLine(ancestors, "")
end

local function globalFunction(name)
    local ok, value = pcall(function()
        local environment = getfenv()
        return environment[name]
    end)
    if ok and type(value) == "function" then
        return value
    end
    return nil
end

local decompiler = globalFunction("decompile")
local relevantExact = {}
local relevantOther = {}
local scriptsScanned = 0
local decompileFailures = 0
local clientKillReferenceFound = false

local function firstLiteralMatch(source, literals)
    for index, literal in ipairs(literals) do
        local matchOffset = string.find(source, literal, 1, true)
        if matchOffset then
            return literal, matchOffset, index
        end
    end
    return nil, nil, nil
end

local function lineNumberAt(source, bytePosition)
    local _, newlineCount = source:sub(1, bytePosition):gsub("\n", "")
    return newlineCount + 1
end

local function splitLines(source)
    local lines = {}
    source = source:gsub("\r\n", "\n")
    if source:sub(-1) ~= "\n" then
        source = source .. "\n"
    end
    for line in source:gmatch("(.-)\n") do
        table.insert(lines, line)
    end
    return lines
end

local function rememberRelevant(scriptInstance, source, literal, bytePosition, exactPriority)
    local destination = exactPriority and relevantExact or relevantOther
    if #destination >= MAX_RELEVANT_SCRIPTS then
        return
    end
    table.insert(destination, {
        Script = scriptInstance,
        Source = source,
        Literal = literal,
        MatchLine = lineNumberAt(source, bytePosition),
    })
end

if not decompiler then
    addLine(references, "DECOMPILE UNAVAILABLE")
else
    for _, instance in ipairs(game:GetDescendants()) do
        if scriptsScanned >= MAX_SCRIPTS_SCANNED then
            break
        end
        if instance:IsA("LocalScript") or instance:IsA("ModuleScript") then
            scriptsScanned = scriptsScanned + 1
            local ok, source = pcall(decompiler, instance)
            if ok and type(source) == "string" then
                local literal, bytePosition, literalIndex = firstLiteralMatch(source, searchLiterals)
                if literal then
                    rememberRelevant(instance, source, literal, bytePosition, literalIndex <= 2)
                end
                local targetLiteral = firstLiteralMatch(source, targetReferenceLiterals)
                local handlerLiteral = firstLiteralMatch(source, killReferenceLiterals)
                if targetLiteral and handlerLiteral then
                    clientKillReferenceFound = true
                end
            else
                decompileFailures = decompileFailures + 1
            end
        end
    end

    addLine(references, "Decompiler: AVAILABLE")
    addLine(references, "Scripts scanned: " .. tostring(scriptsScanned))
    addLine(references, "Decompile failures: " .. tostring(decompileFailures))

    local selected = {}
    for _, result in ipairs(relevantExact) do
        if #selected >= MAX_RELEVANT_SCRIPTS then
            break
        end
        table.insert(selected, result)
    end
    for _, result in ipairs(relevantOther) do
        if #selected >= MAX_RELEVANT_SCRIPTS then
            break
        end
        table.insert(selected, result)
    end

    addLine(references, "Relevant scripts reported: " .. tostring(#selected))
    addLine(references, "")

    for _, result in ipairs(selected) do
        local lines = splitLines(result.Source)
        local firstLine = math.max(1, result.MatchLine - SCRIPT_CONTEXT_RADIUS)
        local lastLine = math.min(#lines, result.MatchLine + SCRIPT_CONTEXT_RADIUS)
        addLine(references, "SCRIPT: " .. fullName(result.Script))
        addLine(references, "Matched literal: " .. quote(result.Literal))
        addLine(references, string.format("Context lines: %d-%d (match at %d)", firstLine, lastLine, result.MatchLine))
        for lineIndex = firstLine, lastLine do
            if not addLine(references, string.format("%5d | %s", lineIndex, clip(lines[lineIndex], 500))) then
                break
            end
        end
        addLine(references, "")
    end
end

local summaryLines = {
    "No definitive cause is asserted by this probe.",
    "Ancestor keyword matches are names only and carry no semantic inference.",
    "DEATH_POSITION_INSIDE_LOBBY_BOUNDARY=" .. tostring(insideResults.LOBBY_BOUNDARY == true),
    "DEATH_POSITION_INSIDE_COLL_GUARD=" .. tostring(insideResults.COLL_GUARD == true),
    "DEATH_POSITION_INSIDE_COLL2=" .. tostring(insideResults.COLL2 == true),
    "CLIENT_KILL_HANDLER_REFERENCE_FOUND=" .. tostring(clientKillReferenceFound),
    "CLIENT_KILL_HANDLER_REFERENCE_FOUND requires a target-name literal and a requested health/damage/touch literal in the same decompiled client script; it does not establish causation.",
    "SCRIPTS_SCANNED=" .. tostring(scriptsScanned),
    "PROBE_VERSION=" .. VERSION,
}

local reportParts = {
    "MEMORYTOOLS FLIGHT BOUNDARY PROBE V1.0.4.3",
    "",
    "[PART GEOMETRY]",
    table.concat(geometry.Lines, "\n"),
    "",
    "[DEATH POSITION TEST]",
    table.concat(deathTests.Lines, "\n"),
    "",
    "[ANCESTORS / ATTRIBUTES / TAGS]",
    table.concat(ancestors.Lines, "\n"),
    "",
    "[CLIENT SCRIPT REFERENCES]",
    table.concat(references.Lines, "\n"),
    "",
    "[SUMMARY]",
    table.concat(summaryLines, "\n"),
}

local report = table.concat(reportParts, "\n")
if #report > MAX_REPORT_BYTES then
    local summary = "[SUMMARY]\n" .. table.concat(summaryLines, "\n")
    local prefixLimit = MAX_REPORT_BYTES - #summary - 2
    report = report:sub(1, math.max(0, prefixLimit)) .. "\n\n" .. summary
end

print(report)

local clipboardWriter = globalFunction("setclipboard") or globalFunction("toclipboard")
if clipboardWriter then
    local ok, clipboardError = pcall(clipboardWriter, report)
    if not ok then
        warn("[MemoryTools Flight Boundary Probe] Clipboard copy failed: " .. clip(clipboardError, 300))
    end
else
    warn("[MemoryTools Flight Boundary Probe] Clipboard API unavailable; report was printed only")
end
