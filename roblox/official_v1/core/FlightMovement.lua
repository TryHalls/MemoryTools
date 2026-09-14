return function(Context)
    local FlightMovement = {}
    FlightMovement.__index = FlightMovement

    local RunService = Context.RunService or game:GetService("RunService")
    local FlightMath = Context.Modules["core/FlightMath"]
    if type(FlightMath) ~= "table"
        or type(FlightMath.ResolveOptions) ~= "function"
        or type(FlightMath.DesiredVelocity) ~= "function" then
        error("FlightMath is not initialized")
    end

    local function disconnect(connection)
        if connection then pcall(function() connection:Disconnect() end) end
    end

    function FlightMovement.new(context)
        return setmetatable({
            Context = context,
            Logger = context.Logger,
            _travel = nil,
            _destroyed = false,
        }, FlightMovement)
    end

    function FlightMovement:IsActive()
        return self._travel ~= nil and not self._travel.cancelled and not self._travel.cleaned
    end

    function FlightMovement:_globalNoclipEnabled()
        local controller = self.Context.Controllers and self.Context.Controllers.PlayerController
        return controller ~= nil and controller.NoclipEnabled == true
    end

    function FlightMovement:_trackPart(travel, instance)
        if not instance or not instance:IsA("BasePart") then return end
        if travel.collisions[instance] == nil then
            travel.collisions[instance] = instance.CanCollide
        end
        if instance.CanCollide then instance.CanCollide = false end
    end

    function FlightMovement:_beginTemporaryState(travel, character, humanoid)
        travel.character = character
        travel.humanoid = humanoid
        travel.collisions = {}
        travel.autoRotate = humanoid.AutoRotate
        humanoid.AutoRotate = false
        for _, instance in ipairs(character:GetDescendants()) do
            self:_trackPart(travel, instance)
        end
        travel.descendantConnection = character.DescendantAdded:Connect(function(instance)
            if not travel.cleaned then self:_trackPart(travel, instance) end
        end)
    end

    function FlightMovement:_cleanupTravel(travel)
        if not travel or travel.cleaned then return end
        travel.cleaned = true
        disconnect(travel.descendantConnection)
        travel.descendantConnection = nil

        local keepNoclip = self:_globalNoclipEnabled()
        for part, original in pairs(travel.collisions or {}) do
            if part and part.Parent then
                pcall(function()
                    if keepNoclip then
                        part.CanCollide = false
                    else
                        part.CanCollide = original
                    end
                end)
            end
        end
        if travel.humanoid and travel.humanoid.Parent then
            pcall(function() travel.humanoid.AutoRotate = travel.autoRotate end)
        end
        local root = self.Context.Character and self.Context.Character.Root
        if root and root.Parent and root:IsA("BasePart") then
            pcall(function() root.AssemblyLinearVelocity = Vector3.zero end)
        end
        if self._travel == travel then self._travel = nil end
    end

    function FlightMovement:Cancel(reason)
        local travel = self._travel
        if not travel then return false end
        travel.cancelled = true
        travel.cancelReason = tostring(reason or "Flight movement cancelled")
        self:_cleanupTravel(travel)
        return true
    end

    function FlightMovement:TravelTo(targetCFrame, options)
        if typeof(targetCFrame) ~= "CFrame" then
            return false, "Flight target is not a CFrame"
        end
        options = type(options) == "table" and options or {}
        if options.offset ~= nil and typeof(options.offset) ~= "Vector3" then
            return false, "Flight offset is not a Vector3"
        end

        self:Cancel("Superseded by another TravelTo")
        if self._destroyed or self.Context.Destroyed then return false, "MemoryTools is destroyed" end

        local characterState = self.Context.Character
        local character, humanoid, root = characterState.Model, characterState.Humanoid, characterState.Root
        if not character or not character.Parent or not humanoid or not humanoid.Parent
            or not root or not root.Parent or not root:IsA("BasePart") then
            return false, "Character is not ready"
        end
        if humanoid.Health <= 0 then return false, "Humanoid is dead" end

        local resolved = FlightMath.ResolveOptions(options)
        local offset = options.offset or Vector3.new(0, 3, 0)
        local destination = targetCFrame.Position + offset
        local startPosition = root.Position
        local startDistance = (destination - startPosition).Magnitude
        local timeout = tonumber(options.timeout)
        if not timeout or timeout ~= timeout or timeout == math.huge or timeout == -math.huge or timeout <= 0 then
            timeout = math.max(10, startDistance / 25 + 8)
        end
        local label = tostring(options.label or "destination")
        local startedAt = os.clock()
        local travel = {
            cancelled = false,
            cleaned = false,
            cancelReason = nil,
        }
        self._travel = travel

        local began, beginError = pcall(function()
            self:_beginTemporaryState(travel, character, humanoid)
        end)
        if not began then
            self:_cleanupTravel(travel)
            return false, "Flight setup failed: " .. tostring(beginError)
        end

        local bestDistance = startDistance
        local lastProgressCheck = startedAt
        local stalledChecks = 0
        local finalDistance = startDistance
        local success = false
        local failure

        local ran, runtimeError = xpcall(function()
            while true do
                RunService.Heartbeat:Wait()
                local now = os.clock()
                local currentCharacter = characterState.Model
                local currentHumanoid = characterState.Humanoid
                local currentRoot = characterState.Root
                local missing = currentCharacter ~= character or currentHumanoid ~= humanoid
                    or currentRoot ~= root or not character.Parent or not humanoid.Parent
                    or not root.Parent or not root:IsA("BasePart")

                local cancelRequested = false
                if type(options.cancelCheck) == "function" and not travel.cancelled then
                    local checked, result = pcall(options.cancelCheck)
                    if not checked then
                        failure = "Flight cancelCheck failed: " .. tostring(result)
                        break
                    end
                    cancelRequested = result == true
                end

                failure = FlightMath.TerminationReason({
                    destroyed = self._destroyed or self.Context.Destroyed,
                    cancelled = travel.cancelled,
                    cancelReason = travel.cancelReason,
                    characterMissing = missing,
                    dead = not missing and humanoid.Health <= 0,
                    cancelCheck = cancelRequested,
                    elapsed = now - startedAt,
                    timeout = timeout,
                })
                if failure then break end

                local delta = destination - root.Position
                local arrived
                arrived, finalDistance = FlightMath.IsArrived(delta.X, delta.Y, delta.Z, resolved.arrivalRadius)
                if arrived then
                    root.AssemblyLinearVelocity = Vector3.zero
                    success = true
                    break
                end

                if now - lastProgressCheck >= 0.1 then
                    if finalDistance < bestDistance - 0.5 then
                        bestDistance = finalDistance
                        stalledChecks = 0
                    else
                        stalledChecks = stalledChecks + 1
                    end
                    lastProgressCheck = now
                    if stalledChecks >= 15 then
                        failure = "Flight movement stalled"
                        break
                    end
                end

                for part in pairs(travel.collisions) do
                    if part and part.Parent and part.CanCollide then part.CanCollide = false end
                end
                local velocityX, velocityY, velocityZ = FlightMath.DesiredVelocity(delta.X, delta.Y, delta.Z, resolved)
                root.AssemblyLinearVelocity = Vector3.new(velocityX, velocityY, velocityZ)
            end
        end, function(err)
            if type(debug) == "table" and type(debug.traceback) == "function" then
                return debug.traceback(tostring(err), 2)
            end
            return tostring(err)
        end)

        local duration = os.clock() - startedAt
        self:_cleanupTravel(travel)
        if not ran then return false, "Flight movement failed: " .. tostring(runtimeError) end
        if not success then return false, failure or "Flight movement cancelled" end

        local details = {
            finalDistance = finalDistance,
            startDistance = startDistance,
            duration = duration,
            averageSpeed = duration > 0 and math.max(0, startDistance - finalDistance) / duration or 0,
            destination = destination,
            label = label,
        }
        self.Logger:Action("Flight arrived", label)
        return true, details
    end

    function FlightMovement:Destroy()
        self._destroyed = true
        self:Cancel("Flight movement destroyed")
    end

    return FlightMovement
end
