return function(Context)
    local StaffService = {}
    StaffService.__index = StaffService

    local function finiteNumber(value, minimum, maximum, label)
        value = tonumber(value)
        if not value or value ~= value or value == math.huge or value == -math.huge then
            return nil, label .. " must be a finite number"
        end
        if value < minimum then
            return nil, label .. " must be at least " .. tostring(minimum)
        end
        if maximum ~= nil and value > maximum then
            return nil, label .. " must be " .. tostring(minimum) .. "-" .. tostring(maximum)
        end
        return value
    end

    local function callableMember(value, member)
        if value == nil then return false end
        local ok, candidate = pcall(function() return value[member] end)
        return ok and type(candidate) == "function"
    end

    local function clientSignal(remote)
        if remote == nil then return nil end
        local ok, signal = pcall(function() return remote.OnClientEvent end)
        if ok and callableMember(signal, "Connect") then return signal end
        return nil
    end

    local function disconnect(connection)
        if callableMember(connection, "Disconnect") then
            pcall(function() connection:Disconnect() end)
        end
    end

    local function safeValue(value, depth, seen)
        local valueType = typeof(value)
        if valueType ~= "table" then
            local ok, rendered = pcall(function() return tostring(value) end)
            return ok and (valueType .. "(" .. rendered .. ")") or (valueType .. "(<unprintable>)")
        end
        if depth >= 2 then return "table(<depth-limit>)" end
        if seen[value] then return "table(<cycle>)" end
        seen[value] = true
        local parts = {}
        local count = 0
        local ok = pcall(function()
            for key, item in pairs(value) do
                count = count + 1
                if count > 12 then
                    table.insert(parts, "...")
                    break
                end
                table.insert(parts, safeValue(key, depth + 1, seen) .. "=" .. safeValue(item, depth + 1, seen))
            end
        end)
        seen[value] = nil
        if not ok then table.insert(parts, "<iteration-error>") end
        return "table{" .. table.concat(parts, ", ") .. "}"
    end

    function StaffService.new(context)
        local self = setmetatable({
            Context = context,
            Logger = context.Logger,
            StaffStatus = nil,
            LastSpeedPowerVerdict = nil,
            _console = nil,
            _speedVerdictConnection = nil,
            _probeConnection = nil,
            _destroyed = false,
        }, StaffService)
        self:_refreshRemotes()
        return self
    end

    function StaffService:_refreshRemotes()
        local remotes = self.Context.Dependencies:Get("Remotes")
        local console = type(remotes) == "table" and remotes.StaffConsole or nil
        self._console = type(console) == "table" and console or nil
        console = self._console
        self._probeRemote = console and console.ProbeStaffStatus or nil
        self._staffVerdictSignal = console and clientSignal(console.StaffVerdict) or nil
        self._walkSpeedRemote = console and console.WriteWalkSpeed or nil
        self._speedPowerRemote = console and console.WriteSpeedPower or nil
        self._speedPowerVerdictSignal = console and clientSignal(console.SpeedPowerVerdict) or nil

        disconnect(self._speedVerdictConnection)
        self._speedVerdictConnection = nil
        if self._speedPowerVerdictSignal then
            self._speedVerdictConnection = self._speedPowerVerdictSignal:Connect(function(...)
                local arguments = table.pack(...)
                self.LastSpeedPowerVerdict = arguments
                local rendered = {}
                for index = 1, arguments.n do
                    rendered[index] = "#" .. tostring(index) .. "=" .. safeValue(arguments[index], 0, {})
                end
                self.Logger:Info("SpeedPowerVerdict args[" .. tostring(arguments.n) .. "]: " .. table.concat(rendered, "; "))
                self.Context.State:Set("speedPowerVerdictRevision",
                    self.Context.State:Get("speedPowerVerdictRevision", 0) + 1)
            end)
        end
    end

    function StaffService:IsReady()
        local function validInterface()
            return type(self._console) == "table"
                and callableMember(self._probeRemote, "FireServer")
                and self._staffVerdictSignal ~= nil
                and callableMember(self._walkSpeedRemote, "FireServer")
                and callableMember(self._speedPowerRemote, "FireServer")
                and self._speedPowerVerdictSignal ~= nil
        end
        if not validInterface() then self:_refreshRemotes() end
        return validInterface()
    end

    function StaffService:Probe(timeout)
        if not self:IsReady() then return false, "StaffConsole interface is unavailable" end
        if self._probeConnection then return false, "Staff probe is already running" end
        timeout = tonumber(timeout) or 5
        timeout = math.max(0.25, math.min(timeout, 15))
        self.StaffStatus = nil
        self.Context.State:Set("staffStatus", "UNKNOWN")

        local received = false
        local connection = self._staffVerdictSignal:Connect(function(isStaff)
            if received then return end
            received = true
            self.StaffStatus = isStaff == true
        end)
        self._probeConnection = connection
        local fired, fireError = pcall(function()
            self._probeRemote:FireServer()
        end)
        if not fired then
            disconnect(connection)
            self._probeConnection = nil
            return false, "ProbeStaffStatus failed: " .. tostring(fireError)
        end
        local deadline = os.clock() + timeout
        while not received and os.clock() < deadline and not self._destroyed do
            task.wait(0.05)
        end
        disconnect(connection)
        self._probeConnection = nil
        if not received then
            self.Logger:Warn("Staff probe timed out")
            return false, "Staff verdict timed out"
        end
        local label = self.StaffStatus and "AUTHORIZED" or "DENIED"
        self.Context.State:Set("staffStatus", label)
        self.Logger:Action("Staff probe", label)
        return true, self.StaffStatus
    end

    function StaffService:IsStaff()
        return self.StaffStatus
    end

    function StaffService:_canWrite()
        if self.StaffStatus ~= true then return false, "Staff authorization is not confirmed" end
        if not self:IsReady() then return false, "StaffConsole interface is unavailable" end
        return true
    end

    function StaffService:SetWalkSpeed(value)
        local allowed, permissionError = self:_canWrite()
        if not allowed then return false, permissionError end
        local validated, validationError = finiteNumber(value, 0, 1000, "WalkSpeed")
        if not validated then return false, validationError end
        local ok, err = pcall(function() self._walkSpeedRemote:FireServer(validated) end)
        if not ok then return false, "WriteWalkSpeed failed: " .. tostring(err) end
        self.Logger:Action("Server WalkSpeed", validated)
        return true
    end

    function StaffService:SetSpeedPower(value)
        local allowed, permissionError = self:_canWrite()
        if not allowed then return false, permissionError end
        local validated, validationError = finiteNumber(value, 0, nil, "SpeedPower")
        if not validated then return false, validationError end
        local ok, err = pcall(function() self._speedPowerRemote:FireServer(validated) end)
        if not ok then return false, "WriteSpeedPower failed: " .. tostring(err) end
        self.Logger:Action("Server SpeedPower request", validated)
        return true
    end

    function StaffService:Destroy()
        self._destroyed = true
        disconnect(self._speedVerdictConnection)
        disconnect(self._probeConnection)
        self._speedVerdictConnection = nil
        self._probeConnection = nil
    end

    return StaffService
end
