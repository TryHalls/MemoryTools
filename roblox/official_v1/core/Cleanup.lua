return function(Context)
    local Cleanup = {}
    Cleanup.__index = Cleanup

    function Cleanup.new()
        return setmetatable({
            _items = {},
            _cleaning = false,
            _destroyed = false,
        }, Cleanup)
    end

    function Cleanup:Add(item, method)
        if item == nil then
            return nil
        end
        if self._destroyed then
            self:_cleanItem(item, method)
            return item
        end
        table.insert(self._items, { item = item, method = method })
        return item
    end

    function Cleanup:_cleanItem(item, method)
        local kind = typeof(item)
        local ok, err = pcall(function()
            if method then
                item[method](item)
            elseif kind == "RBXScriptConnection" then
                item:Disconnect()
            elseif kind == "Instance" then
                item:Destroy()
            elseif type(item) == "function" then
                item()
            elseif type(item) == "table" then
                if type(item.Destroy) == "function" then
                    item:Destroy()
                elseif type(item.Cleanup) == "function" then
                    item:Cleanup()
                elseif type(item.Disconnect) == "function" then
                    item:Disconnect()
                end
            end
        end)
        if not ok and Context and Context.Logger then
            Context.Logger:Warn("Cleanup failed: " .. tostring(err))
        end
    end

    function Cleanup:Cleanup()
        if self._cleaning then
            return
        end
        self._cleaning = true
        for index = #self._items, 1, -1 do
            local entry = self._items[index]
            self._items[index] = nil
            self:_cleanItem(entry.item, entry.method)
        end
        self._cleaning = false
    end

    function Cleanup:Destroy()
        if self._destroyed then
            return
        end
        self._destroyed = true
        self:Cleanup()
    end

    return Cleanup
end
