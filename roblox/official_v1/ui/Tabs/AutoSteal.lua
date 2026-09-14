return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        local controller = Context.Controllers.AutoStealController

        local function tracebackError(err)
            if type(debug) == "table" and type(debug.traceback) == "function" then
                return debug.traceback(tostring(err), 2)
            end
            return tostring(err)
        end

        C.Label(page, "AUTO STEAL", 34, "title")
        local status = C.Status(page, "Status", "IDLE")
        local movementBackend = C.Status(page, "Movement Backend", "FLIGHT")
        local selectorError = C.Label(page, "", 54, "muted")
        selectorError.TextColor3 = C.Theme.danger
        selectorError.Visible = false
        local diagnostics = C.Label(page, "Field Eggs: 0    Zones: 0    Assets: 0", 32, "mono")
        local zones = C.Dropdown(page, "Zone", { "All Zones" }, "All Zones", function(value)
            controller:UpdateConfig("zone", value == "All Zones" and "ALL" or value)
        end)
        C.Dropdown(page, "Target Mode", { "Nearest", "Random", "Specific Asset" }, "Nearest", function(value)
            controller:UpdateConfig("targetMode", value)
        end)
        local assets = C.Dropdown(page, "Specific Asset", { "Any" }, "Any", function(value)
            controller:UpdateConfig("specificAsset", value == "Any" and "ANY" or value)
        end)
        C.Section(page, "Options")
        C.Toggle(page, "Travel To Egg", true, function(value) controller:UpdateConfig("teleportToEgg", value) end)
        C.Toggle(page, "Return To Base", true, function(value) controller:UpdateConfig("returnToBase", value) end)
        C.Toggle(page, "Repeat", true, function(value) controller:UpdateConfig("repeatEnabled", value) end)
        C.NumberInput(page, "Flight Speed", 140, function(value) return controller:UpdateConfig("flightSpeed", value) end)
        C.NumberInput(page, "Delay", 0.5, function(value) return controller:UpdateConfig("delay", value) end)
        C.NumberInput(page, "Carry Timeout", 5, function(value) return controller:UpdateConfig("carryTimeout", value) end)
        C.NumberInput(page, "Retries", 2, function(value) return controller:UpdateConfig("retries", value) end)
        local target = C.Label(page, "", 104, "mono")

        local selectorsReady = false
        local lastAreaOptions = { "All Zones" }
        local lastZoneSelection = "All Zones"

        local function failRefresh(message)
            selectorsReady = false
            selectorError.Text = "Selector refresh failed:\n" .. tostring(message)
            selectorError.Visible = true
            if controller.Config.enabled or controller._workerRunning then
                controller:Stop("Auto Steal UI data error: " .. tostring(message), true)
            end
            return false, message
        end

        local function refreshSelectorsUnsafe()
            local areaList, assetList, snapshotError, eggs = Context.Services.AreaService:Snapshot()
            if not areaList then return failRefresh(snapshotError or "AreaService snapshot unavailable") end

            local nextAreas = { "All Zones" }
            local nextAssets = { "Any" }
            for _, areaId in ipairs(areaList) do table.insert(nextAreas, areaId) end
            for _, assetCategory in ipairs(assetList) do table.insert(nextAssets, assetCategory) end

            local zone = controller.Config.zone == "ALL" and "All Zones" or controller.Config.zone
            local asset = controller.Config.specificAsset == "ANY" and "Any" or controller.Config.specificAsset
            if not table.find(nextAreas, zone) then zone = "All Zones" end
            if not table.find(nextAssets, asset) then asset = "Any" end

            local zonesOk, zonesError = zones:SetOptions(nextAreas, zone)
            if not zonesOk then return failRefresh("Zone dropdown: " .. tostring(zonesError)) end
            local assetsOk, assetsError = assets:SetOptions(nextAssets, asset)
            if not assetsOk then
                zones:SetOptions(lastAreaOptions, lastZoneSelection)
                return failRefresh("Asset dropdown: " .. tostring(assetsError))
            end

            lastAreaOptions = nextAreas
            lastZoneSelection = zone
            controller:UpdateConfig("zone", zone == "All Zones" and "ALL" or zone)
            controller:UpdateConfig("specificAsset", asset == "Any" and "ANY" or asset)
            diagnostics.Text = "Field Eggs: " .. tostring(type(eggs) == "table" and #eggs or 0)
                .. "    Zones: " .. tostring(#areaList)
                .. "    Assets: " .. tostring(#assetList)
            selectorError.Text = ""
            selectorError.Visible = false
            selectorsReady = true
            return true
        end

        local function refreshSelectors()
            local ran, success, refreshError = xpcall(refreshSelectorsUnsafe, tracebackError)
            if not ran then return failRefresh(success) end
            if success == false then return false, refreshError end
            return true
        end

        C.Button(page, "REFRESH ZONES / ASSETS", function()
            local ok, err = refreshSelectors()
            if not ok then Context.Logger:Warn("Auto Steal selectors: " .. tostring(err)) end
        end)
        C.Button(page, "START", function()
            if not selectorsReady then
                Context.Logger:Warn("Auto Steal start blocked: refresh zones/assets first")
                return
            end
            local ok, err = controller:Start()
            if not ok then Context.Logger:Error("Auto Steal start: " .. tostring(err)) end
        end, C.Theme.success)
        C.Button(page, "STOP", function() controller:Stop() end, C.Theme.danger)

        local function refreshStatus()
            status:Set(Context.State:Get("autoStealState", "IDLE"))
            local backend = controller:GetMovementBackend()
            movementBackend:Set(backend, C.Theme.success)
            target.Text = table.concat({
                "Current Target: " .. (Context.State:Get("currentTargetUid", "") ~= "" and "ACTIVE" or "-"),
                "UID: " .. Context.State:Get("currentTargetUid", ""),
                "Area: " .. Context.State:Get("currentTargetArea", ""),
                "Completed: " .. tostring(Context.State:Get("completed", 0)) .. "    Failed: " .. tostring(Context.State:Get("failed", 0)),
                "Detail: " .. Context.State:Get("autoStealMessage", ""),
            }, "\n")
        end
        window:Connect(Context.State.Changed, refreshStatus)
        refreshStatus()

        -- Dynamic game data is intentionally read only after every control above
        -- exists, so a bad snapshot cannot leave a half-constructed tab.
        task.defer(function()
            local ok, err = refreshSelectors()
            if not ok then Context.Logger:Warn("Initial Auto Steal selectors: " .. tostring(err)) end
        end)
    end
end
