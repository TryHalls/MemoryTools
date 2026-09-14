-- MemoryTools Flight Death Probe V1.0.4.2.
-- Passive runtime telemetry only. This script does not alter character gameplay.

local VERSION = "1.0.4.2"
local SAMPLE_INTERVAL = 0.05
local SAMPLE_WINDOW = 10
local MAX_TOUCHES = 30
local MAX_HEALTH_CHANGES = 40
local MAX_REPORT_BYTES = 24 * 1024

local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")
local RunService = game:GetService("RunService")

local LocalPlayer = Players.LocalPlayer

local samples = {}
local healthHistory = {}
local touches = {}
local touchByPart = {}
local movementEvents = {}
local characterConnections = {}
local connectedParts = {}

local activeCharacter = nil
local activeHumanoid = nil
local activeRoot = nil
local previousSample = nil
local lastSampleTime = -math.huge
local deathHandled = false
local lastDeathReport = nil
local statusLabel = nil

local counters = {
    PossibleLagbacks = 0,
    HLimit = 0,
    VLimit = 0,
    ExtremeH = 0,
    ExtremeV = 0,
}

local minimumRootY = math.huge

local function disconnectCharacterConnections()
    for _, connection in ipairs(characterConnections) do
        connection:Disconnect()
    end
    table.clear(characterConnections)
    table.clear(connectedParts)
end

local function clip(text, maximum)
    text = tostring(text)
    if #text <= maximum then
        return text
    end
    return text:sub(1, math.max(0, maximum - 3)) .. "..."
end

local function fmt(number)
    if typeof(number) ~= "number" then
        return "?"
    end
    return string.format("%.2f", number)
end

local function fmtVector(vector)
    if typeof(vector) ~= "Vector3" then
        return "(?, ?, ?)"
    end
    return string.format("(%.2f,%.2f,%.2f)", vector.X, vector.Y, vector.Z)
end

local function safeFullName(instance)
    local ok, value = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(value, 180) or clip(tostring(instance), 180)
end

local function readWorkspaceTelemetry()
    local fallenHeight = Workspace.FallenPartsDestroyHeight
    local clientAntiTp = Workspace:GetAttribute("ClientObbyAntiTp")
    local suspendedRegion = Workspace:GetAttribute("AnticheatSuspendedRegion")
    return fallenHeight, clientAntiTp, suspendedRegion
end

local function setGuiText(text)
    if statusLabel and statusLabel.Parent then
        statusLabel.Text = text
    end
end

local function ensureGui(text)
    local ok, err = pcall(function()
        local playerGui = LocalPlayer:FindFirstChildOfClass("PlayerGui")
            or LocalPlayer:WaitForChild("PlayerGui", 5)
        if not playerGui then
            error("PlayerGui unavailable")
        end

        local gui = playerGui:FindFirstChild("MemoryToolsFlightDeathProbe")
        if not gui then
            gui = Instance.new("ScreenGui")
            gui.Name = "MemoryToolsFlightDeathProbe"
            gui.ResetOnSpawn = false
            gui.DisplayOrder = 1000000
            gui.IgnoreGuiInset = false
            gui.Parent = playerGui
        end

        local label = gui:FindFirstChild("Status")
        if not label then
            label = Instance.new("TextLabel")
            label.Name = "Status"
            label.AnchorPoint = Vector2.new(0.5, 0)
            label.Position = UDim2.fromScale(0.5, 0.02)
            label.Size = UDim2.fromOffset(300, 30)
            label.BackgroundColor3 = Color3.fromRGB(20, 20, 20)
            label.BackgroundTransparency = 0.2
            label.BorderSizePixel = 0
            label.TextColor3 = Color3.fromRGB(110, 255, 140)
            label.TextSize = 14
            label.Font = Enum.Font.Code
            label.Parent = gui
        end

        statusLabel = label
        statusLabel.Text = text
        statusLabel.Visible = true
    end)

    if not ok then
        statusLabel = nil
        warn("[MemoryTools Flight Death Probe] GUI unavailable: " .. tostring(err))
    end
end

local function appendBounded(list, value, maximum)
    table.insert(list, value)
    while #list > maximum do
        table.remove(list, 1)
    end
