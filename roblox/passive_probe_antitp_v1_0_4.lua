-- MemoryTools ObbyAntiTP passive probe V1.0.4.
-- Exactly one LocalScript is decompiled. Nothing is required, hooked, fired,
-- invoked, enumerated through getgc/getconnections, or changed in gameplay.

local VERSION = "1.0.4"
local MAX_REPORT_BYTES = 18 * 1024
local CONTEXT_RADIUS = 12
local FIRST_REQUESTED_LINE = 140
local LAST_REQUESTED_LINE = 339

local Players = game:GetService("Players")

local result = {
    Version = VERSION,
    Target = "Players.LocalPlayer.PlayerScripts.Game.ObbyAntiTPClient",
    Report = "",
    Warnings = {},
}

local function warn(message)
    table.insert(result.Warnings, tostring(message))
end

local function resolveTarget()
    local player = Players.LocalPlayer
    local playerScripts = player and player:FindFirstChild("PlayerScripts")
    local gameFolder = playerScripts and playerScripts:FindFirstChild("Game")
    return gameFolder and gameFolder:FindFirstChild("ObbyAntiTPClient") or nil
end

local function splitLines(source)
    source = source:gsub("\r\n", "\n"):gsub("\r", "\n")
    local lines = {}
    if source:sub(-1) ~= "\n" then source = source .. "\n" end
    for line in source:gmatch("(.-)\n") do
        table.insert(lines, line)
    end
    return lines
end

local function numberedRange(lines, first, last)
    local output = {}
    for index = first, last do
        table.insert(output, string.format("%5d | %s", index, lines[index]))
    end
    return table.concat(output, "\n")
end

local function literalRange(lines, first, last)
    local output = {}
    for index = first, last do
        table.insert(output, lines[index])
    end
    return table.concat(output, "\n")
end

