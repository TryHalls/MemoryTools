return function(Context)
    local LobbyRouteService = {}
    LobbyRouteService.__index = LobbyRouteService

    local Workspace = Context.Workspace or game:GetService("Workspace")
    local RouteMath = Context.Modules["core/LobbyRouteMath"]
    local typeOf = Context.Typeof or typeof
    local makeCFrame = Context.CFrameNew or CFrame.new
    local makeVector3 = Context.Vector3New or Vector3.new
    local zeroVector = Context.Vector3Zero or Vector3.zero
    if type(RouteMath) ~= "table" or type(RouteMath.DeriveEastGate) ~= "function" then
        error("LobbyRouteMath is not initialized")
    end

    local function copyOptions(options)
        local copy = {}
        for key, value in pairs(type(options) == "table" and options or {}) do copy[key] = value end
        return copy
    end

    local function resolveContainer()
        local objects = Workspace:FindFirstChild("__OBJECTS")
        local build = objects and objects:FindFirstChild("Build")
        local mainMap = build and build:FindFirstChild("MainMap")
        return mainMap and mainMap:FindFirstChild("LobbyBoundaries") or nil
    end

    local function worldBounds(part)
        if type(Context.LobbyBoundsForPart) == "function" then
            local bounds = Context.LobbyBoundsForPart(part)
            bounds.source = part
            return bounds
        end
        local half = part.Size * 0.5
        local minX, maxX, minY, maxY, minZ, maxZ
        for _, x in ipairs({ -half.X, half.X }) do
            for _, y in ipairs({ -half.Y, half.Y }) do
                for _, z in ipairs({ -half.Z, half.Z }) do
                    local corner = part.CFrame * makeVector3(x, y, z)
                    minX = minX and math.min(minX, corner.X) or corner.X
                    maxX = maxX and math.max(maxX, corner.X) or corner.X
                    minY = minY and math.min(minY, corner.Y) or corner.Y
                    maxY = maxY and math.max(maxY, corner.Y) or corner.Y
                    minZ = minZ and math.min(minZ, corner.Z) or corner.Z
                    maxZ = maxZ and math.max(maxZ, corner.Z) or corner.Z
                end
            end
        end
        return {
            minX = minX, maxX = maxX,
            minY = minY, maxY = maxY,
            minZ = minZ, maxZ = maxZ,
            source = part,
        }
    end

    function LobbyRouteService.new(context)
        return setmetatable({
            Context = context,
            Logger = context.Logger,
            SafetyMargin = RouteMath.DEFAULT_SAFETY_MARGIN,
            _gateCache = nil,
        }, LobbyRouteService)
    end

    function LobbyRouteService:_cacheValid()
        local cache = self._gateCache
        if not cache or not cache.container or not cache.container.Parent then return false end
        if resolveContainer() ~= cache.container then return false end
        for _, part in ipairs(cache.parts) do
            if not part or not part.Parent then return false end
        end
        return true
    end

    function LobbyRouteService:ResolveGate()
        if self:_cacheValid() then return self._gateCache.gate end
        self._gateCache = nil
        local container = resolveContainer()
        if not container then return nil, "Lobby east gate could not be resolved" end

        local walls = {}
        for _, descendant in ipairs(container:GetDescendants()) do
            if descendant:IsA("BasePart") then table.insert(walls, worldBounds(descendant)) end
        end
        local gate, err = RouteMath.DeriveEastGate(walls, self.SafetyMargin)
        if not gate then return nil, err end

        local parts = {}
        for _, wall in ipairs(walls) do table.insert(parts, wall.source) end
        self._gateCache = { gate = gate, container = container, parts = parts }
        self.Logger:Info(string.format("Lobby east gate resolved: center Z %.3f", gate.gateCenterZ))
        return gate
    end

    function LobbyRouteService:BuildRoute(startPosition, targetCFrame)
        if typeOf(targetCFrame) ~= "CFrame" then return nil, "Lobby route target is not a CFrame" end
        local gate, err = self:ResolveGate()
        if not gate then return nil, err end
        return RouteMath.BuildRoute(startPosition, targetCFrame.Position, gate)
    end

    function LobbyRouteService:TravelTo(targetCFrame, options)
        local root = self.Context.Character and self.Context.Character.Root
        if not root or not root.Parent or not root:IsA("BasePart") then
            return false, "Character is not ready"
        end
        local route, routeError = self:BuildRoute(root.Position, targetCFrame)
        if not route then return false, routeError end
        self.Logger:Info("Lobby route: " .. route.kind)

        for index, waypoint in ipairs(route.waypoints) do
            self.Context.State:Set("autoStealState", waypoint.state or (index == 1 and "ROUTE_TO_GATE_INNER" or "ROUTE_CROSS_GATE"))
            local legOptions = copyOptions(options)
            legOptions.offset = zeroVector
            legOptions.label = tostring(legOptions.label or "destination") .. " via lobby gate " .. tostring(index)
            local ok, result = self.Context.FlightMovement:TravelTo(
                makeCFrame(waypoint.x, waypoint.y, waypoint.z),
                legOptions
            )
            if not ok then return false, result end
        end

        self.Context.State:Set("autoStealState", "ROUTE_TO_TARGET")
        return self.Context.FlightMovement:TravelTo(targetCFrame, options)
    end

    function LobbyRouteService:Destroy()
        self._gateCache = nil
    end

    return LobbyRouteService
end
