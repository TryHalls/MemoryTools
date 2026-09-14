-- MemoryTools Lobby Topology Probe V1.0.4.4.
-- Passive, client-visible geometry and source inspection only.

local VERSION = "1.0.4.4"
local MAX_REPORT_BYTES = 24 * 1024
local MAX_PARTS_REPORTED = 100
local MAX_SCRIPTS_SCANNED = 5000
local DEATH = Vector3.new(554.53, 67.04, -448.52)
local QUERY_SIZE = Vector3.new(80, 80, 160)
local EXPANSION = Vector3.new(3, 3, 3)

local Workspace = game:GetService("Workspace")
local CollectionService = game:GetService("CollectionService")

local function clip(value, maximum)
    local text = tostring(value)
    if #text <= maximum then
        return text
    end
    return text:sub(1, math.max(0, maximum - 3)) .. "..."
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
    local c = { value:GetComponents() }
    for i, v in ipairs(c) do
        c[i] = string.format("%.5f", v)
    end
    return table.concat(c, ", ")
end

local function quote(value)
    return string.format("%q", tostring(value))
end

local function safeFullName(instance)
    local ok, result = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(result, 320) or clip(instance, 320)
end

local function formatAttributes(instance)
    local ok, attributes = pcall(function()
        return instance:GetAttributes()
    end)
    if not ok or type(attributes) ~= "table" then
        return "<unavailable>"
    end
    local keys = {}
    for key in pairs(attributes) do
        table.insert(keys, key)
    end
    table.sort(keys)
    if #keys == 0 then
        return "{}"
    end
    local values = {}
    for _, key in ipairs(keys) do
        table.insert(values, quote(key) .. ": " .. clip(attributes[key], 180))
    end
    return "{" .. table.concat(values, ", ") .. "}"
end

local function formatTags(instance)
    local ok, tags = pcall(function()
        return CollectionService:GetTags(instance)
    end)
    if not ok then
        return "<unavailable>"
    end
    table.sort(tags)
    if #tags == 0 then
        return "{}"
    end
    for index, tag in ipairs(tags) do
        tags[index] = quote(tag)
    end
    return "{" .. clip(table.concat(tags, ","), 300) .. "}"
end

local function resolve(path)
    local current = Workspace
    for _, name in ipairs(path) do
        current = current:FindFirstChild(name)
        if not current then
            return nil
        end
    end
    return current
end

local function section(limit)
    return { lines = {}, bytes = 0, limit = limit, truncated = false }
end

local function add(target, line)
    if target.truncated then
        return false
    end
    line = tostring(line)
    if target.bytes + #line + 1 > target.limit then
        local marker = "[SECTION TRUNCATED BY REPORT BUDGET]"
        if target.bytes + #marker + 1 <= target.limit then
            table.insert(target.lines, marker)
            target.bytes = target.bytes + #marker + 1
        end
        target.truncated = true
        return false
    end
    table.insert(target.lines, line)
    target.bytes = target.bytes + #line + 1
    return true
end

local function obbData(part, worldPoint, expansion)
    expansion = expansion or Vector3.zero
    local localPoint = part.CFrame:PointToObjectSpace(worldPoint)
    local half = part.Size / 2 + expansion
    local absolute = Vector3.new(math.abs(localPoint.X), math.abs(localPoint.Y), math.abs(localPoint.Z))
    local margin = half - absolute
    local inside = absolute.X <= half.X and absolute.Y <= half.Y and absolute.Z <= half.Z
    local outside = Vector3.new(
        math.max(absolute.X - half.X, 0),
        math.max(absolute.Y - half.Y, 0),
        math.max(absolute.Z - half.Z, 0)
    )
    return {
        localPoint = localPoint,
        half = half,
        margin = margin,
        inside = inside,
        distance = outside.Magnitude,
    }
end

