return function(Context)
    local State = {}
    State.__index = State

    function State.new(initial)
        local changed = Instance.new("BindableEvent")
        return setmetatable({
            _data = initial or {},
            _changed = changed,
            Changed = changed.Event,
        }, State)
    end

    function State:Get(key, fallback)
        local value = self._data[key]
        if value == nil then
            return fallback
        end
        return value
    end

    function State:Set(key, value)
        if self._destroyed then
            return false
        end
        local previous = self._data[key]
        if previous == value then
            return false
        end
        self._data[key] = value
        self._changed:Fire(key, value, previous)
        return true
    end

    function State:Patch(values)
        for key, value in pairs(values) do
            self:Set(key, value)
        end
    end

    function State:Snapshot()
        local copy = {}
        for key, value in pairs(self._data) do
            copy[key] = value
        end
        return copy
    end

    function State:Destroy()
        self._destroyed = true
        self._changed:Destroy()
        self._data = {}
    end

    return State
end
