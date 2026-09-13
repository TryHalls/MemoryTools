return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        C.Label(page, "SETTINGS", 34, "title")
        C.Dropdown(page, "UI Scale", { "0.80", "0.90", "1.00", "1.10", "1.20" }, "1.00", function(value)
            local number = tonumber(value)
            Context.Config.uiScale = number
            window:SetScale(number)
        end)
        C.Toggle(page, "Notifications", Context.Config.notifications, function(value)
            Context.Config.notifications = value
        end)
        local reapplyToggle = C.Toggle(page, "Reapply Player Settings", Context.Config.reapplyPlayerSettings, function(value)
            Context.Config.reapplyPlayerSettings = value
            Context.Controllers.PlayerController:SetReapply(value)
        end)
        window:Connect(Context.State.Changed, function(key)
            if key == "reapplyPlayerSettings" then
                reapplyToggle:Set(Context.State:Get(key, true), true)
            end
        end)
        C.Label(page, "Settings are session-only in V1.0.2.\nNo filesystem persistence is used.", 58, "muted")
        C.Button(page, "CLOSE MEMORYTOOLS", function()
            Context:Destroy("closed from Settings")
        end, C.Theme.danger)
    end
end
