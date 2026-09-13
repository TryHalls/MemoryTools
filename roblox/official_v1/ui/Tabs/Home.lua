return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        C.Label(page, "MemoryTools", 34, "title")
        C.Label(page, "Official V" .. Context.Version, 24, "muted")
        C.Section(page, "Game")
        C.Label(page, "PlaceId: " .. tostring(game.PlaceId) .. "\nPlaceVersion: " .. tostring(game.PlaceVersion), 48, "mono")
        C.Section(page, "Dependencies")
        local deps = C.Label(page, "", 94, "mono")
        C.Section(page, "Health")
        local character = C.Status(page, "Character", "WAITING")
        local auto = C.Status(page, "Auto Steal", "IDLE")
        C.Button(page, "TP TO BASE", function()
            Context.Controllers.TeleportController:ToBase()
        end, C.Theme.accent)
        C.Button(page, "STOP ALL", function()
            Context.Controllers.AutoStealController:Stop()
            Context.Controllers.PlayerController:ResetMovement()
        end, C.Theme.danger)

        local function refresh()
            deps.Text = table.concat({
                "EggState       " .. Context.Dependencies:Status("EggState"),
                "PlotState      " .. Context.Dependencies:Status("PlotState"),
                "NestResolver   " .. Context.Dependencies:Status("NestResolver"),
            }, "\n")
            character:Set(Context.Character:IsReady() and "READY" or "WAITING",
                Context.Character:IsReady() and C.Theme.success or C.Theme.warning)
            auto:Set(Context.State:Get("autoStealState", "IDLE"))
        end
        window:OnPulse(refresh)
        window:Connect(Context.State.Changed, refresh)
        window:Connect(Context.Character.Changed, refresh)
        refresh()
    end
end