-- Remove comments and quoted contents while preserving newlines. This lets the
-- block matcher ignore keywords which only occur inside strings/comments.
local function structuralSource(source)
    local output = table.create(#source)
    local index = 1
    local mode = "code"
    local quote = nil
    local longEquals = nil

    while index <= #source do
        local character = source:sub(index, index)
        local pair = source:sub(index, index + 1)

        if mode == "code" then
            local equals = source:match("^%[(=*)%[", index)
            if pair == "--" then
                local commentEquals = source:match("^%-%-%[(=*)%[", index)
                if commentEquals then
                    mode = "long"
                    longEquals = commentEquals
                    table.insert(output, " ")
                    index = index + 4 + #commentEquals
                else
                    mode = "line"
                    table.insert(output, " ")
                    index = index + 2
                end
            elseif character == "\"" or character == "'" then
                mode = "quote"
                quote = character
                table.insert(output, " ")
                index = index + 1
            elseif equals ~= nil then
                mode = "long"
                longEquals = equals
                table.insert(output, " ")
                index = index + 2 + #equals
            else
                table.insert(output, character)
                index = index + 1
            end
        elseif mode == "line" then
            if character == "\n" then
                mode = "code"
                table.insert(output, "\n")
            else
                table.insert(output, " ")
            end
            index = index + 1
        elseif mode == "quote" then
            if character == "\\" then
                table.insert(output, "  ")
                index = index + 2
            else
                if character == quote then mode = "code" end
                table.insert(output, character == "\n" and "\n" or " ")
                index = index + 1
            end
        else
            local closer = "]" .. longEquals .. "]"
            if source:sub(index, index + #closer - 1) == closer then
                mode = "code"
                table.insert(output, " ")
                index = index + #closer
            else
                table.insert(output, character == "\n" and "\n" or " ")
                index = index + 1
            end
        end
    end

    return table.concat(output)
end

local function functionRanges(source)
    local clean = structuralSource(source)
    local stack = {}
    local functions = {}
    local line = 1

    for token, newlines in clean:gmatch("([%a_][%w_]*)([^%a_%w]*)") do
        local lower = token:lower()
        if lower == "function" then
            local node = { Kind = "function", First = line }
            table.insert(stack, node)
        elseif lower == "if" or lower == "repeat" then
            table.insert(stack, { Kind = lower, First = line })
        elseif lower == "for" or lower == "while" then
            table.insert(stack, { Kind = lower, First = line, AwaitingDo = true })
        elseif lower == "do" then
            local top = stack[#stack]
            if top and top.AwaitingDo then
                top.AwaitingDo = false
            else
                table.insert(stack, { Kind = "do", First = line })
            end
        elseif lower == "end" then
            local node = table.remove(stack)
            if node and node.Kind == "function" then
                node.Last = line
                table.insert(functions, node)
            end
        elseif lower == "until" then
            local top = stack[#stack]
            if top and top.Kind == "repeat" then table.remove(stack) end
        end
        local _, count = newlines:gsub("\n", "")
        line = line + count
    end

    table.sort(functions, function(left, right)
        if left.First == right.First then return left.Last < right.Last end
        return left.First < right.First
    end)
    return functions
end

local function enclosingFunction(ranges, line)
    local best = nil
    for _, range in ipairs(ranges) do
        if range.First <= line and line <= range.Last then
            if not best or (range.Last - range.First) < (best.Last - best.First) then
                best = range
            end
        end
    end
    return best
end

local MARKERS = {
    "Heartbeat", "RenderStepped", "Stepped", "task.wait", "os.clock",
    "Magnitude", "v_u_15", "v_u_16", "v_u_17", "v_u_18", "v_u_19",
    "v_u_20", "v_u_21", "lagback", "punish",
}

local function markerHits(lines)
    local hits = {}
    for index, line in ipairs(lines) do
        local found = {}
        for _, marker in ipairs(MARKERS) do
            if line:find(marker, 1, true) then table.insert(found, marker) end
        end
        if #found > 0 then
            table.insert(hits, { Line = index, Markers = found })
        end
    end
    return hits
end

local function contextWindows(lines, hits)
    local windows = {}
    for _, hit in ipairs(hits) do
        local first = math.max(1, hit.Line - CONTEXT_RADIUS)
        local last = math.min(#lines, hit.Line + CONTEXT_RADIUS)
        local previous = windows[#windows]
        if previous and first <= previous.Last + 1 then
            previous.Last = math.max(previous.Last, last)
            table.insert(previous.Hits, hit)
        else
            table.insert(windows, { First = first, Last = last, Hits = { hit } })
        end
    end
    return windows
end

local function hasWord(text, word)
    local pattern = "%f[%w_]" .. word .. "%f[^%w_]"
    return text:lower():find(pattern) ~= nil
end

local function candidateFunctions(lines, ranges, hits)
    local selected = {}
    local reasons = {}

    local function selectRange(range, reason)
        if not range then return end
        local key = tostring(range.First) .. ":" .. tostring(range.Last)
        selected[key] = range
        reasons[key] = reasons[key] or {}
        table.insert(reasons[key], reason)
    end

    for _, hit in ipairs(hits) do
        for _, marker in ipairs(hit.Markers) do
            local lower = marker:lower()
            if lower == "lagback" or lower == "punish"
                or marker == "Heartbeat" or marker == "RenderStepped" or marker == "Stepped" then
                selectRange(enclosingFunction(ranges, hit.Line), marker .. "@" .. tostring(hit.Line))
            end
        end
    end

    for _, range in ipairs(ranges) do
        local body = table.concat(lines, "\n", range.First, range.Last)
        local lower = body:lower()
        if hasWord(body, "moved") and hasWord(body, "up") then
            selectRange(range, "moved/up calculation")
        end
        local frameDriven = lower:find("heartbeat", 1, true)
            or lower:find("renderstepped", 1, true)
            or lower:find("stepped", 1, true)
        local punitive = lower:find("punish", 1, true) or lower:find("lagback", 1, true)
        if frameDriven and punitive then
            selectRange(range, "per-frame punishment decision")
        end
    end

    local output = {}
    for key, range in pairs(selected) do
        table.insert(output, { Range = range, Reasons = reasons[key] })
    end
    table.sort(output, function(left, right)
        if left.Range.First == right.Range.First then return left.Range.Last < right.Range.Last end
        return left.Range.First < right.Range.First
    end)
    return output
end

local parts = {}
local bytes = 0
local omitted = {}

local function append(text, label, mandatory)
    local separator = #parts == 0 and "" or "\n\n"
    if bytes + #separator + #text <= MAX_REPORT_BYTES then
        table.insert(parts, text)
        bytes = bytes + #separator + #text
        return true
    end
    table.insert(omitted, label)
    if mandatory then warn(label .. " did not fit inside the absolute 18 KB cap") end
    return false
end

local target = resolveTarget()
local source = nil
if not target then
    warn(result.Target .. " was not found")
elseif not target:IsA("LocalScript") then
    warn(result.Target .. " exists but is not a LocalScript")
elseif type(decompile) ~= "function" then
    warn("decompile is unavailable in this executor")
else
    local ok, value = pcall(decompile, target)
    if ok and type(value) == "string" and value ~= "" then
        source = value
    else
        warn("decompile failed: " .. tostring(value or "empty source"))
    end
end

append(table.concat({
    "MEMORYTOOLS PASSIVE ANTITP PROBE V" .. VERSION,
    "Target: " .. result.Target,
    "PlaceId: " .. tostring(game.PlaceId),
    "PlaceVersion: " .. tostring(game.PlaceVersion),
}, "\n"), "report header", true)

if source then
    local lines = splitLines(source)
    local requestedLast = math.min(LAST_REQUESTED_LINE, #lines)
    if #lines < LAST_REQUESTED_LINE then
        warn("decompiled source has only " .. tostring(#lines) .. " lines; requested line 339 is unavailable")
    end

    if requestedLast >= FIRST_REQUESTED_LINE then
        append("[LITERAL DECOMPILED SOURCE LINES 140-" .. tostring(requestedLast) .. "]\n"
            .. literalRange(lines, FIRST_REQUESTED_LINE, requestedLast), "literal lines 140-339", true)
    else
        warn("decompiled source ends before requested line 140")
    end

    local hits = markerHits(lines)
    local hitIndex = { "[MARKER HIT INDEX]" }
    if #hits == 0 then
        table.insert(hitIndex, "<no requested markers found>")
    else
        for _, hit in ipairs(hits) do
            table.insert(hitIndex, tostring(hit.Line) .. ": " .. table.concat(hit.Markers, ", "))
        end
    end
    append(table.concat(hitIndex, "\n"), "marker hit index", true)

    for _, window in ipairs(contextWindows(lines, hits)) do
        local coveredByLiteral = window.First >= FIRST_REQUESTED_LINE and window.Last <= requestedLast
        if not coveredByLiteral then
            local labels = {}
            for _, hit in ipairs(window.Hits) do
                table.insert(labels, table.concat(hit.Markers, "+") .. "@" .. tostring(hit.Line))
            end
            append("[MARKER CONTEXT +/-" .. tostring(CONTEXT_RADIUS) .. " :: "
                .. table.concat(labels, ", ") .. "]\n" .. numberedRange(lines, window.First, window.Last),
                "marker context " .. tostring(window.First) .. "-" .. tostring(window.Last), false)
        end
    end

    local ranges = functionRanges(source)
    local functions = candidateFunctions(lines, ranges, hits)
    for _, candidate in ipairs(functions) do
        local range = candidate.Range
        local coveredByLiteral = range.First >= FIRST_REQUESTED_LINE and range.Last <= requestedLast
        if coveredByLiteral then
            append("[COMPLETE FUNCTION COVERED BY LITERAL BLOCK]\nlines " .. tostring(range.First)
                .. "-" .. tostring(range.Last) .. " :: " .. table.concat(candidate.Reasons, ", "),
                "function coverage note " .. tostring(range.First) .. "-" .. tostring(range.Last), false)
        else
            append("[COMPLETE FUNCTION :: " .. table.concat(candidate.Reasons, ", ") .. "]\n"
                .. numberedRange(lines, range.First, range.Last),
                "complete function " .. tostring(range.First) .. "-" .. tostring(range.Last), false)
        end
    end
end

if #result.Warnings > 0 or #omitted > 0 then
    local footer = { "[WARNINGS / OMITTED]" }
    for _, message in ipairs(result.Warnings) do table.insert(footer, "- " .. message) end
    for _, label in ipairs(omitted) do table.insert(footer, "- omitted by 18 KB cap: " .. label) end
    local footerText = table.concat(footer, "\n")
    local separator = #parts == 0 and "" or "\n\n"
    if bytes + #separator + #footerText <= MAX_REPORT_BYTES then
        table.insert(parts, footerText)
    end
end

result.Report = table.concat(parts, "\n\n")
if #result.Report > MAX_REPORT_BYTES then
    result.Report = result.Report:sub(1, MAX_REPORT_BYTES)
end

print(result.Report)
local clipboard = type(setclipboard) == "function" and setclipboard
    or (type(toclipboard) == "function" and toclipboard or nil)
if clipboard then
    local ok, clipboardError = pcall(clipboard, result.Report)
    result.CopiedToClipboard = ok
    if not ok then result.ClipboardError = tostring(clipboardError) end
else
    result.CopiedToClipboard = false
    result.ClipboardError = "setclipboard/toclipboard unavailable"
end

return result
