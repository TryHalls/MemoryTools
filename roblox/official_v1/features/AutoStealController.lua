return function(Context)
    local AutoStealController = {}
    AutoStealController.__index = AutoStealController

    local Players = Context.Players or game:GetService("Players")
    local VALID_MODES = { Nearest = true, Random = true, ["Specific Asset"] = true }

    local function tracebackError(err)
        if type(debug) == "table" and type(debug.traceback) == "function" then
            return debug.traceback(tostring(err), 2)
        end
        return tostring(err)
    end

    function AutoStealController.new(context)
        return setmetatable({
            Context = context,
            Logger = context.Logger,
            EggService = context.Services.EggService,
            AreaService = context.Services.AreaService,
            PlotService = context.Services.PlotService,
            Config = {
                enabled = false,
                zone = "ALL",
                targetMode = "Nearest",
                specificAsset = "ANY",
                rarity = "ANY",
                movementMode = "FLIGHT",
                flightSpeed = 40,
                teleportToEgg = true,
                returnToBase = true,
                repeatEnabled = true,
                delay = 0.5,
                retries = 2,
                skipFirstAreaSpecial = true,
            },
            _generation = 0,
            _workerRunning = false,
            _failedTargets = {},
        }, AutoStealController)
    end

    function AutoStealController:GetMovementBackend()
        return self.Config.movementMode
    end

    function AutoStealController:_state(value)
        self.Context.State:Set("autoStealState", value)
    end

    function AutoStealController:_active(generation)
        return self.Config.enabled and generation == self._generation and not self.Context.Destroyed
    end

    function AutoStealController:_isFatalAttemptError(result)
        return result == "Humanoid is dead"
    end

    function AutoStealController:_wait(seconds, generation)
        local deadline = os.clock() + math.max(0, seconds or 0)
        repeat
            if not self:_active(generation) then return false end
            task.wait(math.min(0.05, math.max(0.01, deadline - os.clock())))
        until os.clock() >= deadline
        return self:_active(generation)
    end

    function AutoStealController:UpdateConfig(key, value)
        if self.Config[key] == nil then return false, "Unknown config key" end
        if key == "delay" then
            value = tonumber(value)
            if not value or value < 0.1 or value > 60 then return false, key .. " must be 0.1-60" end
        elseif key == "retries" then
            value = tonumber(value)
            if not value or value < 0 or value > 10 then return false, "retries must be 0-10" end
            value = math.floor(value)
        elseif key == "flightSpeed" then
            value = tonumber(value)
            if not value or value ~= value or value == math.huge or value == -math.huge
                or value < 30 or value > 150 then return false, "flightSpeed must be 30-150" end
        elseif key == "movementMode" and value ~= "FLIGHT" then
            return false, "movementMode must be FLIGHT"
        elseif key == "targetMode" and not VALID_MODES[value] then
            return false, "Invalid target mode"
        end
        self.Config[key] = value
        return true
    end

    function AutoStealController:_validRecord(record)
        if type(record) ~= "table" then return false end
        if type(record.Uid) ~= "string" or record.Uid == "" then return false end
        if type(record.AreaId) ~= "string" or record.AreaId == "" then return false end
        if type(record.NestId) ~= "string" or record.NestId == "" then return false end
        if self.Config.skipFirstAreaSpecial and self.EggService:IsFirstArea(record) then return false end
        local localPlayer = Players.LocalPlayer
        if record.CarrierUserId ~= nil
            and (not localPlayer or tonumber(record.CarrierUserId) ~= localPlayer.UserId) then return false end
        if self.Config.zone ~= "ALL" and record.AreaId ~= self.Config.zone then return false end
        if self.Config.targetMode == "Specific Asset" then
            if self.Config.specificAsset == "ANY" or record.AssetCategory ~= self.Config.specificAsset then return false end
        end
        if self._failedTargets[record.Uid] then return false end
        return true
    end

    function AutoStealController:_select(records)
        self:_state("FILTER")
        if self.Config.targetMode == "Specific Asset" and self.Config.specificAsset == "ANY" then
            return nil, "Select a specific asset"
        end
        local candidates = {}
        for _, record in ipairs(records) do
            if self:_validRecord(record) then table.insert(candidates, record) end
        end
        if #candidates == 0 then return nil, "Waiting for eligible field eggs" end
        self:_state("SELECT_TARGET")
        if self.Config.targetMode == "Random" then
            return candidates[math.random(1, #candidates)]
        end
        local root = self.Context.Character.Root
        if not root then return nil, "HumanoidRootPart is not ready" end
        local nearest, nearestDistance, nearestCFrame
        for _, record in ipairs(candidates) do
            local cframe = self.AreaService:GetNestCFrame(record)
            if cframe then
                local distance = (root.Position - cframe.Position).Magnitude
                if not nearestDistance or distance < nearestDistance then
                    nearest, nearestDistance, nearestCFrame = record, distance, cframe
                end
            end
        end
        if not nearest then return nil, "No candidate nest could be resolved" end
        return nearest, nil, nearestCFrame
    end

    function AutoStealController:_attempt(record, cachedCFrame, generation)
        local uid = record.Uid
        self.Context.State:Patch({ currentTargetUid = uid, currentTargetArea = record.AreaId })
        self:_state("RESOLVE_NEST")
        local nestCFrame, err = cachedCFrame, nil
        if not nestCFrame then nestCFrame, err = self.AreaService:GetNestCFrame(record) end
        if not nestCFrame then return false, err end
        if not self:_active(generation) then return false, "cancelled" end

        if self.Config.teleportToEgg then
            self:_state("FLY_TO_TARGET")
            local travelled, travelError = self.Context.Services.LobbyRouteService:TravelTo(nestCFrame, {
                label = "field egg " .. uid,
                horizontalSpeed = self.Config.flightSpeed,
                cancelCheck = function() return not self:_active(generation) end,
            })
            if not travelled then return false, travelError end
            self:_state("ARRIVAL_STABILIZE")
            local root = self.Context.Character.Root
            if root and root.Parent then root.AssemblyLinearVelocity = Vector3.zero end
            if not self:_wait(0.2, generation) then return false, "cancelled" end
        end

        self:_state("WAIT_MANUAL_CARRY")
        local carried, carryInfo = self.EggService:WaitForManualCarry(uid, function()
            return not self:_active(generation)
        end, function()
            if self:_active(generation) then
                self.Context.State:Set("autoStealMessage", "Grab the egg manually")
            end
        end)
        if not carried then return false, carryInfo end
        if not self:_active(generation) then return false, "cancelled" end

        self.Context.State:Set(
            "autoStealMessage",
            self.Config.returnToBase and "Manual carry confirmed — returning to base" or "Manual carry confirmed"
        )

        if self.Config.returnToBase then
            self:_state("FLY_TO_BASE")
            local baseCFrame, plotError = self.PlotService:GetRespawnCFrame()
            if not baseCFrame then return false, plotError end
            local returned, returnError = self.Context.Services.LobbyRouteService:TravelTo(baseCFrame, {
                label = "base with " .. uid,
                horizontalSpeed = self.Config.flightSpeed,
                cancelCheck = function() return not self:_active(generation) end,
            })
            if not returned then return false, returnError end
        end
        self:_state("DONE")
        return true, carryInfo
    end

    function AutoStealController:_run(generation)
        self._workerRunning = true
        while self:_active(generation) do
            self:_state("REFRESH_EGGS")
            local records, readError = self.EggService:ReadAll()
            if not records then
                self.Context.State:Set("failed", self.Context.State:Get("failed", 0) + 1)
                self:Stop("Auto Steal data error: " .. tostring(readError), true)
                break
            end
            local target, selectError, cachedCFrame = self:_select(records)
            if not target then
                self.Context.State:Set("autoStealMessage", tostring(selectError))
                if not self.Config.repeatEnabled then break end
                self:_state("COOLDOWN")
                if not self:_wait(self.Config.delay, generation) then break end
            else
                self.Context.State:Set("autoStealMessage", "")
                local success, result
                for attempt = 0, self.Config.retries do
                    if not self:_active(generation) then break end
                    success, result = self:_attempt(target, cachedCFrame, generation)
                    if success then break end
                    if result == "cancelled" then break end
                    if self:_isFatalAttemptError(result) then
                        self._failedTargets[target.Uid] = true
                        self.Context.State:Set("failed", self.Context.State:Get("failed", 0) + 1)
                        self:Stop("Auto Steal stopped: Humanoid is dead", true)
                        break
                    end
                    if attempt < self.Config.retries then
                        self:_state("RETRY")
                        self.Logger:Warn("Retry " .. tostring(attempt + 1) .. " for " .. target.Uid .. ": " .. tostring(result))
                        if not self:_wait(self.Config.delay, generation) then break end
                    end
                end
                if not self:_active(generation) then break end
                if success then
                    self.Context.State:Set("completed", self.Context.State:Get("completed", 0) + 1)
                    self.Logger:Action("Auto Steal completed", target.Uid)
                else
                    self._failedTargets[target.Uid] = true
                    self.Context.State:Set("failed", self.Context.State:Get("failed", 0) + 1)
                    self.Logger:Error("Auto Steal failed for " .. target.Uid .. ": " .. tostring(result))
                end
                if not self.Config.repeatEnabled then break end
                self:_state("COOLDOWN")
                if not self:_wait(self.Config.delay, generation) then break end
            end
        end
        if generation == self._generation then
            self.Config.enabled = false
            self:_state("IDLE")
            self.Context.State:Patch({ currentTargetUid = "", currentTargetArea = "", autoStealMessage = "" })
        end
        self._workerRunning = false
    end

    function AutoStealController:Start()
        if self.Config.enabled or self._workerRunning then return false, "Auto Steal is already running" end
        if not self.EggService:IsReady() then return false, "EggState is unavailable" end
        if not self.Context.Dependencies:IsReady("NestResolver") then return false, "NestResolver is unavailable" end
        local records, readError = self.EggService:ReadAll()
        if not records then
            self:Stop("Auto Steal data error: " .. tostring(readError), true)
            return false, readError
        end
        self._generation = self._generation + 1
        self.Config.enabled = true
        self._failedTargets = {}
        self.Context.State:Patch({ autoStealMessage = "", autoStealMovementWarning = "" })
        local generation = self._generation
        task.spawn(function()
            local ok, err = xpcall(function() self:_run(generation) end, tracebackError)
            if not ok then
                if generation == self._generation then
                    self:Stop("Auto Steal worker crashed:\n" .. tostring(err), true)
                    self._workerRunning = false
                end
            end
        end)
        return true
    end

    function AutoStealController:Stop(reason, isError)
        self._generation = self._generation + 1
        self.Config.enabled = false
        self.Context.FlightMovement:Cancel(reason or "Auto Steal stopped")
        self:_state(isError and "ERROR" or "STOPPED")
        self.Context.State:Patch({
            currentTargetUid = "",
            currentTargetArea = "",
            autoStealMessage = tostring(reason or ""),
        })
        if isError then
            self.Logger:Error(tostring(reason or "Auto Steal stopped by error"))
        else
            self.Logger:Info(tostring(reason or "Auto Steal stopped"))
        end
    end

    function AutoStealController:Destroy()
        self:Stop()
    end

    return AutoStealController
end
