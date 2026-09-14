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
        Id = "BASES",
        Path = { "__OBJECTS", "Build", "MainMap", "Bases", "Part" },
    },
    {
        Id = "GROUND",
        Path = { "__OBJECTS", "Areas", "Ground" },
    },
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

local killReferenceLiterals = {
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
    "Bases",
    "Ground",
}

local noteworthyAncestorWords = {
    "kill",
    "death",
    "boundary",
    "lobby",
    "guard",
    "collision",
    "anti",
    "damage",
    "void",
    "restricted",
    "zone",
}

local function clip(value, maximum)
    local text = tostring(value)
    if #text <= maximum then
        return text
    end
    return text:sub(1, math.max(0, maximum - 3)) .. "..."
end

local function quote(value)
    return string.format("%q", clip(value, 600))
end

local function formatNumber(value)
    return string.format("%.6f", value)
end

local function formatVector(value)
    return string.format("(%.6f, %.6f, %.6f)", value.X, value.Y, value.Z)
end

local function formatValue(value)
    local kind = typeof(value)
    if kind == "string" then
        return quote(value)
    elseif kind == "number" then
        return formatNumber(value)
    elseif kind == "Vector3" then
        return formatVector(value)
    elseif kind == "CFrame" then
        return clip(tostring(value), 600)
    elseif kind == "Color3" then
        return string.format("(%.6f, %.6f, %.6f)", value.R, value.G, value.B)
    elseif kind == "Instance" then
        local ok, fullName = pcall(function()
            return value:GetFullName()
        end)
        return ok and clip(fullName, 600) or clip(value, 600)
    end
    return clip(tostring(value), 600)
end

local function sortedKeys(dictionary)
    local keys = {}
    for key in pairs(dictionary) do
        table.insert(keys, tostring(key))
    end
    table.sort(keys)
    return keys
end

local function formatAttributes(instance)
    local ok, attributes = pcall(function()
        return instance:GetAttributes()
    end)
    if not ok then
        return "<error: " .. clip(attributes, 200) .. ">"
    end

    local pieces = {}
    for _, key in ipairs(sortedKeys(attributes)) do
        table.insert(pieces, quote(key) .. ": " .. formatValue(attributes[key]))
    end
    return "{" .. table.concat(pieces, ", ") .. "}"
end

local function formatTags(instance)
    local ok, tags = pcall(function()
        return CollectionService:GetTags(instance)
    end)
    if not ok then
        return "<error: " .. clip(tags, 200) .. ">"
    end

    table.sort(tags)
    local pieces = {}
    for _, tag in ipairs(tags) do
        table.insert(pieces, quote(tag))
    end
    return "{" .. table.concat(pieces, ", ") .. "}"
end

local function fullName(instance)
    local ok, result = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(result, 800) or "<unavailable>"
end

local function pathText(path)
    return "Workspace." .. table.concat(path, ".")
end

local function resolve(path)
    local current = Workspace
    for _, childName in ipairs(path) do
        current = current:FindFirstChild(childName)
        if not current then
            return nil
        end
    end
    return current
end

local function newSection(limit)
    return {
        Lines = {},
        Bytes = 0,
        Limit = limit,
        Truncated = false,
    }
end

local function addLine(section, line)
    if section.Truncated then
        return false
    end

    line = tostring(line)
    local addedBytes = #line + 1
    local marker = "[SECTION TRUNCATED TO PRESERVE 20 KB REPORT LIMIT]"
    if section.Bytes + addedBytes > section.Limit then
        if section.Bytes + #marker + 1 <= section.Limit then
            table.insert(section.Lines, marker)
            section.Bytes = section.Bytes + #marker + 1
        end
        section.Truncated = true
        return false
    end

    table.insert(section.Lines, line)
    section.Bytes = section.Bytes + addedBytes
    return true
end

local geometry = newSection(5200)
local deathTests = newSection(3600)
local ancestors = newSection(5000)
local references = newSection(4400)
local insideResults = {}

