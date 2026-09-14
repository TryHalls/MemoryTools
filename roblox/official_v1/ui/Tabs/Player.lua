return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        local controller = Context.Controllers.PlayerController
        local playerService = Context.Services.PlayerService
        local staffService = Context.Services.StaffService
        local snapshot = playerService:Snapshot()

        C.Label(page, "PLAYER", 34, "title")
        C.Section(page, "Server Movement")
        local staffStatus = C.Status(page, "Staff Status", Context.State:Get("staffStatus", "UNKNOWN"))
        C.Button(page, "PROBE STAFF", function()
            local ok, result = staffService:Probe(5)
            if not ok then Context.Logger:Warn("Staff probe: " .. tostring(result)) end
        end, C.Theme.accent)

        local pendingWalkSpeed = snapshot.walkSpeed
        local serverWalkSpeed = C.NumberInput(page, "WalkSpeed", pendingWalkSpeed, function(value)
            pendingWalkSpeed = value
            return true
        end)
        C.Button(page, "APPLY SERVER — WALKSPEED", function()
            local value = tonumber(serverWalkSpeed.Text) or pendingWalkSpeed
            local ok, err = staffService:SetWalkSpeed(value)
            if not ok then Context.Logger:Warn("Server WalkSpeed: " .. tostring(err)) end
        end)

        local speedDetails = snapshot.speedPower or {}
        local pendingSpeedPower = speedDetails.value
        local serverSpeedPower = C.NumberInput(page, "SpeedPower", pendingSpeedPower, function(value)
            pendingSpeedPower = value
            return true
        end)
        C.Button(page, "APPLY SERVER — SPEEDPOWER", function()
            local value = tonumber(serverSpeedPower.Text) or pendingSpeedPower
            local ok, err = staffService:SetSpeedPower(value)
            if not ok then Context.Logger:Warn("Server SpeedPower: " .. tostring(err)) end
        end)
        local speedPower = C.Label(page, "SpeedPower: UNKNOWN", 58, "mono")

        C.Section(page, "Local Debug Override")
        C.Label(page, "Client-side only; the server may reconcile these values.", 34, "muted")
        C.NumberInput(page, "Local WalkSpeed Override", snapshot.walkSpeed, function(value)
            return controller:SetWalkSpeed(value)
        end)
        local walkToggle = C.Toggle(page, "Enable Local WalkSpeed Override", false, function(enabled)
            local ok, err = controller:EnableWalkSpeed(enabled)
            if ok == false then Context.Logger:Warn(err) end
        end)
        C.NumberInput(page, "Local JumpPower", snapshot.jumpPower, function(value)
            return controller:SetJumpPower(value)
        end)
        C.NumberInput(page, "Local JumpHeight", snapshot.jumpHeight, function(value)
            return controller:SetJumpHeight(value)
        end)
        C.NumberInput(page, "Local Gravity", snapshot.gravity, function(value)
            return controller:SetGravity(value)
        end)
        local jumpToggle = C.Toggle(page, "Infinite Jump", false, function(enabled)
            controller:EnableInfiniteJump(enabled)
        end)
        local noclipToggle = C.Toggle(page, "Noclip", false, function(enabled)
            controller:EnableNoclip(enabled)
        end)
        local freezeToggle = C.Toggle(page, "Freeze", false, function(enabled)
            local ok, err = controller:EnableFreeze(enabled)
            if ok == false then Context.Logger:Warn(err) end
        end)
        local reapplyToggle = C.Toggle(page, "Reapply After Respawn", controller.ReapplyAfterRespawn, function(enabled)
            controller:SetReapply(enabled)
        end)
        C.Button(page, "TP TO BASE", function()
            Context.Controllers.TeleportController:ToBase()
        end, C.Theme.accent)
        C.Button(page, "RESET LOCAL MOVEMENT", function()
            controller:ResetMovement()
        end, C.Theme.danger)

        local function refreshState()
            local status = Context.State:Get("staffStatus", "UNKNOWN")
            local color = status == "AUTHORIZED" and C.Theme.success
                or (status == "DENIED" and C.Theme.danger or C.Theme.warning)
            staffStatus:Set(status, color)
            walkToggle:Set(Context.State:Get("walkSpeedEnabled", false), true)
            jumpToggle:Set(Context.State:Get("infiniteJumpEnabled", false), true)
            noclipToggle:Set(Context.State:Get("noclipEnabled", false), true)
            freezeToggle:Set(Context.State:Get("freezeEnabled", false), true)
            reapplyToggle:Set(Context.State:Get("reapplyPlayerSettings", true), true)
        end
        window:Connect(Context.State.Changed, refreshState)
        refreshState()
        window:OnPulse(function()
            local details = playerService:ReadSpeedPower()
            speedPower.Text = "Current SpeedPower (Save): " .. tostring(details.value or "UNKNOWN")
                .. "\nEqualised: " .. tostring(details.equalised or "UNKNOWN")
                .. "  Minimum: " .. tostring(details.minimum or "UNKNOWN")
        end)
    end
end
