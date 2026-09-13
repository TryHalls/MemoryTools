return function(_Context)
    local DropdownPool = {}

    local function tracebackError(err)
        if type(debug) == "table" and type(debug.traceback) == "function" then
            return debug.traceback(tostring(err), 2)
        end
        return tostring(err)
    end

    function DropdownPool.Apply(records, options, createRecord, updateRecord)
        if type(records) ~= "table" then return false, "records must be a table" end
        if type(options) ~= "table" then return false, "options must be a table" end
        if type(createRecord) ~= "function" then return false, "createRecord must be a function" end
        if type(updateRecord) ~= "function" then return false, "updateRecord must be a function" end

        local prepared = {}
        local preparedOk, preparedError = xpcall(function()
            for index, value in ipairs(options) do
                prepared[index] = {
                    Value = value,
                    Text = tostring(value),
                }
            end
        end, tracebackError)
        if not preparedOk then return false, preparedError end

        -- Grow before mutating existing records. If creation fails, the prior
        -- visible selection remains usable and a later refresh can retry.
        local growOk, growError = xpcall(function()
            for index = #records + 1, #prepared do
                local record = createRecord(index)
                if type(record) ~= "table" then
                    error("createRecord returned " .. type(record))
                end
                table.insert(records, record)
            end
        end, tracebackError)
        if not growOk then return false, growError end

        local updateOk, updateError = xpcall(function()
            for index, item in ipairs(prepared) do
                local record = records[index]
                record.Value = item.Value
                updateRecord(record, true, item.Text, index)
            end
            for index = #prepared + 1, #records do
                local record = records[index]
                record.Value = nil
                updateRecord(record, false, "", index)
            end
        end, tracebackError)
        if not updateOk then return false, updateError end
        return true
    end

    return DropdownPool
end