local function cornersWorldBounds(part)
    local half = part.Size / 2
    local minimum = Vector3.new(math.huge, math.huge, math.huge)
    local maximum = Vector3.new(-math.huge, -math.huge, -math.huge)
    for _, sx in ipairs({ -1, 1 }) do
        for _, sy in ipairs({ -1, 1 }) do
            for _, sz in ipairs({ -1, 1 }) do
                local point = part.CFrame:PointToWorldSpace(Vector3.new(half.X * sx, half.Y * sy, half.Z * sz))
                minimum = Vector3.new(
                    math.min(minimum.X, point.X),
                    math.min(minimum.Y, point.Y),
                    math.min(minimum.Z, point.Z)
                )
                maximum = Vector3.new(
                    math.max(maximum.X, point.X),
                    math.max(maximum.Y, point.Y),
                    math.max(maximum.Z, point.Z)
                )
            end
        end
    end
    return minimum, maximum
end

local function inspectPart(part)
    local base = obbData(part, DEATH)
    local expanded = obbData(part, DEATH, EXPANSION)
    local minimum, maximum = cornersWorldBounds(part)
    return {
        part = part,
        path = safeFullName(part),
        distance = base.distance,
        localPoint = base.localPoint,
        half = base.half,
        margin = base.margin,
        inside = base.inside,
        expandedInside = expanded.inside,
        boundsMin = minimum,
        boundsMax = maximum,
    }
end

local function emitPart(target, item, includeDetail)
    local part = item.part
    add(target, "GetFullName(): " .. item.path)
    add(target, "Name: " .. quote(part.Name))
    add(target, "ClassName: " .. part.ClassName)
    add(target, "Position: " .. fmtVector(part.Position))
    add(target, "Size: " .. fmtVector(part.Size))
    add(target, "CFrame: " .. fmtCFrame(part.CFrame))
    add(target, "Orientation: " .. fmtVector(part.Orientation))
    add(target, "CanCollide: " .. tostring(part.CanCollide))
    add(target, "CanTouch: " .. tostring(part.CanTouch))
    add(target, "CanQuery: " .. tostring(part.CanQuery))
    add(target, "Transparency: " .. fmtNumber(part.Transparency))
    add(target, "Tags: " .. formatTags(part))
    add(target, "Attributes: " .. formatAttributes(part))
    add(target, "Death local/object-space: " .. fmtVector(item.localPoint))
    add(target, "Half-size: " .. fmtVector(item.half))
    add(target, "Axis margin: " .. fmtVector(item.margin))
    add(target, "Inside OBB: " .. tostring(item.inside))
    add(target, "Inside OBB expanded 3: " .. tostring(item.expandedInside))
    add(target, "Distance to OBB approx: " .. fmtNumber(item.distance))
    if includeDetail then
        add(target, "World corners min: " .. fmtVector(item.boundsMin))
        add(target, "World corners max: " .. fmtVector(item.boundsMax))
    end
    add(target, "")
end

local function ancestorSummary(instance)
    local names = {}
    local current = instance
    local depth = 0
    while current and depth < 8 do
        table.insert(names, 1, current.Name .. "<" .. current.ClassName .. ">")
        if current == Workspace then
            break
        end
        current = current.Parent
        depth = depth + 1
    end
    return table.concat(names, "/")
end

local lobbyContainer = resolve({ "__OBJECTS", "Build", "MainMap", "LobbyBoundaries" })
local lobbyItems = {}
if lobbyContainer then
    for _, descendant in ipairs(lobbyContainer:GetDescendants()) do
        if descendant:IsA("BasePart") then
            table.insert(lobbyItems, inspectPart(descendant))
        end
    end
end
table.sort(lobbyItems, function(a, b)
    if a.distance == b.distance then
        return a.path < b.path
    end
    return a.distance < b.distance
end)
for index, item in ipairs(lobbyItems) do
    item.index = index
end