end

local function recordMovementEvent(timestamp, kind)
    table.insert(movementEvents, {
        Time = timestamp,
        Kind = kind,
    })
    while #movementEvents > 0 and timestamp - movementEvents[1].Time > SAMPLE_WINDOW do
        table.remove(movementEvents, 1)
    end
end

local function horizontalMagnitude(vector)
    return Vector3.new(vector.X, 0, vector.Z).Magnitude
end

local function calculateVerticalAllowance(humanoid, velocityY, checkDt)
    local jumpVelocity
    if humanoid.UseJumpPower then
        jumpVelocity = math.max(humanoid.JumpPower, 0)
    else
        jumpVelocity = math.sqrt(
            math.max(Workspace.Gravity, 0)
            * 2
            * math.max(humanoid.JumpHeight, 0)
        )
    end
    local positiveVelocityY = math.clamp(velocityY, 0, 400)
    return math.max(jumpVelocity, positiveVelocityY) * checkDt + 14 + 60
end

local function takeSample(now)
    local character = activeCharacter
    local humanoid = activeHumanoid
    local root = activeRoot
    if not character or not humanoid or not root then
        return
    end
    if character.Parent == nil or humanoid.Parent == nil or root.Parent == nil then
        return
    end

    local position = root.Position
    local velocity = root.AssemblyLinearVelocity
    local fallenHeight, clientAntiTp, suspendedRegion = readWorkspaceTelemetry()
    local stateName = humanoid:GetState().Name

    local sample = {
        Time = now,
        Position = position,
        Velocity = velocity,
        Health = humanoid.Health,
        WalkSpeed = humanoid.WalkSpeed,
        State = stateName,
        Sit = humanoid.Sit,
        PlatformStand = humanoid.PlatformStand,
        FallenHeight = fallenHeight,
        ClientAntiTp = clientAntiTp,
        SuspendedRegion = suspendedRegion,
        Dt = 0,
        DeltaPosition = Vector3.zero,
        HorizontalDistance = 0,
        VerticalUp = 0,
        TotalDistance = 0,
        PreviousHorizontalVelocity = horizontalMagnitude(velocity),
        CurrentHorizontalVelocity = horizontalMagnitude(velocity),
        HorizontalAllowed = 0,
        VerticalAllowed = 0,
        Flags = {},
    }

    minimumRootY = math.min(minimumRootY, position.Y)

    if previousSample then
        local dt = math.max(now - previousSample.Time, 0)
        local checkDt = math.min(dt, 0.4)
        local displacement = position - previousSample.Position
        local horizontalDistance = horizontalMagnitude(displacement)
        local verticalUp = math.max(displacement.Y, 0)
        local totalDistance = displacement.Magnitude
        local previousHorizontalVelocity = horizontalMagnitude(previousSample.Velocity)
        local currentHorizontalVelocity = horizontalMagnitude(velocity)
        local horizontalVelocity = math.max(previousHorizontalVelocity, currentHorizontalVelocity)
        local horizontalAllowed = math.clamp(humanoid.WalkSpeed, 2, 1000) * 1.7 * checkDt
            + 14
            + math.min(horizontalVelocity, 400) * checkDt
            + 80
        local verticalAllowed = calculateVerticalAllowance(humanoid, velocity.Y, checkDt)

        sample.Dt = dt
        sample.DeltaPosition = displacement
        sample.HorizontalDistance = horizontalDistance
        sample.VerticalUp = verticalUp
        sample.TotalDistance = totalDistance
        sample.PreviousHorizontalVelocity = previousHorizontalVelocity
        sample.CurrentHorizontalVelocity = currentHorizontalVelocity
        sample.HorizontalAllowed = horizontalAllowed
        sample.VerticalAllowed = verticalAllowed

        local hLimit = horizontalDistance > horizontalAllowed
        local vLimit = verticalUp > verticalAllowed
        local extremeH = horizontalDistance >= 300
        local extremeV = verticalUp >= 150

        if hLimit then
            table.insert(sample.Flags, "H_LIMIT")
            counters.HLimit = counters.HLimit + 1
            recordMovementEvent(now, "H_LIMIT")
        end
        if vLimit then
            table.insert(sample.Flags, "V_LIMIT")
            counters.VLimit = counters.VLimit + 1
            recordMovementEvent(now, "V_LIMIT")
        end
        if extremeH then
            table.insert(sample.Flags, "EXTREME_H")
            counters.ExtremeH = counters.ExtremeH + 1
        end
        if extremeV then
            table.insert(sample.Flags, "EXTREME_V")
            counters.ExtremeV = counters.ExtremeV + 1
        end

        local previousSpeed = previousSample.Velocity.Magnitude
        local oppositeDirection = false
        if totalDistance > 20 and previousSpeed > 1 then
            oppositeDirection = displacement.Unit:Dot(previousSample.Velocity.Unit) < -0.7
        end
        local abruptWithoutCompatibleVelocity = dt <= 0.2
            and totalDistance > 25
            and totalDistance > math.max(25, previousSpeed * dt * 1.75 + 10)
        local possibleLagback = oppositeDirection or hLimit or abruptWithoutCompatibleVelocity

        if possibleLagback then
            table.insert(sample.Flags, "POSSIBLE_LAGBACK")
            counters.PossibleLagbacks = counters.PossibleLagbacks + 1
            recordMovementEvent(now, "POSSIBLE_LAGBACK")
        end
    end

    table.insert(samples, sample)
    while #samples > 0 and now - samples[1].Time > SAMPLE_WINDOW do
        table.remove(samples, 1)
    end
    previousSample = sample
