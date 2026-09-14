                    deltaSpeed = horizontalMagnitude(position - item.PreviousPosition) / dt
                    item.SumDelta = item.SumDelta + deltaSpeed
                    item.DeltaCount = item.DeltaCount + 1
                end
            end
            item.PreviousPosition = position
            item.PreviousTime = now
            item.MinDistance = math.min(item.MinDistance, distance)
            item.MaxLinear = math.max(item.MaxLinear, linearSpeed)
            item.SumLinear = item.SumLinear + linearSpeed
            item.MaxDelta = math.max(item.MaxDelta, deltaSpeed)
            item.Count = item.Count + 1
            appendWindow(item.Samples, {
                Time = now,
                Distance = distance,
                Linear = linearSpeed,
                Delta = deltaSpeed,
            }, now, SAMPLE_WINDOW)
        end
    end
end

local function takeSample(now)
    local currentHumanoid, currentRoot = getCharacterParts()
    if not currentHumanoid or not currentRoot or not currentHumanoid.Parent or not currentRoot.Parent then
        return
    end
    local velocity = currentRoot.AssemblyLinearVelocity
    local sample = {
        Time = now,
        Health = currentHumanoid.Health,
        Walk = currentHumanoid.WalkSpeed,
        State = currentHumanoid:GetState().Name,
        Direction = currentHumanoid.MoveDirection,
        Position = currentRoot.Position,
        Velocity = velocity,
        Horizontal = horizontalMagnitude(velocity),
        AreaId = carryState.AreaId,
        AreaEvidence = getAreaEvidence(),
    }
    appendWindow(samples, sample, now, SAMPLE_WINDOW)
    if carryInitialHealth and sample.Health < carryInitialHealth then
        healthDropped = true
    end
    if now - lastDiscoveryAt >= DISCOVERY_INTERVAL then
        lastDiscoveryAt = now
        discoverCandidates(sample.Position)
    end
    sampleCandidates(now, sample.Position)
end

local function approximateP95(values)
    if #values == 0 then
        return nil
    end
    table.sort(values)
    return values[math.clamp(math.ceil(#values * 0.95), 1, #values)]
end

local function sourceContext(section, record, radius, literals)
    addLine(section, "SCRIPT " .. safeFullName(record.Script) .. " | score=" .. tostring(record.Score or 0))
    local ranges = {}
    for _, literal in ipairs(literals) do
        local bytePosition = string.find(record.Source, literal, 1, true)
        if bytePosition then
            local hit = lineAt(record.Source, bytePosition)
            table.insert(ranges, { First = math.max(1, hit - radius), Last = hit + radius, Hits = { { Line = hit, Literal = literal } } })
        end
    end
    table.sort(ranges, function(a, b) return a.First < b.First end)
    local lines = splitLines(record.Source)
    local merged = {}
    for _, range in ipairs(ranges) do
        range.Last = math.min(#lines, range.Last)
        local previous = merged[#merged]
        if previous and range.First <= previous.Last + 1 then
            previous.Last = math.max(previous.Last, range.Last)
            for _, hit in ipairs(range.Hits) do table.insert(previous.Hits, hit) end
        else
            table.insert(merged, range)
        end
    end
    for _, range in ipairs(merged) do
        local labels = {}
        for _, hit in ipairs(range.Hits) do
            table.insert(labels, string.format("%s@%d", hit.Literal, hit.Line))
        end
        addLine(section, string.format("HITS %s | merged context %d-%d", table.concat(labels, ","), range.First, range.Last))
        for index = range.First, range.Last do
            if not addLine(section, string.format("%5d | %s", index, clip(lines[index], 320))) then
                return
            end
        end
    end
    addLine(section, "")
end

local eggLiterals = {
    "RunBackWakeDelayRequired", "GuardDisabled", "SpeedMultiplier", "CarryChanged",
    "IsCarrying", "CarrierUserId", "DropFieldEgg", "AskFieldEggDrop",
}
local speedLiterals = { "WalkSpeedGovernor", "SpeedPowerProjection", "SpeedMultiplier", "RunBack" }
local guardLiterals = {
    "Guard", "GuardAreas", "RunBack", "Wake", "WakeDelay", "Chase", "Target",
    "Carrier", "IsCarrying", "SpeedMultiplier", "Egg",
}

local function countHits(source, literals)
    local count = 0
    local hits = {}
    for _, literal in ipairs(literals) do
        if string.find(source, literal, 1, true) then
            count = count + 1
            table.insert(hits, literal)
        end
    end
    return count, hits
end

local function inspectSources(eggScript)
    terminalLine("SOURCE", "Source scan started")
    visualState.Sources = "SCANNING"
    updateStatus()
    local decompiler = globalFunction("decompile")
    if not decompiler then
        sourceState.Status = "DECOMPILE UNAVAILABLE"
        visualState.Sources = "READY"
        terminalLine("SOURCE", "Source scan finished: decompiler unavailable")
        updateStatus()
        return
    end
    sourceState.Status = "SCANNING"
    local cache = {}
    local function decompileOnce(instance)
        local cached = cache[instance]
        if cached ~= nil then
            return cached ~= false and cached or nil
        end
        local ok, source = pcall(decompiler, instance)
        if ok and type(source) == "string" then
            cache[instance] = source
            return source
        end
        cache[instance] = false
        sourceState.Failures = sourceState.Failures + 1
        return nil
    end
    if eggScript then
        local source = decompileOnce(eggScript)
        if source then
            table.insert(sourceState.Egg, { Script = eggScript, Source = source, Score = 100 })
        end
    end
    local speedResults = {}
    local guardResults = {}
    for _, instance in ipairs(game:GetDescendants()) do
        if not session.Active then
            sourceState.Status = "STOPPED"
            return
        end
        if sourceState.Scanned >= MAX_SCRIPTS_SCANNED then
            break
        end
