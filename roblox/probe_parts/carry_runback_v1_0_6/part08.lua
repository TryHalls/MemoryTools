                    Name = safeFullName(item.Instance),
                    Distance = sample.Distance,
                    Linear = sample.Linear,
                    Delta = sample.Delta,
                })
            end
        end
    end
    dropSnapshot = { Time = now, Player = recentPlayer, Guards = guards }
end

local function applyCarryState(state, reason)
    local wasCarrying = carrying
    carryState = state
    carrying = state.IsCarrying == true
    if carrying and not wasCarrying then
        observedCarryState = {}
    end
    if carrying then
        for _, key in ipairs(carryKeys) do
            if state[key] ~= nil then
                observedCarryState[key] = state[key]
            end
        end
    end
    if carrying and not wasCarrying then
        carryStartedAt = os.clock()
        samples = {}
        candidates = {}
        candidateOrder = {}
        dropSnapshot = nil
        carryLost = false
        healthDropped = false
        reportGenerated = false
        latestReport = nil
        if miniButton then
            miniButton.Text = "RUNBACK PROBE"
        end
        local currentHumanoid = getCharacterParts()
        carryInitialHealth = currentHumanoid and currentHumanoid.Health or nil
        recordCarryEvent(reason, state)
        visualState.Carry = "CARRYING"
        visualState.Report = "WAITING"
        updateStatus()
        terminalLine("CARRY", "Carry started")
        terminalLine("CARRY", "UID=" .. stateValue(state, "Uid"))
        terminalLine("CARRY", "AreaId=" .. stateValue(state, "AreaId"))
        terminalLine("CARRY", "SpeedMultiplier=" .. stateValue(state, "SpeedMultiplier"))
        terminalLine("CARRY", "RunBackWakeDelayRequired=" .. stateValue(state, "RunBackWakeDelayRequired"))
        terminalLine("CARRY", "GuardDisabled=" .. stateValue(state, "GuardDisabled"))
    elseif not carrying and wasCarrying then
        local now = os.clock()
        carryLost = true
        local currentHumanoid = getCharacterParts()
        if carryInitialHealth and currentHumanoid and currentHumanoid.Health < carryInitialHealth then
            healthDropped = true
        end
        recordCarryEvent(reason, state)
        captureDrop(now)
        visualState.Carry = "ENDED"
        updateStatus()
        terminalLine("CARRY", "Carry ended; true-to-false transition observed")
        publishReport("CARRY_ENDED")
    elseif reason == "CarryChanged" then
        recordCarryEvent(reason, state)
        updateStatus()
        terminalLine("CARRY", "Carry state updated; IsCarrying=" .. stateValue(state, "IsCarrying"))
    end
end

local function resolveEggState()
    local client = ReplicatedStorage:FindFirstChild("Client")
    local eggScript = client and client:FindFirstChild("EggState")
    if eggScript and eggScript:IsA("ModuleScript") then
        local ok, value = pcall(require, eggScript)
        if ok then
            eggModule = value
        else
            warn("[Carry Runback Probe] EggState require failed: " .. clip(value, 240))
            terminalLine("ERROR", "EggState require failed: " .. clip(value, 180))
        end
    else
        warn("[Carry Runback Probe] ReplicatedStorage.Client.EggState not found")
        terminalLine("ERROR", "ReplicatedStorage.Client.EggState not found")
    end
    return eggScript
end

local function connectCarrySignal()
    if type(eggModule) ~= "table" then
        return
    end
    local signal = eggModule.CarryChanged
    local connect
    local ok = pcall(function()
        connect = signal and signal.Connect
    end)
    if ok and type(connect) == "function" then
        local connected, result = pcall(function()
            return connect(signal, function(...)
                if session.Active then
                    applyCarryState(snapshotCarry(...), "CarryChanged")
                end
            end)
        end)
        if connected then
            carrySignalConnection = result
            session.CarryConnection = result
        else
            warn("[Carry Runback Probe] CarryChanged connection failed: " .. clip(result, 200))
            terminalLine("ERROR", "CarryChanged connection failed: " .. clip(result, 180))
        end
    end
end

local guiOk, guiProblem = pcall(createGui)
if not guiOk then
    warn("[Carry Runback Probe] GUI creation failed: " .. clip(guiProblem, 240))
end
terminalLine("PROBE", "Probe ACTIVE")
terminalLine("PROBE", "Waiting for carry")
updateStatus()

local eggScript = resolveEggState()
connectCarrySignal()
applyCarryState(snapshotCarry(), "INITIAL")

task.spawn(function()
    inspectSources(eggScript)
end)

local commandTarget = sessionEnvironment
commandTarget.MemoryToolsCarryRunbackReport = function()
    return publishReport("MANUAL_COMMAND")
end

if generateButton then
    rememberConnection(generateButton.Activated:Connect(function()
        if session.Active then
            publishReport("GUI_MANUAL")
        end
    end))
end
if copyButton then
    rememberConnection(copyButton.Activated:Connect(function()
        if session.Active then
            copyLatestReport()
        end
    end))
end

print("[Carry Runback Probe] ACTIVE. Manual report command: MemoryToolsCarryRunbackReport()")

task.spawn(function()
    while session.Active do
        task.wait(0.025)
        if not session.Active then
            break
        end
        local now = os.clock()
        if now - lastSampleAt >= SAMPLE_INTERVAL then
            lastSampleAt = now
            local state = snapshotCarry()
            if state.IsCarrying ~= carrying then
                applyCarryState(state, "POLL_TRANSITION")
            else
                carryState = state
            end
            if carrying then
                takeSample(now)
                if carryStartedAt and now - carryStartedAt >= CARRY_TIMEOUT and not reportGenerated then
                    terminalLine("CARRY", "30-second timeout reached")
                    publishReport("30_SECOND_CARRY_TIMEOUT")
