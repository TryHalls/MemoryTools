return function(Context)
    local Logger = {}
    Logger.__index = Logger

    local function stringify(value)
        local ok, result = pcall(function()
            return tostring(value)
        end)
        return ok and result or "<unprintable>"
    end

    function Logger.new(limit)
        local changed = Instance.new("BindableEvent")
        return setmetatable({
            _entries = {},
            _limit = math.max(10, tonumber(limit) or 50),
            _changed = changed,
            Changed = changed.Event,
            LastAction = "Boot",
            LastResult = "Starting",
            LastError = "",
        }, Logger)
    end

    function Logger:_push(level, message)
        local entry = {
            time = os.date("%H:%M:%S"),
            level = level,
            message = stringify(message),
        }
        table.insert(self._entries, entry)
        while #self._entries > self._limit do
            table.remove(self._entries, 1)
        end
        if level == "ERROR" then
            self.LastError = entry.message
        end
        if self._changed then
            self._changed:Fire(entry)
        end
        return entry
    end

    function Logger:Info(message)
        return self:_push("INFO", message)
    end

    function Logger:Warn(message)
        return self:_push("WARN", message)
    end

    function Logger:Error(message)
        return self:_push("ERROR", message)
    end

    function Logger:Action(action, result)
        self.LastAction = stringify(action)
        if result ~= nil then
            self.LastResult = stringify(result)
        end
        self:_push("INFO", self.LastAction .. (result ~= nil and (": " .. self.LastResult) or ""))
    end

    function Logger:GetEntries()
        local copy = {}
        for index, entry in ipairs(self._entries) do
            copy[index] = entry
        end
        return copy
    end

    function Logger:Format()
        local lines = {}
        for _, entry in ipairs(self._entries) do
            table.insert(lines, string.format("[%s] %-5s %s", entry.time, entry.level, entry.message))
        end
        return table.concat(lines, "\n")
    end

    function Logger:Destroy()
        if self._changed then
            self._changed:Destroy()
            self._changed = nil
        end
        self._entries = {}
    end

    return Logger
end
