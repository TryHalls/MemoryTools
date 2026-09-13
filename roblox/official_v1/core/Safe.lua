return function(Context)
    local Safe = {}

    function Safe.Call(label, callback, fallback, logger)
        local packed = table.pack(pcall(callback))
        if not packed[1] then
            if logger then logger:Warn(tostring(label) .. ": " .. tostring(packed[2])) end
            return fallback, tostring(packed[2])
        end
        return table.unpack(packed, 2, packed.n)
    end

    function Safe.Find(root, parts, startIndex)
        local current = root
        for index = startIndex or 1, #parts do
            if not current then return nil end
            local ok, nextInstance = pcall(function()
                return current:FindFirstChild(parts[index])
            end)
            if not ok then return nil end
            current = nextInstance
        end
        return current
    end

    function Safe.Require(instance)
        if not instance or not instance:IsA("ModuleScript") then
            return false, nil, "ModuleScript not found"
        end
        local ok, result = pcall(require, instance)
        if not ok then return false, nil, tostring(result) end
        return true, result, nil
    end

    return Safe
end