for _, target in ipairs(targets) do
    local instance = resolve(target.Path)

    addLine(geometry, "TARGET " .. target.Id)
    addLine(geometry, "Requested path: " .. pathText(target.Path))

    if not instance then
        addLine(geometry, "Status: NOT FOUND")
        addLine(geometry, "")
        insideResults[target.Id] = false
        addLine(deathTests, "TARGET " .. target.Id .. ": NOT FOUND")
        addLine(deathTests, "")
        addLine(ancestors, "TARGET " .. target.Id .. ": NOT FOUND")
        addLine(ancestors, "")
        continue
    end

    addLine(geometry, "GetFullName(): " .. fullName(instance))
    addLine(geometry, "ClassName: " .. instance.ClassName)

    if not instance:IsA("BasePart") then
        addLine(geometry, "Status: FOUND, NOT A BasePart")
        addLine(geometry, "GetAttributes(): " .. formatAttributes(instance))
        addLine(geometry, "CollectionService:GetTags(part): " .. formatTags(instance))
        addLine(geometry, "")
        insideResults[target.Id] = false
        addLine(deathTests, "TARGET " .. target.Id .. ": FOUND, NOT A BasePart")
        addLine(deathTests, "")
    else
        local position = instance.Position
        local size = instance.Size
        local frame = instance.CFrame
        local halfExtents = size / 2
        local approximateMinimum = position - halfExtents
        local approximateMaximum = position + halfExtents
        local objectPoint = frame:PointToObjectSpace(death)
        local margin = halfExtents - Vector3.new(
            math.abs(objectPoint.X),
            math.abs(objectPoint.Y),
            math.abs(objectPoint.Z)
        )
        local isInside = math.abs(objectPoint.X) <= halfExtents.X
            and math.abs(objectPoint.Y) <= halfExtents.Y
            and math.abs(objectPoint.Z) <= halfExtents.Z

        insideResults[target.Id] = isInside

        addLine(geometry, "Position: " .. formatVector(position))
        addLine(geometry, "Size: " .. formatVector(size))
        addLine(geometry, "CFrame: " .. tostring(frame))
        addLine(geometry, "Orientation: " .. formatVector(instance.Orientation))
        addLine(geometry, "CanCollide: " .. tostring(instance.CanCollide))
        addLine(geometry, "CanTouch: " .. tostring(instance.CanTouch))
        addLine(geometry, "CanQuery: " .. tostring(instance.CanQuery))
        addLine(geometry, "Transparency: " .. formatNumber(instance.Transparency))
        addLine(geometry, "CollisionGroup: " .. instance.CollisionGroup)
        addLine(geometry, "Anchored: " .. tostring(instance.Anchored))
        addLine(geometry, "Massless: " .. tostring(instance.Massless))
        if instance:IsA("Part") then
            addLine(geometry, "Shape: " .. tostring(instance.Shape))
        else
            addLine(geometry, "Shape: N/A for " .. instance.ClassName)
        end
        addLine(geometry, "GetAttributes(): " .. formatAttributes(instance))
        addLine(geometry, "CollectionService:GetTags(part): " .. formatTags(instance))
        addLine(geometry, "Approximate AABB min: " .. formatVector(approximateMinimum))
        addLine(geometry, "Approximate AABB max: " .. formatVector(approximateMaximum))
        addLine(geometry, "")

        addLine(deathTests, "TARGET " .. target.Id)
        addLine(deathTests, "Death world point: " .. formatVector(death))
        addLine(deathTests, "Distance to part center: " .. formatNumber((death - position).Magnitude))
        addLine(deathTests, "Death in object-space: " .. formatVector(objectPoint))
        addLine(deathTests, "Half-size: " .. formatVector(halfExtents))
        addLine(deathTests, "Inside OBB: " .. tostring(isInside))
        addLine(deathTests, "Axis margin (half-size minus absolute local point): " .. formatVector(margin))
        addLine(deathTests, "")
    end

    addLine(ancestors, "TARGET " .. target.Id)
    local current = instance
    while current do
        local lowerName = string.lower(current.Name)
        local keywordHits = {}
        for _, word in ipairs(noteworthyAncestorWords) do
            if string.find(lowerName, word, 1, true) then
                table.insert(keywordHits, word)
            end
        end

        addLine(ancestors, "- Name: " .. quote(current.Name))
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
