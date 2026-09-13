return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        local controller = Context.Controllers.AutoStealController
        C.Label(page, "AUTO STEAL", 34, "title")
        local status = C.Status(page, "Status", "IDLE")
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
        C.Toggle(page, "Teleport To Egg", true, function(value) controller:UpdateConfig("teleportToEgg", value) end)
        C.Toggle(page, "Return To Base", true, function(value) controller:UpdateConfig("returnToBase", value) end)
        C.Toggle(page, "Repeat", true, function(value) controller:UpdateConfig("repeatEnabled", value) end)
        C.NumberInput(page, "Delay", 0.5, function(value) return controller:UpdateConfig("delay", value) end)
        C.NumberInput(page, "Carry Timeout", 5, function(value) return controller:UpdateConfig("carryTimeout", value) end)
        C.NumberInput(page, "Retries", 2, function(value) return controller:UpdateConfig("retries", value) end)
        local target = C.Label(page, "", 104, "mono")

        local function refreshSelectors()
            local areaList, assetList, err = Context.Services.AreaService:Snapshot()
            if not areaList then
                controller:Stop("Auto Steal UI data error: " .. tostring(err), true)
                return
            end
            table.insert(areaList, 1, "All Zones")
            table.insert(assetList, 1, "Any")
            local zone = controller.Config.zone == "ALL" and "All Zones" or controller.Config.zone
            local asset = controller.Config.specificAsset == "ANY" and "Any" or controller.Config.specificAsset
            if not table.find(areaList, zone) then
                zone = "All Zones"
                controller:UpdateConfig("zone", "ALL")
            end
            if not table.find(assetList, asset) then
                asset = "Any"
                controller:UpdateConfig("specificAsset", "ANY")
            end
            zones:SetOptions(areaList, zone)
            assets:SetOptions(assetList, asset)
        end
        C.Button(page, "REFRESH ZONES / ASSETS", refreshSelectors)
        C.Button(page, "START", function()
            local ok, err = controller:Start()
            if not ok then Context.Logger:Error("Auto Steal start: " .. tostring(err)) end
        end, C.Theme.success)
        C.Button(page, "STOP", function() controller:Stop() end, C.Theme.danger)

        local function refreshStatus()
            status:Set(Context.State:Get("autoStealState", "IDLE"))
            target.Text = table.concat({
                "Current Target: " .. (Context.State:Get("currentTargetUid", "") ~= "" and "ACTIVE" or "-"),
                "UID: " .. Context.State:Get("currentTargetUid", ""),
                "Area: " .. Context.State:Get("currentTargetArea", ""),
                "Completed: " .. tostring(Context.State:Get("completed", 0)) .. "    Failed: " .. tostring(Context.State:Get("failed", 0)),
                "Detail: " .. Context.State:Get("autoStealMessage", ""),
            }, "\n")
        end
        window:Connect(Context.State.Changed, refreshStatus)
        refreshSelectors()
        refreshStatus()
    end
end
