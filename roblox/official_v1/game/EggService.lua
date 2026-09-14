return function(Context)
    local EggService = {}
    EggService.__index = EggService
    local Task = Context.Task or task
    local Players = Context.Players

    function EggService.new(context)
        return setmetatable({
            Context = context,
            Logger = context.Logger,
            Dependencies = context.Dependencies,
            LastCount = 0,
            LastInvalidCount = 0,
            LastSnapshot = nil,
        }, EggService)
    end

    function EggService:_module()
        return self.Dependencies:Get("EggState")
    end

    function EggService:IsReady()
        local module = self:_module()
        return type(module) == "table" and type(module.ReadFieldEggs) == "function"
            and module.CarryChanged ~= nil and type(module.CarryChanged.Connect) == "function"
    end

    function EggService:ReadAll()
        self.LastCount = 0
        self.LastInvalidCount = 0
        self.LastSnapshot = nil
        local module = self:_module()
        if not module or type(module.ReadFieldEggs) ~= "function" then
            return nil, "EggState.ReadFieldEggs unavailable"
        end
        local ok, snapshot = pcall(module.ReadFieldEggs)
        if not ok then
            self.Logger:Error("ReadFieldEggs failed: " .. tostring(snapshot))
            return nil, tostring(snapshot)
        end
        if type(snapshot) ~= "table" then
            return nil, "ReadFieldEggs returned " .. type(snapshot)
        end

        self.LastSnapshot = snapshot
        local records = snapshot.Records
        if records ~= nil and type(records) ~= "table" then
            return nil, "ReadFieldEggs snapshot.Records returned " .. type(records)
        end
        if records == nil then
            -- Compatibility fallback for the previously observed direct-record map.
            records = snapshot
        end

        local list = {}
        local invalidCount = 0
        for key, record in pairs(records) do
            if type(record) == "table" then
                if record.Uid == nil and type(key) == "string" then
                    local copy = {}
                    for field, value in pairs(record) do copy[field] = value end
                    copy.Uid = key
                    record = copy
                end
                table.insert(list, record)
            else
                invalidCount = invalidCount + 1
            end
        end
        self.LastCount = #list
        self.LastInvalidCount = invalidCount
        return list
    end

    function EggService:IsFirstArea(record)
        if type(record) ~= "table" or type(record.Uid) ~= "string" then
            return false
        end
        local identity = self.Dependencies:Get("AreaEggSlotIdentity")
        if identity and type(identity.LooksLikeFirstAreaUid) == "function" then
            local ok, result = pcall(identity.LooksLikeFirstAreaUid, record.Uid)
            if ok then return result == true end
        end
        return string.find(record.Uid, "FirstAreaEgg_", 1, true) == 1
    end

    function EggService:RequestCarry(uid, firstAreaSlotKey)
        local module = self:_module()
        if not module or type(module.CarryFieldEgg) ~= "function" then
            return false, "EggState.CarryFieldEgg unavailable"
        end
        local packed = table.pack(pcall(module.CarryFieldEgg, uid, firstAreaSlotKey))
        if not packed[1] then
            self.Logger:Error("CarryFieldEgg failed: " .. tostring(packed[2]))
            return false, tostring(packed[2])
        end
        local results = { n = math.max(0, packed.n - 1) }
        for index = 2, packed.n do
            results[index - 1] = packed[index]
        end
        return true, results
    end

    local function findCarryState(...)
        local args = table.pack(...)
        for index = 1, args.n do
            local value = args[index]
            if type(value) == "table" and (value.IsCarrying ~= nil or value.Uid ~= nil) then
                return value
            end
        end
        return nil
    end

    function EggService:RequestCarryAndWait(uid, timeout, isCancelled, onRequested)
        local module = self:_module()
        local signal = module and module.CarryChanged
        if not signal or type(signal.Connect) ~= "function" then
            return false, "EggState.CarryChanged unavailable"
        end

        local confirmed = false
        local observedMismatch = nil
        local connection = signal:Connect(function(...)
            local carry = findCarryState(...)
            if carry and carry.IsCarrying == true then
                if carry.Uid == uid then
                    confirmed = true
                elseif carry.Uid ~= nil then
                    observedMismatch = tostring(carry.Uid)
                end
            end
        end)

        local requested, requestResult = self:RequestCarry(uid, nil)
        if not requested then
            connection:Disconnect()
            return false, requestResult
        end
        if onRequested then
            pcall(onRequested, requestResult)
        end

        local deadline = os.clock() + math.max(0.25, tonumber(timeout) or 5)
        while not confirmed and os.clock() < deadline do
            if isCancelled and isCancelled() then
                connection:Disconnect()
                return false, "cancelled"
            end
            task.wait(0.05)
        end
        connection:Disconnect()
        if confirmed then
            return true, requestResult
        end
        if observedMismatch then
            return false, "carry mismatch: " .. observedMismatch
        end
        return false, "carry confirmation timeout"
    end

    function EggService:WaitForManualCarry(uid, isCancelled, onWaiting)
        if type(uid) ~= "string" or uid == "" then
            return false, "invalid egg uid"
        end

        local module = self:_module()
        local signal = module and module.CarryChanged
        if not signal or type(signal.Connect) ~= "function" then
            return false, "EggState.CarryChanged unavailable"
        end

        local confirmed = false
        local confirmation = nil
        local observedMismatch = nil
        local connection = signal:Connect(function(...)
            local carry = findCarryState(...)
            if carry and carry.IsCarrying == true then
                if carry.Uid == uid then
                    confirmed = true
                    confirmation = carry
                elseif carry.Uid ~= nil then
                    observedMismatch = tostring(carry.Uid)
                end
            end
        end)

        if onWaiting then pcall(onWaiting) end

        local lastPoll = -math.huge
        while not confirmed do
            if isCancelled and isCancelled() then
                connection:Disconnect()
                return false, "cancelled"
            end

            local now = os.clock()
            if type(module.ReadFieldEgg) == "function" and now - lastPoll >= 0.25 then
                lastPoll = now
                local readOk, record = pcall(module.ReadFieldEgg, uid)
                if not Players then Players = game:GetService("Players") end
                local localPlayer = Players and Players.LocalPlayer
                if readOk and type(record) == "table" and localPlayer
                    and record.CarrierUserId == localPlayer.UserId then
                    confirmed = true
                    confirmation = record
                end
            end

            if not confirmed then Task.wait(0.05) end
        end

        connection:Disconnect()
        return true, confirmation, observedMismatch
    end

    function EggService:Destroy() end

    return EggService
end
