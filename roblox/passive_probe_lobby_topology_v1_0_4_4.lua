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
    return string.format("%.4f", value)
end

local function fmtVector(value)
    return string.format("(%.4f,%.4f,%.4f)", value.X, value.Y, value.Z)
end

local function fmtFrame(value)
    return clip(tostring(value), 260)
end

local function safeFullName(instance)
    local ok, value = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(value, 500) or "<unavailable>"
end

local function quote(value)
    return string.format("%q", clip(value, 180))
end

local function sortedKeys(dictionary)
    local keys = {}
    for key in pairs(dictionary) do
        table.insert(keys, key)
    end
    table.sort(keys, function(a, b)
        return tostring(a) < tostring(b)
    end)
    return keys
end

local function fmtValue(value)
    local kind = typeof(value)
    if kind == "string" then
        return quote(value)
    elseif kind == "number" then
        return fmtNumber(value)
    elseif kind == "Vector3" then
        return fmtVector(value)
    elseif kind == "CFrame" then
        return fmtFrame(value)
    elseif kind == "Color3" then
        return string.format("(%.4f,%.4f,%.4f)", value.R, value.G, value.B)
    elseif kind == "Instance" then
        return safeFullName(value)
    end
    return clip(value, 180)
end

local function fmtAttributes(instance)
    local ok, attributes = pcall(function()
        return instance:GetAttributes()
    end)
    if not ok then
        return "<error>"
    end
    local pieces = {}
    for _, key in ipairs(sortedKeys(attributes)) do
        table.insert(pieces, quote(key) .. ":" .. fmtValue(attributes[key]))
    end
    return "{" .. clip(table.concat(pieces, ","), 500) .. "}"
end

local function fmtTags(instance)
    local ok, tags = pcall(function()
        return CollectionService:GetTags(instance)
    end)
    if not ok then
        return "<error>"
    end
    table.sort(tags)
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

local function containsPoint(localPoint, halfSize)
    return math.abs(localPoint.X) <= halfSize.X
        and math.abs(localPoint.Y) <= halfSize.Y
        and math.abs(localPoint.Z) <= halfSize.Z
end

local function inspectPart(part)
    local halfSize=part.Size / 2
    local localPoint = part.CFrame:PointToObjectSpace(DEATH)
    local outside = Vector3.new(
        math.max(math.abs(localPoint.X) - halfSize.X, 0),
        math.max(math.abs(localPoint.Y) - halfSize.Y, 0),
        math.max(math.abs(localPoint.Z) - halfSize.Z, 0)
    )
    local minimum = Vector3.new(math.huge, math.huge, math.huge)
    local maximum = Vector3.new(-math.huge, -math.huge, -math.huge)
    for _, x in ipairs({ -halfSize.X, halfSize.X }) do
        for _, y in ipairs({ -halfSize.Y, halfSize.Y }) do
            for _, z in ipairs({ -halfSize.Z, halfSize.Z }) do
                local corner = part.CFrame * Vector3.new(x, y, z)
                minimum = Vector3.new(
                    math.min(minimum.X, corner.X),
                    math.min(minimum.Y, corner.Y),
                    math.min(minimum.Z, corner.Z)
                )
                maximum = Vector3.new(
                    math.max(maximum.X, corner.X),
                    math.max(maximum.Y, corner.Y),
                    math.max(maximum.Z, corner.Z)
                )
            end
        end
    end
    return {
        part = part,
        path = safeFullName(part),
        position = part.Position,
        size = part.Size,
        frame = part.CFrame,
        orientation = part.Orientation,
        half = halfSize,
        localPoint = localPoint,
        margin = halfSize - Vector3.new(math.abs(localPoint.X), math.abs(localPoint.Y), math.abs(localPoint.Z)),
        distance = outside.Magnitude,
        inside = containsPoint(localPoint, halfSize),
        expanded = containsPoint(localPoint, halfSize + EXPANSION),
        minimum = minimum,
        maximum = maximum,
    }
end

local function partDetails(item)
    local part = item.part
    return table.concat({
        "path:" .. item.path,
        "Name:" .. quote(part.Name),
        "Class:" .. part.ClassName,
        "Position:" .. fmtVector(item.position),
        "Size:" .. fmtVector(item.size),
        "CFrame:" .. fmtFrame(item.frame),
        "Orientation:" .. fmtVector(item.orientation),
        "CanCollide:" .. tostring(part.CanCollide),
        "CanTouch:" .. tostring(part.CanTouch),
        "CanQuery:" .. tostring(part.CanQuery),
        "Transparency:" .. fmtNumber(part.Transparency),
        "tags:" .. fmtTags(part),
        "attributes:" .. fmtAttributes(part),
    }, " | ")