end

local function recordTouch(part)
    local now = os.clock()
    local existing = touchByPart[part]
    if existing then
        for index, item in ipairs(touches) do
            if item == existing then
                table.remove(touches, index)
                break
            end
        end
    end

    local entry = {
        Time = now,
        Part = part,
        FullName = safeFullName(part),
        ClassName = part.ClassName,
        Position = part.Position,
        Collidable = part.CanCollide,
        Touchable = part.CanTouch,
        Transparency = part.Transparency,
    }
    touchByPart[part] = entry
    table.insert(touches, entry)

    while #touches > MAX_TOUCHES do
        local removed = table.remove(touches, 1)
        if removed and touchByPart[removed.Part] == removed then
            touchByPart[removed.Part] = nil
        end
    end
end

local function connectCharacterPart(part)
    if not part:IsA("BasePart") or connectedParts[part] then
        return
    end
    connectedParts[part] = true
    table.insert(characterConnections, part.Touched:Connect(recordTouch))
end

local function relevantHealthTransition(deathTime)
    for index = #healthHistory, 1, -1 do
        local item = healthHistory[index]
        if item.Time <= deathTime + 0.05 then
            return item
        end
    end
    return nil
end

local function eventsWithin(deathTime, seconds, kind)
    local count = 0
    for _, event in ipairs(movementEvents) do
        local age = deathTime - event.Time
        if age >= 0 and age <= seconds and (not kind or event.Kind == kind) then
            count = count + 1
        end
    end
    return count
end

local function buildSampleLine(sample, deathTime)
    local flags = #sample.Flags > 0 and table.concat(sample.Flags, ",") or "-"
    return string.format(
        "%s | %s | %s | %s | %s | %s | %s | %s | %s | %s",
        fmt(sample.Time - deathTime),
        fmtVector(sample.Position),
        fmtVector(sample.Velocity),
        fmt(sample.Health),
        fmt(sample.Dt),
        fmt(sample.HorizontalDistance),
        fmt(sample.VerticalUp),
        fmt(sample.HorizontalAllowed),
        fmt(sample.VerticalAllowed),
        flags
    )
end