local boundaryParts = section(5200)
add(boundaryParts, "Container: " .. (lobbyContainer and safeFullName(lobbyContainer) or "NOT FOUND"))
add(boundaryParts, "Total BaseParts: " .. tostring(#lobbyItems))
add(boundaryParts, "")
for index, item in ipairs(lobbyItems) do
    if index > MAX_PARTS_REPORTED then
        add(boundaryParts, "[remaining parts omitted after " .. tostring(MAX_PARTS_REPORTED) .. "]")
        break
    end
    add(boundaryParts, "LOBBY_PART #" .. tostring(index))
    emitPart(boundaryParts, item, false)
end

local priorityWords = { "lobbyboundaries", "collisions", "coll", "guard", "boundary", "ground", "bases" }
local function priority(path)
    local lower = string.lower(path)
    for index, word in ipairs(priorityWords) do
        if string.find(lower, word, 1, true) then
            return index
        end
    end
    return #priorityWords + 1
end

local overlapItems = {}
local overlapOk, overlapResult = pcall(function()
    return Workspace:GetPartBoundsInBox(CFrame.new(DEATH), QUERY_SIZE, OverlapParams.new())
end)
if overlapOk then
    for _, part in ipairs(overlapResult) do
        if part:IsA("BasePart") then
            local item = inspectPart(part)
            item.priority = priority(item.path)
            table.insert(overlapItems, item)
        end
    end
    table.sort(overlapItems, function(a, b)
        if a.priority ~= b.priority then
            return a.priority < b.priority
        end
        if a.distance ~= b.distance then
            return a.distance < b.distance
        end
        return a.path < b.path
    end)
end

local overlapSection = section(4300)
add(overlapSection, "Query center: " .. fmtVector(DEATH))
add(overlapSection, "Query size: " .. fmtVector(QUERY_SIZE))
add(overlapSection, "Query status: " .. (overlapOk and "OK" or "FAILED"))
if not overlapOk then
    add(overlapSection, "Error: " .. tostring(overlapResult))
else
    add(overlapSection, "BaseParts returned: " .. tostring(#overlapItems))
    for index, item in ipairs(overlapItems) do
        if index > 50 then
            add(overlapSection, "[remaining overlap results omitted]")
            break
        end
        add(overlapSection, "OVERLAP #" .. tostring(index))
        add(overlapSection, "Path: " .. item.path)
        add(overlapSection, "Position: " .. fmtVector(item.part.Position) .. " | Size: " .. fmtVector(item.part.Size))
        add(overlapSection, "CanCollide=" .. tostring(item.part.CanCollide) .. " CanTouch=" .. tostring(item.part.CanTouch) .. " Transparency=" .. fmtNumber(item.part.Transparency))
        add(overlapSection, "Tags: " .. formatTags(item.part))
        add(overlapSection, "Ancestors: " .. ancestorSummary(item.part))
        add(overlapSection, "Inside death OBB=" .. tostring(item.inside) .. " expanded3=" .. tostring(item.expandedInside) .. " distance=" .. fmtNumber(item.distance))
        add(overlapSection, "")
    end
end

local collisions = resolve({ "__OBJECTS", "Build", "1", "COLLISIONS" })
local runtimeA = Vector3.new(1222.69, 85.56, -631.39)
local runtimeB = Vector3.new(1579.18, 85.56, -683.86)
local duplicateMatchesA = {}
local duplicateMatchesB = {}
if collisions then
    for _, descendant in ipairs(collisions:GetDescendants()) do
        if descendant:IsA("BasePart") then
            if (descendant.Position - runtimeA).Magnitude <= 2 then
                table.insert(duplicateMatchesA, inspectPart(descendant))
            end
            if (descendant.Position - runtimeB).Magnitude <= 2 then
                table.insert(duplicateMatchesB, inspectPart(descendant))
            end
        end
    end
end

local duplicateSection = section(3200)
add(duplicateSection, "COLLISIONS root: " .. (collisions and safeFullName(collisions) or "NOT FOUND"))
add(duplicateSection, "Runtime target A: " .. fmtVector(runtimeA) .. " | matches: " .. tostring(#duplicateMatchesA))
for index, item in ipairs(duplicateMatchesA) do
    add(duplicateSection, "A MATCH #" .. tostring(index))
    emitPart(duplicateSection, item, true)
end
add(duplicateSection, "Runtime target B: " .. fmtVector(runtimeB) .. " | matches: " .. tostring(#duplicateMatchesB))
for index, item in ipairs(duplicateMatchesB) do
    add(duplicateSection, "B MATCH #" .. tostring(index))
    emitPart(duplicateSection, item, true)
end

local topologySection = section(3000)
for index, item in ipairs(lobbyItems) do
    if index > MAX_PARTS_REPORTED then
        break
    end
    add(topologySection, string.format(
        "#%d %s | worldMin=%s worldMax=%s | deathDistanceOBB=%s expanded3=%s",
        index,
        item.path,
        fmtVector(item.boundsMin),
        fmtVector(item.boundsMax),
        fmtNumber(item.distance),
        tostring(item.expandedInside)
    ))
end

local nearSection = section(1800)
local nearCount = 0
for _, item in ipairs(lobbyItems) do
    if item.expandedInside then
        nearCount = nearCount + 1
        add(nearSection, "NEAR_DEATH_VOLUME " .. tostring(item.index) .. " " .. item.path)
        add(nearSection, "  Position=" .. fmtVector(item.part.Position) .. " Size=" .. fmtVector(item.part.Size))
        add(nearSection, "  local=" .. fmtVector(item.localPoint) .. " margin=" .. fmtVector(item.margin))
    end
end
if nearCount == 0 then
    add(nearSection, "none")
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

local function splitLines(source)
    local result = {}
    source = source:gsub("\r\n", "\n")
    if source:sub(-1) ~= "\n" then
        source = source .. "\n"
    end
    for line in source:gmatch("(.-)\n") do
        table.insert(result, line)
    end
    return result
end

local function allOccurrences(source, literal, maximum)
    local result = {}
    local start = 1
    while #result < maximum do
        local position = string.find(source, literal, start, true)
        if not position then
            break
        end
        table.insert(result, position)
        start = position + #literal
    end
    return result
end

local function lineAt(source, bytePosition)
    local _, count = source:sub(1, bytePosition):gsub("\n", "")
    return count + 1
end

local touchLiterals = { "Touched", "TouchEnded" }
local healthLiterals = { "Humanoid.Health", "Health = 0", "TakeDamage", "BreakJoints", "Died" }
local decompiler = globalFunction("decompile")
local lobbyScripts = {}
local guardScripts = {}
local scriptsScanned = 0
local decompileFailures = 0
local boundaryReference = false
local boundaryAndTouch = false
local boundaryAndHealth = false

local function containsAny(source, literals)
    for _, literal in ipairs(literals) do
        if string.find(source, literal, 1, true) then
            return true
        end
    end
    return false
end

if decompiler then
    for _, instance in ipairs(game:GetDescendants()) do
        if scriptsScanned >= MAX_SCRIPTS_SCANNED then
            break
        end
        if instance:IsA("LocalScript") or instance:IsA("ModuleScript") then
            scriptsScanned = scriptsScanned + 1
            local ok, source = pcall(decompiler, instance)
            if ok and type(source) == "string" then
                local hasLobby = string.find(source, "LobbyBoundaries", 1, true) ~= nil
                local hasTouch = containsAny(source, touchLiterals)
                local hasHealth = containsAny(source, healthLiterals)
                if hasLobby then
                    boundaryReference = true
                    boundaryAndTouch = boundaryAndTouch or hasTouch
                    boundaryAndHealth = boundaryAndHealth or hasHealth
                    if #lobbyScripts < 8 then
                        table.insert(lobbyScripts, { script = instance, source = source })
                    end
                elseif string.find(source, "COLL GUARD", 1, true) and #guardScripts < 4 then
                    table.insert(guardScripts, { script = instance, source = source })
                end
            else
                decompileFailures = decompileFailures + 1
            end
        end
    end
end

local referencesSection = section(7600)
add(referencesSection, "Decompiler: " .. (decompiler and "AVAILABLE" or "UNAVAILABLE"))
add(referencesSection, "Scripts scanned: " .. tostring(scriptsScanned) .. " | failures: " .. tostring(decompileFailures))
add(referencesSection, "Lobby scripts selected: " .. tostring(#lobbyScripts) .. " | additional COLL GUARD scripts selected: " .. tostring(#guardScripts))

local function emitContext(target, source, lines, literal, bytePosition)
    local hitLine = lineAt(source, bytePosition)
    local firstLine = math.max(1, hitLine - 20)
    local lastLine = math.min(#lines, hitLine + 20)
    if not add(target, string.format("HIT %q line %d context %d-%d", literal, hitLine, firstLine, lastLine)) then
        return false
    end
    for lineIndex = firstLine, lastLine do
        if not add(target, string.format("%5d | %s", lineIndex, clip(lines[lineIndex], 360))) then
            return false
        end
    end
    return true
end

local function emitScript(target, result, primaryLiteral)
    local source = result.source
    local lines = splitLines(source)
    if not add(target, "SCRIPT " .. safeFullName(result.script)) then
        return false
    end
    local primaryHits = allOccurrences(source, primaryLiteral, 5)
    add(target, "Primary occurrences reported: " .. tostring(#primaryHits))
    for _, bytePosition in ipairs(primaryHits) do
        if not emitContext(target, source, lines, primaryLiteral, bytePosition) then
            return false
        end
    end
    for _, literal in ipairs(touchLiterals) do
        local hit = string.find(source, literal, 1, true)
        if hit and not emitContext(target, source, lines, literal, hit) then
            return false
        end
    end
    for _, literal in ipairs(healthLiterals) do
        local hit = string.find(source, literal, 1, true)
        if hit and not emitContext(target, source, lines, literal, hit) then
            return false
        end
    end
    return add(target, "")
end

for _, result in ipairs(lobbyScripts) do
    if not emitScript(referencesSection, result, "LobbyBoundaries") then
        break
    end
end
if not referencesSection.truncated then
    for _, result in ipairs(guardScripts) do
        if not emitScript(referencesSection, result, "COLL GUARD") then
            break
        end
    end
end

local anyInside = false
for _, item in ipairs(lobbyItems) do
    anyInside = anyInside or item.inside
end

local matchedA = #duplicateMatchesA > 0
local matchedB = #duplicateMatchesB > 0

local summaryLines = {
    "LOBBY_BOUNDARY_PART_COUNT=" .. tostring(#lobbyItems),
    "DEATH_INSIDE_ANY_LOBBY_OBB=" .. tostring(anyInside),
    "DEATH_INSIDE_ANY_LOBBY_OBB_EXPANDED_3=" .. tostring(nearCount > 0),
    "NEAR_DEATH_VOLUME_COUNT=" .. tostring(nearCount),
    "MATCHED_RUNTIME_COLL_GUARD_PART=" .. tostring(matchedA),
    "MATCHED_RUNTIME_COLL2_PART=" .. tostring(matchedB),
    "LOBBY_BOUNDARY_REFERENCE_FOUND=" .. tostring(boundaryReference),
    "LOBBY_BOUNDARY_AND_TOUCH_SAME_SCRIPT=" .. tostring(boundaryAndTouch),
    "LOBBY_BOUNDARY_AND_HEALTH_SAME_SCRIPT=" .. tostring(boundaryAndHealth),
}

local reportParts = {
    "MEMORYTOOLS LOBBY TOPOLOGY PROBE V1.0.4.4",
    "",
    "[LOBBY BOUNDARY PARTS]",
    table.concat(boundaryParts.lines, "\n"),
    "",
    "[LOCAL OVERLAP AT DEATH]",
    table.concat(overlapSection.lines, "\n"),
    "",
    "[DUPLICATE COLLISION PART MATCHES]",
    table.concat(duplicateSection.lines, "\n"),
    "",
    "[BOUNDARY TOPOLOGY]",
    table.concat(topologySection.lines, "\n"),
    "",
    "[NEAR-DEATH VOLUMES]",
    table.concat(nearSection.lines, "\n"),
    "",
    "[FOCUSED CLIENT REFERENCES]",
    table.concat(referencesSection.lines, "\n"),
    "",
    "[SUMMARY]",
    table.concat(summaryLines, "\n"),
}

local report = table.concat(reportParts, "\n")
if #report > MAX_REPORT_BYTES then
    report = report:sub(1, MAX_REPORT_BYTES - 64) .. "\n[REPORT TRUNCATED TO 24 KB]"
end

print(report)
if type(setclipboard) == "function" then
    local ok, err = pcall(function()
        setclipboard(report)
    end)
    if not ok then
        warn("[MemoryTools Lobby Topology Probe] Clipboard copy failed: " .. tostring(err))
    end
else
    warn("[MemoryTools Lobby Topology Probe] Clipboard API unavailable; report was printed only")
end