end

local function ancestors(instance)
    local names = {}
    local current = instance.Parent
    while current and #names < 7 do
        table.insert(names, 1, current.Name .. "<" .. current.ClassName .. ">")
        if current == Workspace then
            break
        end
        current = current.Parent
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

local boundaryParts = section(5900)
add(boundaryParts, "Container: " .. (lobbyContainer and safeFullName(lobbyContainer) or "NOT FOUND"))
add(boundaryParts, "BasePart descendants: " .. tostring(#lobbyItems))
add(boundaryParts, "Sorted by approximate distance from death to OBB; reporting at most " .. tostring(MAX_PARTS_REPORTED))
for index = 1, math.min(#lobbyItems, MAX_PARTS_REPORTED) do
    local item = lobbyItems[index]
    add(boundaryParts, string.format(
        "#%03d %s | deathLocal:%s | halfSize:%s | insideOBB:%s | axisMargin:%s | distanceOBB:%s | insideExpanded3:%s",
        item.index,
        partDetails(item),
        fmtVector(item.localPoint),
        fmtVector(item.half),
        tostring(item.inside),
        fmtVector(item.margin),
        fmtNumber(item.distance),
        tostring(item.expanded)
    ))
end

local priorityWords = {
    "lobbyboundaries", "collisions", "coll", "guard", "boundary", "ground", "bases",
}

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

local overlapSection = section(2400)
add(overlapSection, "Query center: " .. fmtVector(DEATH) .. " | query box: " .. fmtVector(QUERY_SIZE))
add(overlapSection, "Query status: " .. (overlapOk and "OK" or ("ERROR " .. clip(overlapResult, 200))))
add(overlapSection, "BaseParts returned: " .. tostring(#overlapItems))
for index, item in ipairs(overlapItems) do
    if not add(overlapSection, string.format(
        "#%03d priority:%d | %s | ancestors:%s",
        index,
        item.priority,
        partDetails(item),
        clip(ancestors(item.part), 500)
    )) then
        break
    end
end

local duplicateSection = section(1800)
local collisions = resolve({ "__OBJECTS", "Build", "1", "COLLISIONS" })
local targetA = Vector3.new(1222.69, 85.56, -631.39)
local targetB = Vector3.new(1579.18, 85.56, -683.86)
local matchedA = false
local matchedB = false
local duplicateMatches = {}

local function withinTolerance(position, target)
    return (position - target).Magnitude <= 2
end

if collisions then
    for _, descendant in ipairs(collisions:GetDescendants()) do
        if descendant:IsA("BasePart") then
            local matchesA = withinTolerance(descendant.Position, targetA)
            local matchesB = withinTolerance(descendant.Position, targetB)
            if matchesA or matchesB then
                matchedA = matchedA or matchesA
                matchedB = matchedB or matchesB
                table.insert(duplicateMatches, {
                    item = inspectPart(descendant),
                    label = (matchesA and "A" or "") .. (matchesB and "B" or ""),
                })
            end
        end
    end
end
table.sort(duplicateMatches, function(a, b)
    return a.item.path < b.item.path
end)
add(duplicateSection, "Container: " .. (collisions and safeFullName(collisions) or "NOT FOUND"))
add(duplicateSection, "A target: " .. fmtVector(targetA) .. " | B target: " .. fmtVector(targetB) .. " | position-distance tolerance: 2")
add(duplicateSection, "Matches: " .. tostring(#duplicateMatches))
for index, match in ipairs(duplicateMatches) do
    local item = match.item
    add(duplicateSection, string.format(
        "#%03d target:%s | %s | deathLocal:%s | halfSize:%s | insideOBB:%s | axisMargin:%s | distanceOBB:%s | insideExpanded3:%s | worldMin:%s | worldMax:%s",
        index,
        match.label,
        partDetails(item),
        fmtVector(item.localPoint),
        fmtVector(item.half),
        tostring(item.inside),
        fmtVector(item.margin),
        fmtNumber(item.distance),
        tostring(item.expanded),
        fmtVector(item.minimum),
        fmtVector(item.maximum)
    ))
end

local topologySection = section(2900)
for index = 1, math.min(#lobbyItems, MAX_PARTS_REPORTED) do
    local item = lobbyItems[index]
    add(topologySection, string.format(
        "#%03d %s | worldCornerMin:%s | worldCornerMax:%s",
        item.index,
        item.path,
        fmtVector(item.minimum),
        fmtVector(item.maximum)
    ))
end

local observedPosition=Vector3.new(556.301758, 85.560913, -512.275024)
local observed = nil
local observedDistance = math.huge
for _, item in ipairs(lobbyItems) do
    local distance = (item.position - observedPosition).Magnitude
    if distance < observedDistance then
        observed = item
        observedDistance = distance
    end
end

local function overlaps(aMin, aMax, bMin, bMax)
    return aMax >= bMin and bMax >= aMin
end

local function joinIndices(indices)
    if #indices == 0 then
        return "none"
    end
    local output = {}
    for _, index in ipairs(indices) do
        table.insert(output, string.format("#%03d", index))
    end
    return table.concat(output, ",")
end

if observed then
    local positiveZ = {}
    local negativeZ = {}
    local aboveY = {}
    for _, item in ipairs(lobbyItems) do
        if item ~= observed then
            local xyOverlap = overlaps(item.minimum.X, item.maximum.X, observed.minimum.X, observed.maximum.X)
                and overlaps(item.minimum.Y, item.maximum.Y, observed.minimum.Y, observed.maximum.Y)
            local xzOverlap = overlaps(item.minimum.X, item.maximum.X, observed.minimum.X, observed.maximum.X)
                and overlaps(item.minimum.Z, item.maximum.Z, observed.minimum.Z, observed.maximum.Z)
            if xyOverlap and item.maximum.Z > observed.maximum.Z then
                table.insert(positiveZ, item.index)
            end
            if xyOverlap and item.minimum.Z < observed.minimum.Z then
                table.insert(negativeZ, item.index)
            end
            if xzOverlap and item.maximum.Y > observed.maximum.Y then
                table.insert(aboveY, item.index)
            end
        end
    end
    add(topologySection, "Observed-wall nearest center: #" .. string.format("%03d", observed.index) .. " " .. observed.path)
    add(topologySection, "Observed center delta: " .. fmtNumber(observedDistance) .. " | worldCornerMin:" .. fmtVector(observed.minimum) .. " | worldCornerMax:" .. fmtVector(observed.maximum))
    add(topologySection, "Z greater than " .. fmtNumber(observed.maximum.Z) .. "; other boundary AABBs extending beyond with XY overlap: " .. joinIndices(positiveZ))
    add(topologySection, "Z less than " .. fmtNumber(observed.minimum.Z) .. "; other boundary AABBs extending beyond with XY overlap: " .. joinIndices(negativeZ))
    add(topologySection, "Y greater than " .. fmtNumber(observed.maximum.Y) .. "; other boundary AABBs extending above with XZ overlap: " .. joinIndices(aboveY))
else
    add(topologySection, "Observed wall match: unavailable")
end

local nearSection = section(800)
local nearCount = 0
for _, item in ipairs(lobbyItems) do
    if item.expanded then
        nearCount = nearCount + 1
        add(nearSection, string.format("NEAR_DEATH_VOLUME %03d %s", item.index, item.path))
    end
end
if nearCount == 0 then
    add(nearSection, "none")
end

local function globalFunction(name)
    local ok, value = pcall(function()
        return getfenv()[name]
    end)
    return ok and type(value) == "function" and value or nil
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

local function allOccurrences(source, literal, maximum)
    local positions = {}
    local startAt = 1
    while #positions < maximum do
        local found = string.find(source, literal, startAt, true)
        if not found then
            break
        end
        table.insert(positions, found)
        startAt = found + math.max(#literal, 1)
    end
    return positions
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
    local summary = "[SUMMARY]\n" .. table.concat(summaryLines, "\n")
    report = report:sub(1, math.max(0, MAX_REPORT_BYTES - #summary - 2)) .. "\n\n" .. summary
end

print(report)

local clipboardWriter = globalFunction("setclipboard") or globalFunction("toclipboard")
if clipboardWriter then
    local ok, clipboardError = pcall(clipboardWriter, report)
    if not ok then
        warn("[MemoryTools Lobby Topology Probe] Clipboard copy failed: " .. clip(clipboardError, 300))
    end
else
    warn("[MemoryTools Lobby Topology Probe] Clipboard API unavailable; report was printed only")
end

print("[MemoryTools Lobby Topology Probe] Version " .. VERSION .. " complete")
