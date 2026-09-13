return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        C.Label(page, "DEBUG / HEALTH", 34, "title")
        local reportLabel = C.Label(page, "Loading...", 470, "mono")
        reportLabel.TextSize = 11
        reportLabel.TextYAlignment = Enum.TextYAlignment.Top
        reportLabel.AutomaticSize = Enum.AutomaticSize.Y

        local lastReport = ""
        local function makeReport()
            local character = Context.Character
            local playerSnapshot = Context.Services.PlayerService:Snapshot()
            local eggs = Context.Services.EggService:ReadAll()
            local eggCount = eggs and #eggs or 0
            local areaSet, firstAreaCount = {}, 0
            if eggs then
                for _, record in ipairs(eggs) do
                    if type(record.AreaId) == "string" then areaSet[record.AreaId] = true end
                    if Context.Services.EggService:IsFirstArea(record) then firstAreaCount = firstAreaCount + 1 end
                end
            end
            local areaCount = 0
            for _ in pairs(areaSet) do areaCount = areaCount + 1 end
            local plot = Context.Services.PlotService:Resolve()
            local lines = {
                "MemoryTools Official V" .. Context.Version,
                "PlaceId: " .. tostring(game.PlaceId),
                "PlaceVersion: " .. tostring(game.PlaceVersion),
                "GameId: " .. tostring(game.GameId),
                "",
                "EggState: " .. Context.Dependencies:Status("EggState"),
                "PlotState: " .. Context.Dependencies:Status("PlotState"),
                "NestResolver: " .. Context.Dependencies:Status("NestResolver"),
                "AreaEggSlotIdentity: " .. Context.Dependencies:Status("AreaEggSlotIdentity"),
                "SharedPlayer: " .. Context.Dependencies:Status("SharedPlayer"),
                "",
                "Character: " .. (character.Model and "READY" or "WAITING"),
                "Humanoid: " .. (character.Humanoid and "READY" or "WAITING"),
                "HumanoidRootPart: " .. (character.Root and "READY" or "WAITING"),
                "WalkSpeed: " .. tostring(playerSnapshot.walkSpeed or "UNKNOWN"),
                "JumpPower: " .. tostring(playerSnapshot.jumpPower or "UNKNOWN"),
                "JumpHeight: " .. tostring(playerSnapshot.jumpHeight or "UNKNOWN"),
                "Gravity: " .. tostring(playerSnapshot.gravity or "UNKNOWN"),
                "SpeedPower: " .. tostring(playerSnapshot.speedPower.value or "UNKNOWN"),
                "",
                "Field Eggs Count: " .. tostring(eggCount),
                "Areas Count: " .. tostring(areaCount),
                "FIRST_AREA_SPECIAL: " .. tostring(firstAreaCount),
                "Local Plot: " .. (plot and "READY" or "NOT READY"),
                "",
                "AutoSteal State: " .. Context.State:Get("autoStealState", "IDLE"),
                "Current Target UID: " .. Context.State:Get("currentTargetUid", ""),
                "Last Action: " .. Context.Logger.LastAction,
                "Last Result: " .. Context.Logger.LastResult,
                "Last Error: " .. Context.Logger.LastError,
                "",
                "--- Recent log ---",
                Context.Logger:Format(),
            }
            return table.concat(lines, "\n")
        end

        local function refresh()
            lastReport = makeReport()
            reportLabel.Text = lastReport
        end
        C.Button(page, "COPY DEBUG", function()
            local clipboard = type(setclipboard) == "function" and setclipboard
                or (type(toclipboard) == "function" and toclipboard or nil)
            if not clipboard then
                Context.Logger:Warn("Clipboard API unavailable")
                return
            end
            local ok, err = pcall(clipboard, lastReport)
            if ok then Context.Logger:Action("Copy Debug", "report copied")
            else Context.Logger:Error("Copy Debug failed: " .. tostring(err)) end
        end, C.Theme.accent)
        C.Button(page, "REFRESH DEPENDENCIES", function()
            Context.Dependencies:Refresh()
            refresh()
        end)
        window:OnPulse(refresh)
        refresh()
    end
end