local function buildReport(deathTime)
    local lastSample = samples[#samples]
    local deathPosition = activeRoot and activeRoot.Parent and activeRoot.Position
        or (lastSample and lastSample.Position)
        or Vector3.zero
    local fallenHeight = Workspace.FallenPartsDestroyHeight
    local healthTransition = relevantHealthTransition(deathTime)
    local transitionText = "none observed"
    if healthTransition then
        transitionText = fmt(healthTransition.OldHealth) .. " -> " .. fmt(healthTransition.NewHealth)
    end

    local recentTouches = {}
    for _, touch in ipairs(touches) do
        local age = deathTime - touch.Time
        if age >= 0 and age <= 0.5 then
            table.insert(recentTouches, touch)
        end
    end

    local repeatedLagbacks = eventsWithin(deathTime, 5, "POSSIBLE_LAGBACK") >= 3
    local sixFlags = eventsWithin(deathTime, 5, nil) >= 6
    local extremeMovement = counters.ExtremeH > 0 or counters.ExtremeV > 0
    local voidPattern = minimumRootY <= fallenHeight + 10 or deathPosition.Y <= fallenHeight + 10
    local instantHealthZero = healthTransition ~= nil
        and healthTransition.OldHealth > 0
        and healthTransition.NewHealth <= 0

    local prefix = {
        "MEMORYTOOLS FLIGHT DEATH PROBE V1.0.4.2",
        "",
        "[SUMMARY]",
        "PlaceId: " .. tostring(game.PlaceId),
        "PlaceVersion: " .. tostring(game.PlaceVersion),
        "Death time: " .. fmt(deathTime),
        "Death position: " .. fmtVector(deathPosition),
        "FallenPartsDestroyHeight: " .. fmt(fallenHeight),
        "Distance above destroy height: " .. fmt(deathPosition.Y - fallenHeight),
        "Number of possible lagbacks: " .. tostring(counters.PossibleLagbacks),
        "H_LIMIT count: " .. tostring(counters.HLimit),
        "V_LIMIT count: " .. tostring(counters.VLimit),
        "EXTREME_H count: " .. tostring(counters.ExtremeH),
        "EXTREME_V count: " .. tostring(counters.ExtremeV),
        "Health transition before death: " .. transitionText,
        "",
        "[HEALTH HISTORY]",
    }

    if #healthHistory == 0 then
        table.insert(prefix, "none observed")
    else
        for _, item in ipairs(healthHistory) do
            table.insert(prefix, string.format(
                "%s | %s -> %s",
                fmt(item.Time - deathTime),
                fmt(item.OldHealth),
                fmt(item.NewHealth)
            ))
        end
    end

    table.insert(prefix, "")
    table.insert(prefix, "[LAST 10 SECONDS]")
    table.insert(prefix, "time | pos | vel | hp | dt | movedXZ | up | hAllowed | vAllowed | flags")

    local suffix = {
        "",
        "[RECENT TOUCHES]",
    }
    if #touches == 0 then
        table.insert(suffix, "none observed")
    else
        for _, touch in ipairs(touches) do
            local age = deathTime - touch.Time
            local label = age >= 0 and age <= 0.5 and "RECENT_TOUCH" or "TOUCH"
            table.insert(suffix, string.format(
                "%s | %s | %s | %s | pos=%s | collide=%s | touch=%s | transparency=%s",
                fmt(touch.Time - deathTime),
                label,
                touch.FullName,
                touch.ClassName,
                fmtVector(touch.Position),
                tostring(touch.Collidable),
                tostring(touch.Touchable),
                fmt(touch.Transparency)
            ))
        end
    end

    table.insert(suffix, "")
    table.insert(suffix, "[EVENT FLAGS]")
    local observationalFlags = {}
    if repeatedLagbacks then table.insert(observationalFlags, "REPEATED_LAGBACK_PATTERN") end
    if sixFlags then table.insert(observationalFlags, "SIX_FLAGS_PATTERN") end
    if extremeMovement then table.insert(observationalFlags, "EXTREME_MOVEMENT_PATTERN") end
    if #recentTouches > 0 then table.insert(observationalFlags, "RECENT_HAZARD_TOUCH") end
    if voidPattern then table.insert(observationalFlags, "VOID_PATTERN") end
    if instantHealthZero then table.insert(observationalFlags, "INSTANT_HEALTH_ZERO") end
    if #observationalFlags == 0 then
        table.insert(suffix, "none observed")
    else
        for _, flag in ipairs(observationalFlags) do
            table.insert(suffix, flag)
        end
    end
    table.insert(suffix, "")
    table.insert(suffix, "Observational telemetry only; no cause is automatically confirmed.")

    local prefixText = table.concat(prefix, "\n")
    local suffixText = table.concat(suffix, "\n")
    local sampleBudget = MAX_REPORT_BYTES - #prefixText - #suffixText - 4
    local selected = {}
    local selectedBytes = 0
    local omitted = 0

    for index = #samples, 1, -1 do
        local line = buildSampleLine(samples[index], deathTime)
        if selectedBytes + #line + 1 <= sampleBudget then
            table.insert(selected, 1, line)
            selectedBytes = selectedBytes + #line + 1
        else
            omitted = index
            break
        end
    end
    if omitted > 0 then
        local marker = "[" .. tostring(omitted) .. " older samples omitted to keep report <= 24 KB]"
        while #selected > 0 and selectedBytes + #marker + 1 > sampleBudget do
            local removed = table.remove(selected, 1)
            selectedBytes = selectedBytes - #removed - 1
            omitted = omitted + 1
            marker = "[" .. tostring(omitted) .. " older samples omitted to keep report <= 24 KB]"
        end
        table.insert(selected, 1, marker)
    elseif #selected == 0 then
        table.insert(selected, "none captured")
    end

    local report = prefixText .. "\n" .. table.concat(selected, "\n") .. "\n" .. suffixText
    if #report > MAX_REPORT_BYTES then
        report = report:sub(1, MAX_REPORT_BYTES - 24) .. "\n[REPORT SIZE LIMITED]"
    end
    return report
end

local function handleDeath()
    if deathHandled then
        return
    end
    deathHandled = true

    local deathTime = os.clock()
    if deathTime - lastSampleTime >= 0.001 then
        takeSample(deathTime)
    end

    local report = buildReport(deathTime)
    lastDeathReport = report
    print(report)

    local copied = false
    if type(setclipboard) == "function" then
        copied = pcall(function()
            setclipboard(report)
        end)
    end

    setGuiText("DEATH CAPTURED — REPORT COPIED")
    if not copied then
        warn("[MemoryTools Flight Death Probe] setclipboard unavailable; report was printed")
    end
end

local function resetCharacterTelemetry()
    table.clear(samples)
    table.clear(healthHistory)
    table.clear(touches)
    table.clear(touchByPart)
    table.clear(movementEvents)
    previousSample = nil
    lastSampleTime = -math.huge
    deathHandled = false
    minimumRootY = math.huge
    counters.PossibleLagbacks = 0
    counters.HLimit = 0
    counters.VLimit = 0
    counters.ExtremeH = 0
    counters.ExtremeV = 0
end

local function attachCharacter(character)
    disconnectCharacterConnections()
    resetCharacterTelemetry()
    activeCharacter = character
    activeHumanoid = character:WaitForChild("Humanoid", 10)
    activeRoot = character:WaitForChild("HumanoidRootPart", 10)

    ensureGui("FLIGHT DEATH PROBE: ACTIVE")

    if not activeHumanoid or not activeRoot then
        warn("[MemoryTools Flight Death Probe] Character is missing Humanoid or HumanoidRootPart")
        return
    end

    local observedHealth = activeHumanoid.Health
    table.insert(characterConnections, activeHumanoid.HealthChanged:Connect(function(newHealth)
        local now = os.clock()
        appendBounded(healthHistory, {
            Time = now,
            OldHealth = observedHealth,
            NewHealth = newHealth,
        }, MAX_HEALTH_CHANGES)
        observedHealth = newHealth
    end))
    table.insert(characterConnections, activeHumanoid.Died:Connect(handleDeath))
    table.insert(characterConnections, character.DescendantAdded:Connect(connectCharacterPart))

    for _, descendant in ipairs(character:GetDescendants()) do
        connectCharacterPart(descendant)
    end

    local now = os.clock()
    takeSample(now)
    lastSampleTime = now
end

print("[MemoryTools Flight Death Probe] ACTIVE")
ensureGui("FLIGHT DEATH PROBE: ACTIVE")

LocalPlayer.CharacterAdded:Connect(attachCharacter)
if LocalPlayer.Character then
    task.spawn(attachCharacter, LocalPlayer.Character)
end

RunService.Heartbeat:Connect(function()
    local now = os.clock()
    if now - lastSampleTime >= SAMPLE_INTERVAL then
        lastSampleTime = now
        takeSample(now)
    end
end)
