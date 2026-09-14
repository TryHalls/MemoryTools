        if instance:IsA("LocalScript") or instance:IsA("ModuleScript") then
            sourceState.Scanned = sourceState.Scanned + 1
            local source = decompileOnce(instance)
            if source then
                local speedScore = countHits(source, speedLiterals)
                if speedScore > 0 then
                    table.insert(speedResults, { Script = instance, Source = source, Score = speedScore })
                end
                local guardScore = countHits(source, guardLiterals)
                if guardScore >= 2 then
                    table.insert(guardResults, { Script = instance, Source = source, Score = guardScore })
                end
            end
        end
    end
    local function ranked(a, b)
        if a.Score ~= b.Score then return a.Score > b.Score end
        return safeFullName(a.Script) < safeFullName(b.Script)
    end
    table.sort(speedResults, ranked)
    table.sort(guardResults, ranked)
    for index = 1, math.min(#speedResults, MAX_RELEVANT_SCRIPTS) do
        table.insert(sourceState.Speed, speedResults[index])
    end
    for index = 1, math.min(#guardResults, MAX_RELEVANT_SCRIPTS) do
        table.insert(sourceState.Guard, guardResults[index])
    end
    sourceState.Status = "COMPLETE"
    visualState.Sources = "READY"
    terminalLine("SOURCE", "Source scan finished: " .. tostring(sourceState.Scanned) .. " scripts inspected")
    updateStatus()
end

local function buildReport(reason)
    local carrySection = newSection(2200)
    addLine(carrySection, "Report reason: " .. reason)
    addLine(carrySection, "Source scan status: " .. sourceState.Status)
    addLine(carrySection, "Current IsCarrying: " .. stateValue(carryState, "IsCarrying"))
    for _, event in ipairs(carryEvents) do
        local state = event.State
        addLine(carrySection, string.format(
            "t=%.3f reason=%s IsCarrying=%s Uid=%s AreaId=%s RunBackWakeDelayRequired=%s GuardDisabled=%s SpeedMultiplier=%s CarrierUserId=%s read=%s",
            event.Time - startedAt, event.Reason, stateValue(state, "IsCarrying"), stateValue(state, "Uid"),
            stateValue(state, "AreaId"), stateValue(state, "RunBackWakeDelayRequired"),
            stateValue(state, "GuardDisabled"), stateValue(state, "SpeedMultiplier"),
            stateValue(state, "CarrierUserId"), stateValue(state, "ReadMode")
        ))
    end

    local playerSection = newSection(4300)
    local speeds = {}
    local speedSum = 0
    local maxSpeed = 0
    local minWalk = math.huge
    local maxWalk = -math.huge
    for _, sample in ipairs(samples) do
        table.insert(speeds, sample.Horizontal)
        speedSum = speedSum + sample.Horizontal
        maxSpeed = math.max(maxSpeed, sample.Horizontal)
        minWalk = math.min(minWalk, sample.Walk)
        maxWalk = math.max(maxWalk, sample.Walk)
    end
    local averageSpeed = #samples > 0 and speedSum / #samples or nil
    local p95 = approximateP95(speeds)
    addLine(playerSection, "samples=" .. tostring(#samples))
    addLine(playerSection, "max=" .. fmtNumber(#samples > 0 and maxSpeed or nil))
    addLine(playerSection, "average=" .. fmtNumber(averageSpeed))
    addLine(playerSection, "p95_approx=" .. fmtNumber(p95))
    addLine(playerSection, "WalkSpeed observed min/max=" .. fmtNumber(#samples > 0 and minWalk or nil) .. "/" .. fmtNumber(#samples > 0 and maxWalk or nil))
    addLine(playerSection, "time | WalkSpeed | state | MoveDirection | position | velocity | horizontalVelocity | carry AreaId | player area evidence")
    for _, sample in ipairs(samples) do
        addLine(playerSection, string.format(
            "%.2f | %.2f | %s | %s | %s | %s | %.2f | %s | %s",
            sample.Time - startedAt, sample.Walk, sample.State, fmtVector(sample.Direction),
            fmtVector(sample.Position), fmtVector(sample.Velocity), sample.Horizontal,
            sample.AreaId == nil and "UNKNOWN" or tostring(sample.AreaId), clip(sample.AreaEvidence, 180)
        ))
    end

    local guardSection = newSection(3300)
    addLine(guardSection, "Candidates are evidence-ranked objects, not confirmed guards.")
    for index, item in ipairs(candidateOrder) do
        local count = math.max(item.Count, 1)
        local candidateHumanoid = item.Humanoid
        addLine(guardSection, string.format(
            "#%d Name=%s | GetFullName=%s | tags=%s | evidence=%s | position=%s | linearVelocity=%s | HumanoidWalkSpeed=%s | distance=%.2f",
            index, item.Instance.Name, safeFullName(item.Instance), item.Tags ~= "" and item.Tags or "none",
            table.concat(item.Evidence, ","), item.Root and item.Root.Parent and fmtVector(item.Root.Position) or "unavailable",
            item.Root and item.Root.Parent and fmtVector(item.Root.AssemblyLinearVelocity) or "unavailable",
            candidateHumanoid and fmtNumber(candidateHumanoid.WalkSpeed) or "n/a",
            item.Samples[#item.Samples] and item.Samples[#item.Samples].Distance or math.huge
        ))
        addLine(guardSection, string.format(
            "   speedA linear max/avg=%.3f/%.3f | speedB position-delta max/avg=%.3f/%.3f | minimum distance=%.3f | samples=%d",
            item.MaxLinear, item.SumLinear / count, item.MaxDelta,
            item.DeltaCount > 0 and item.SumDelta / item.DeltaCount or 0, item.MinDistance, item.Count
        ))
    end
    if #candidateOrder == 0 then addLine(guardSection, "none found") end

    local dropSection = newSection(3300)
    if not dropSnapshot then
        addLine(dropSection, "No true-to-false carry transition observed.")
    else
        addLine(dropSection, "CARRY_ENDED timestamp=" .. fmtNumber(dropSnapshot.Time - startedAt))
        addLine(dropSection, "Last 5 seconds: time | player velocity | health | candidate | distance | linear speed | delta speed")
        for _, sample in ipairs(dropSnapshot.Player) do
