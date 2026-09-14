return function(Context)
    local FlightMath = {}

    FlightMath.DEFAULT_HORIZONTAL_SPEED = 140
    FlightMath.MAX_HORIZONTAL_SPEED = 150
    FlightMath.DEFAULT_ASCENT_SPEED = 90
    FlightMath.MAX_ASCENT_SPEED = 110
    FlightMath.DEFAULT_DESCENT_SPEED = 140
    FlightMath.DEFAULT_ARRIVAL_RADIUS = 5
    FlightMath.DEFAULT_VERTICAL_GAIN = 4
    FlightMath.DEFAULT_VERTICAL_TOLERANCE = 1
    FlightMath.BRAKING_DISTANCE = 20
    FlightMath.MIN_BRAKING_SPEED = 25

    local function finite(value)
        return type(value) == "number"
            and value == value
            and value ~= math.huge
            and value ~= -math.huge
    end

    function FlightMath.ClampFlightSpeed(value)
        value = tonumber(value)
        if not finite(value) then value = FlightMath.DEFAULT_HORIZONTAL_SPEED end
        return math.clamp(value, 30, FlightMath.MAX_HORIZONTAL_SPEED)
    end

    function FlightMath.ResolveOptions(options)
        options = type(options) == "table" and options or {}
        if options._flightMathResolved == true then return options end
        local horizontalSpeed = tonumber(options.horizontalSpeed)
        local ascentSpeed = tonumber(options.ascentSpeed)
        local descentSpeed = tonumber(options.descentSpeed)
        local arrivalRadius = tonumber(options.arrivalRadius)
        if not finite(horizontalSpeed) then horizontalSpeed = FlightMath.DEFAULT_HORIZONTAL_SPEED end
        if not finite(ascentSpeed) then ascentSpeed = FlightMath.DEFAULT_ASCENT_SPEED end
        if not finite(descentSpeed) then descentSpeed = FlightMath.DEFAULT_DESCENT_SPEED end
        if not finite(arrivalRadius) then arrivalRadius = FlightMath.DEFAULT_ARRIVAL_RADIUS end
        local verticalGain = tonumber(options.verticalGain)
        local verticalTolerance = tonumber(options.verticalTolerance)
        if not finite(verticalGain) then verticalGain = FlightMath.DEFAULT_VERTICAL_GAIN end
        if not finite(verticalTolerance) then verticalTolerance = FlightMath.DEFAULT_VERTICAL_TOLERANCE end
        return {
            _flightMathResolved = true,
            horizontalSpeed = FlightMath.ClampFlightSpeed(horizontalSpeed),
            ascentSpeed = math.clamp(ascentSpeed, 0, FlightMath.MAX_ASCENT_SPEED),
            descentSpeed = math.max(0, descentSpeed),
            arrivalRadius = math.max(0, arrivalRadius),
            verticalGain = math.max(0, verticalGain),
            verticalTolerance = math.max(0, verticalTolerance),
        }
    end

    function FlightMath.IsArrived(dx, dy, dz, arrivalRadius)
        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        return distance <= arrivalRadius, distance
    end

    function FlightMath.BrakedHorizontalSpeed(distance, horizontalSpeed)
        if distance <= 0 or horizontalSpeed <= 0 then return 0 end
        if distance >= FlightMath.BRAKING_DISTANCE then return horizontalSpeed end
        local scaled = horizontalSpeed * (distance / FlightMath.BRAKING_DISTANCE)
        return math.min(horizontalSpeed, math.max(math.min(FlightMath.MIN_BRAKING_SPEED, horizontalSpeed), scaled))
    end

    function FlightMath.DesiredVelocity(dx, dy, dz, options)
        local resolved = FlightMath.ResolveOptions(options)
        local horizontalDistance = math.sqrt(dx * dx + dz * dz)
        local horizontalSpeed = FlightMath.BrakedHorizontalSpeed(horizontalDistance, resolved.horizontalSpeed)
        local velocityX, velocityZ = 0, 0
        if horizontalDistance > 0 then
            velocityX = dx / horizontalDistance * horizontalSpeed
            velocityZ = dz / horizontalDistance * horizontalSpeed
        end

        local velocityY
        if dy > resolved.verticalTolerance then
            velocityY = math.min(dy * resolved.verticalGain, resolved.ascentSpeed)
        elseif dy < -resolved.verticalTolerance then
            velocityY = math.max(dy * resolved.verticalGain, -resolved.descentSpeed)
        else
            local stabilizationCap = math.min(8, resolved.ascentSpeed, resolved.descentSpeed)
            velocityY = math.clamp(dy * resolved.verticalGain, -stabilizationCap, stabilizationCap)
        end
        return velocityX, velocityY, velocityZ
    end

    function FlightMath.TerminationReason(state)
        if state.destroyed then return "MemoryTools is destroyed" end
        if state.cancelled then return state.cancelReason or "Flight movement cancelled" end
        if state.characterMissing then return "Character is not ready" end
        if state.dead then return "Humanoid is dead" end
        if state.cancelCheck then return "Flight movement cancelled" end
        if finite(state.elapsed) and finite(state.timeout) and state.elapsed >= state.timeout then
            return "Flight movement timed out"
        end
        if state.stalled then return "Flight movement stalled" end
        return nil
    end

    return FlightMath
end
