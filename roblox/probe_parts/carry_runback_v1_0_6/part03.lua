            miniButton.Text = "RUNBACK PROBE"
            updateTerminalCanvas(latestReport ~= nil)
        end
    end))

    session.Gui = gui
    updateStatus()
    updateTerminalCanvas(false)
    return gui
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

local function lineAt(source, bytePosition)
    local _, count = source:sub(1, bytePosition):gsub("\n", "")
    return count + 1
end

local function newSection(limit)
    return { Lines = {}, Bytes = 0, Limit = limit, Truncated = false }
end

local function addLine(section, line)
    if section.Truncated then
        return false
    end
    line = tostring(line)
    if section.Bytes + #line + 1 > section.Limit then
        local marker = "[SECTION TRUNCATED BY REPORT BUDGET]"
        if section.Bytes + #marker + 1 <= section.Limit then
            table.insert(section.Lines, marker)
            section.Bytes = section.Bytes + #marker + 1
        end
        section.Truncated = true
        return false
    end
    table.insert(section.Lines, line)
    section.Bytes = section.Bytes + #line + 1
    return true
end

local function appendWindow(list, value, now, window)
    table.insert(list, value)
    while #list > 0 and now - list[1].Time > window do
        table.remove(list, 1)
    end
end

local function scalar(value)
    local kind = typeof(value)
    if kind == "string" or kind == "number" or kind == "boolean" then
        return value
    end
    return nil
end

local carryKeys = {
    "IsCarrying",
    "Uid",
    "AreaId",
    "RunBackWakeDelayRequired",
    "GuardDisabled",
    "SpeedMultiplier",
    "CarrierUserId",
}

local function collectCarryFields(value, output, depth, visited)
    if type(value) ~= "table" or depth > 3 or visited[value] then
        return
    end
    visited[value] = true
    for _, key in ipairs(carryKeys) do
        local ok, item = pcall(function()
            return value[key]
        end)
        if ok and item ~= nil and output[key] == nil then
            output[key] = scalar(item) or clip(item, 120)
        end
    end
    for key, child in pairs(value) do
        if type(child) == "table" and depth < 3 then
            local lower = string.lower(tostring(key))
            if string.find(lower, "egg", 1, true)
                or string.find(lower, "carry", 1, true)
                or string.find(lower, "field", 1, true)
                or string.find(lower, "state", 1, true)
            then
                collectCarryFields(child, output, depth + 1, visited)
            end
        end
    end
end

local function readFieldEgg(uid)
    if type(uid) ~= "string" or uid == "" then
        return nil, "NO_UID"
    end
    if type(eggModule) ~= "table" or type(eggModule.ReadFieldEgg) ~= "function" then
        return nil, "UNAVAILABLE"
    end
    local ok, value = pcall(eggModule.ReadFieldEgg, uid)
    if ok then
        return value, "UID"
    end
    return nil, "ERROR"
end

local function snapshotCarry(...)
    local output = {}
    local values = table.pack(...)
    for index = 1, values.n do
        collectCarryFields(values[index], output, 1, {})
        if type(values[index]) == "boolean" and output.IsCarrying == nil then
            output.IsCarrying = values[index]
        end
    end
    local candidateUid = output.Uid
    if type(candidateUid) ~= "string" or candidateUid == "" then
        candidateUid = carryState.Uid
    end
    if type(candidateUid) ~= "string" or candidateUid == "" then
        candidateUid = observedCarryState.Uid
    end
    if type(candidateUid) ~= "string" or candidateUid == "" then
        candidateUid = nil
    end
    local readValue, readMode = readFieldEgg(candidateUid)
    collectCarryFields(readValue, output, 1, {})
    collectCarryFields(eggModule, output, 1, {})
    output.ReadMode = readMode
    if output.IsCarrying == nil then
        local carrier = output.CarrierUserId
        output.IsCarrying = carrier ~= nil and tostring(carrier) == tostring(LocalPlayer.UserId)
    end
    return output
end

local function stateValue(state, key)
    local value = state[key]
    return value == nil and "UNKNOWN" or tostring(value)
end

local function recordCarryEvent(reason, state)
    local now = os.clock()
    table.insert(carryEvents, {
        Time = now,
        Reason = reason,
        State = state,
    })
    print(string.format(
        "[CarryChanged] timestamp=%.3f IsCarrying=%s Uid=%s AreaId=%s RunBackWakeDelayRequired=%s GuardDisabled=%s SpeedMultiplier=%s",
        now,
        stateValue(state, "IsCarrying"),
        stateValue(state, "Uid"),
        stateValue(state, "AreaId"),
        stateValue(state, "RunBackWakeDelayRequired"),
        stateValue(state, "GuardDisabled"),
        stateValue(state, "SpeedMultiplier")
    ))
end

local function getCharacterParts()
    local currentCharacter = LocalPlayer.Character
    if currentCharacter ~= character then
        if healthConnection then
            healthConnection:Disconnect()
            healthConnection = nil
            session.HealthConnection = nil
        end
        character = currentCharacter
        humanoid = nil
        rootPart = nil
    end
    if character then
        if not humanoid or not humanoid.Parent then
            humanoid = character:FindFirstChildOfClass("Humanoid")
        end
        if not rootPart or not rootPart.Parent then
            rootPart = character:FindFirstChild("HumanoidRootPart")
        end
    end
    if humanoid and not healthConnection then
        healthConnection = humanoid.HealthChanged:Connect(function(newHealth)
            if session.Active and carrying and carryInitialHealth and newHealth < carryInitialHealth then
                healthDropped = true
            end
        end)
