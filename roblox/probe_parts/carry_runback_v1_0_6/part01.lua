-- MemoryTools Carry Runback Probe V1.0.6.
-- Passive client-visible source inspection and runtime telemetry only.

local VERSION = "1.0.6"
local SAMPLE_INTERVAL = 0.1
local SAMPLE_WINDOW = 15
local DROP_WINDOW = 5
local CARRY_TIMEOUT = 30
local DISCOVERY_INTERVAL = 1
local SEARCH_RADIUS = 250
local MAX_SCRIPTS_SCANNED = 5000
local MAX_RELEVANT_SCRIPTS = 15
local MAX_CANDIDATES = 40
local MAX_REPORT_BYTES = 24 * 1024

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local Workspace = game:GetService("Workspace")
local CollectionService = game:GetService("CollectionService")

local LocalPlayer = Players.LocalPlayer
local startedAt = os.clock()
local character
local humanoid
local rootPart
local healthConnection
local eggModule
local carrySignalConnection
local carryState = {}
local observedCarryState = {}
local carrying = false
local carryStartedAt
local carryLost = false
local healthDropped = false
local carryInitialHealth
local lastSampleAt = -math.huge
local lastDiscoveryAt = -math.huge
local reportGenerated = false
local samples = {}
local carryEvents = {}
local candidates = {}
local candidateOrder = {}
local dropSnapshot = nil
local latestReport = nil
local publishReport
local copyLatestReport
local mainFrame
local miniButton
local terminalScroll
local terminalText
local statusText
local feedbackText
local minimizeButton
local generateButton
local copyButton
local terminalBuffer = ""
local visualState = {
    Probe = "ACTIVE",
    Carry = "WAITING",
    Sources = "SCANNING",
    Report = "WAITING",
}

local sourceState = {
    Status = "NOT STARTED",
    Scanned = 0,
    Failures = 0,
    Egg = {},
    Speed = {},
    Guard = {},
}

local function clip(value, maximum)
    local text = tostring(value)
    if #text <= maximum then
        return text
    end
    return text:sub(1, math.max(0, maximum - 3)) .. "..."
end

local function fmtNumber(value)
    if type(value) ~= "number" then
        return "?"
    end
    return string.format("%.3f", value)
end

local function fmtVector(value)
    if typeof(value) ~= "Vector3" then
        return "(?,?,?)"
    end
    return string.format("(%.2f,%.2f,%.2f)", value.X, value.Y, value.Z)
end

local function safeFullName(instance)
    local ok, value = pcall(function()
        return instance:GetFullName()
    end)
    return ok and clip(value, 300) or "<unavailable>"
end

local function horizontalMagnitude(value)
    return Vector3.new(value.X, 0, value.Z).Magnitude
end

local function globalFunction(name)
    local ok, value = pcall(function()
        return getfenv()[name]
    end)
    return ok and type(value) == "function" and value or nil
end

local sessionEnvironment = _G
local getEnvironment = globalFunction("getgenv")
if getEnvironment then
    local ok, value = pcall(getEnvironment)
    if ok and type(value) == "table" then
        sessionEnvironment = value
    end
end

local previousSession = sessionEnvironment.__MEMORYTOOLS_CARRY_RUNBACK_PROBE
if type(previousSession) == "table" then
    previousSession.Active = false
    local knownConnections = {
        previousSession.CarryConnection,
        previousSession.HealthConnection,
    }
    if type(previousSession.Connections) == "table" then
        for _, connection in ipairs(previousSession.Connections) do
            table.insert(knownConnections, connection)
        end
    end
    for _, connection in ipairs(knownConnections) do
        if connection then
            pcall(function()
                connection:Disconnect()
            end)
        end
    end
    if previousSession.Gui then
        pcall(function()
            previousSession.Gui.Enabled = false
        end)
    end
end

local session = {
    Active = true,
    Gui = nil,
    CarryConnection = nil,
    HealthConnection = nil,
    Connections = {},
}
sessionEnvironment.__MEMORYTOOLS_CARRY_RUNBACK_PROBE = session

local function rememberConnection(connection)
    if connection then
        table.insert(session.Connections, connection)
    end
    return connection
end

local function updateStatus()
    if not statusText then
        return
    end
    local details = {}
    for _, pair in ipairs({
        { "AreaId", observedCarryState.AreaId },
        { "SpeedMultiplier", observedCarryState.SpeedMultiplier },
        { "WakeDelay", observedCarryState.RunBackWakeDelayRequired },
        { "GuardDisabled", observedCarryState.GuardDisabled },
    }) do
        if pair[2] ~= nil then
            table.insert(details, pair[1] .. ": " .. tostring(pair[2]))
        end
    end
    statusText.Text = string.format(
        "Probe: %s    Carry: %s\nSources: %s    Report: %s\n%s",
        visualState.Probe,
        visualState.Carry,
        visualState.Sources,
        visualState.Report,
        #details > 0 and table.concat(details, "    ") or "Waiting for carry telemetry"
    )
end

local function updateTerminalCanvas(scrollToTop)
    if not terminalScroll or not terminalText then
        return
    end
    task.defer(function()
        if not session.Active or not terminalScroll.Parent or not terminalText.Parent then
            return
        end
        local bounds = terminalText.TextBounds
        local width = math.max(terminalScroll.AbsoluteSize.X - 16, bounds.X + 20)
        local height = math.max(terminalScroll.AbsoluteSize.Y - 16, bounds.Y + 20)
        terminalText.Size = UDim2.fromOffset(width, height)
        terminalScroll.CanvasSize = UDim2.fromOffset(width, height)
        if scrollToTop then
            terminalScroll.CanvasPosition = Vector2.zero
        end
    end)
end

local function terminalSet(text)
    terminalBuffer = tostring(text)
    if terminalText then
        terminalText.Text = terminalBuffer
        updateTerminalCanvas(true)
    end
end

local function terminalAppend(text)
    text = tostring(text)
    if terminalBuffer == "" then
        terminalBuffer = text
    else
        terminalBuffer = terminalBuffer .. "\n" .. text
