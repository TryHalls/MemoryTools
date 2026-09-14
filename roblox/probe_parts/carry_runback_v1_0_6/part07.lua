            addLine(dropSection, string.format("%.2f | %.2f | %.2f | - | - | - | -", sample.Time - startedAt, sample.Horizontal, sample.Health))
        end
        for _, entry in ipairs(dropSnapshot.Guards) do
            addLine(dropSection, string.format(
                "%.2f | - | - | %s | %.2f | %.2f | %.2f",
                entry.Time - startedAt, clip(entry.Name, 90), entry.Distance, entry.Linear, entry.Delta
            ))
        end
    end

    local eggSource = newSection(5100)
    addLine(eggSource, "Decompiler status=" .. sourceState.Status .. " scripts=" .. tostring(sourceState.Scanned) .. " failures=" .. tostring(sourceState.Failures))
    if #sourceState.Egg == 0 then
        addLine(eggSource, "EggState source unavailable or module not found.")
    else
        sourceContext(eggSource, sourceState.Egg[1], 20, eggLiterals)
    end

    local speedSource = newSection(3000)
    addLine(speedSource, "Relevant scripts selected=" .. tostring(#sourceState.Speed) .. " (maximum " .. tostring(MAX_RELEVANT_SCRIPTS) .. ")")
    for _, record in ipairs(sourceState.Speed) do
        addLine(speedSource, "SELECTED " .. safeFullName(record.Script) .. " score=" .. tostring(record.Score))
    end
    for _, record in ipairs(sourceState.Speed) do
        sourceContext(speedSource, record, 20, speedLiterals)
        if speedSource.Truncated then break end
    end
    if #sourceState.Speed == 0 then addLine(speedSource, "No matching client-visible source.") end

    local guardSource = newSection(3700)
    addLine(guardSource, "Relevant scripts selected=" .. tostring(#sourceState.Guard) .. " (maximum " .. tostring(MAX_RELEVANT_SCRIPTS) .. ")")
    for _, record in ipairs(sourceState.Guard) do
        addLine(guardSource, "SELECTED " .. safeFullName(record.Script) .. " score=" .. tostring(record.Score))
    end
    for _, record in ipairs(sourceState.Guard) do
        sourceContext(guardSource, record, 25, guardLiterals)
        if guardSource.Truncated then break end
    end
    if #sourceState.Guard == 0 then addLine(guardSource, "No multi-keyword client-visible source.") end

    local guardMax = nil
    for _, item in ipairs(candidateOrder) do
        guardMax = math.max(guardMax or 0, item.MaxLinear, item.MaxDelta)
    end
    local summary = {
        "CARRY_LOST means a true->false carry transition only; it does not distinguish successful deposit from forced drop.",
        "CARRY_SPEED_MULTIPLIER_OBSERVED=" .. stateValue(observedCarryState, "SpeedMultiplier"),
        "RUNBACK_WAKE_DELAY_REQUIRED=" .. stateValue(observedCarryState, "RunBackWakeDelayRequired"),
        "GUARD_DISABLED=" .. stateValue(observedCarryState, "GuardDisabled"),
        "PLAYER_MANUAL_MAX_SPEED=" .. fmtNumber(#samples > 0 and maxSpeed or nil),
        "PLAYER_MANUAL_P95_SPEED=" .. fmtNumber(p95),
        "GUARD_CANDIDATE_FOUND=" .. tostring(#candidateOrder > 0),
        "GUARD_MAX_OBSERVED_SPEED=" .. fmtNumber(guardMax),
        "CARRY_LOST=" .. tostring(carryLost),
        "HEALTH_DROPPED=" .. tostring(healthDropped),
    }
    local parts = {
        "MEMORYTOOLS CARRY RUNBACK PROBE V1.0.6", "",
        "[CARRY STATE]", table.concat(carrySection.Lines, "\n"), "",
        "[PLAYER MANUAL SPEED]", table.concat(playerSection.Lines, "\n"), "",
        "[GUARD CANDIDATES]", table.concat(guardSection.Lines, "\n"), "",
        "[GUARD SPEED]", "Included per candidate above using both requested methods.", "",
        "[SOURCE: EGGSTATE]", table.concat(eggSource.Lines, "\n"), "",
        "[SOURCE: SPEED]", table.concat(speedSource.Lines, "\n"), "",
        "[SOURCE: GUARD]", table.concat(guardSource.Lines, "\n"), "",
        "[DROP EVENT]", table.concat(dropSection.Lines, "\n"), "",
        "[SUMMARY]", table.concat(summary, "\n"),
    }
    local report = table.concat(parts, "\n")
    if #report > MAX_REPORT_BYTES then
        local ending = "[SUMMARY]\n" .. table.concat(summary, "\n")
        report = report:sub(1, math.max(0, MAX_REPORT_BYTES - #ending - 2)) .. "\n\n" .. ending
    end
    return report
end

publishReport = function(reason)
    local report = buildReport(reason)
    latestReport = report
    visualState.Report = "READY"
    updateStatus()
    terminalSet(table.concat({
        "================================",
        "REPORT READY",
        "================================",
        report,
    }, "\n"))
    if mainFrame and not mainFrame.Visible and miniButton then
        miniButton.Text = "REPORT READY"
    end
    setFeedback("REPORT READY", 4)
    terminalLine("REPORT", "Report generated: " .. reason)
    print(report)
    reportGenerated = true
    return report
end

copyLatestReport = function()
    if not latestReport then
        setFeedback("NO REPORT AVAILABLE", 4)
        terminalLine("COPY", "No report available")
        return false
    end
    local clipboardWriter = globalFunction("setclipboard") or globalFunction("toclipboard")
    if not clipboardWriter then
        setFeedback("CLIPBOARD UNAVAILABLE", 5)
        terminalLine("COPY", "CLIPBOARD UNAVAILABLE; report remains visible")
        return false
    end
    local ok, problem = pcall(clipboardWriter, latestReport)
    if not ok then
        setFeedback("CLIPBOARD ERROR", 5)
        terminalLine("COPY", "Clipboard error: " .. clip(problem, 180))
        return false
    end
    visualState.Report = "COPIED"
    updateStatus()
    setFeedback("REPORT COPIED TO CLIPBOARD", 5)
    terminalLine("COPY", "REPORT COPIED TO CLIPBOARD")
    return true
end

local function captureDrop(now)
    local recentPlayer = {}
    for _, sample in ipairs(samples) do
        if now - sample.Time <= DROP_WINDOW then
            table.insert(recentPlayer, sample)
        end
    end
    local guards = {}
    for _, item in ipairs(candidateOrder) do
        for _, sample in ipairs(item.Samples) do
            if now - sample.Time <= DROP_WINDOW then
                table.insert(guards, {
                    Time = sample.Time,
