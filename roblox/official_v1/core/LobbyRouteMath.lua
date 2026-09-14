return function(_Context)
    local LobbyRouteMath = {}

    LobbyRouteMath.DEFAULT_SAFETY_MARGIN = 8
    LobbyRouteMath.MIN_USABLE_GATE_WIDTH = 20
    LobbyRouteMath.DEFAULT_ARRIVAL_RADIUS = 6

    local function finite(value)
        return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
    end

    local function coordinate(value, upper, lower)
        if type(value) ~= "table" and typeof(value) ~= "Vector3" then return nil end
        local result = value[upper]
        if result == nil then result = value[lower] end
        return finite(result) and result or nil
    end

    local function point(value)
        local x = coordinate(value, "X", "x")
        local y = coordinate(value, "Y", "y")
        local z = coordinate(value, "Z", "z")
        if not x or not y or not z then return nil end
        return { x = x, y = y, z = z }
    end

    local function copyWall(wall, index)
        if type(wall) ~= "table" then return nil end
        local keys = { "minX", "maxX", "minY", "maxY", "minZ", "maxZ" }
        local copy = { source = wall.source, sourceIndex = index }
        for _, key in ipairs(keys) do
            if not finite(wall[key]) then return nil end
            copy[key] = wall[key]
        end
        if copy.minX > copy.maxX or copy.minY > copy.maxY or copy.minZ > copy.maxZ then return nil end
        return copy
    end

    local function wallShape(wall)
        local width = wall.maxX - wall.minX
        local height = wall.maxY - wall.minY
        local depth = wall.maxZ - wall.minZ
        local significantHeight = height >= 8 and height >= math.min(width, depth) * 1.5
        local thinX = depth >= 20 and depth >= width * 2
        local thinZ = width >= 20 and width >= depth * 2
        return significantHeight and (thinX or thinZ), thinX
    end

    local function groupByX(candidates)
        table.sort(candidates, function(a, b)
            return (a.minX + a.maxX) * 0.5 < (b.minX + b.maxX) * 0.5
        end)
        local groups = {}
        for _, wall in ipairs(candidates) do
            local center = (wall.minX + wall.maxX) * 0.5
            local thickness = wall.maxX - wall.minX
            local selected
            for _, group in ipairs(groups) do
                local tolerance = math.max(2, group.maxThickness, thickness)
                if math.abs(center - group.centerX) <= tolerance then
                    selected = group
                    break
                end
            end
            if not selected then
                selected = { walls = {}, centerX = center, maxThickness = thickness }
                table.insert(groups, selected)
            end
            table.insert(selected.walls, wall)
            selected.maxThickness = math.max(selected.maxThickness, thickness)
            local total = 0
            for _, member in ipairs(selected.walls) do
                total = total + (member.minX + member.maxX) * 0.5
            end
            selected.centerX = total / #selected.walls
        end
        return groups
    end

    function LobbyRouteMath.DeriveEastGate(walls, safetyMargin)
        if type(walls) ~= "table" then return nil, "Lobby east gate could not be resolved" end
        safetyMargin = tonumber(safetyMargin)
        if not finite(safetyMargin) or safetyMargin < 0 then
            safetyMargin = LobbyRouteMath.DEFAULT_SAFETY_MARGIN
        end

        local valid, candidates = {}, {}
        for index, wall in ipairs(walls) do
            local copy = copyWall(wall, index)
            if copy then
                table.insert(valid, copy)
                local vertical, thinX = wallShape(copy)
                if vertical then
                    copy.thinX = thinX
                    if thinX then table.insert(candidates, copy) end
                end
            end
        end
        if #candidates < 2 then return nil, "Lobby east gate could not be resolved" end

        local groups = groupByX(candidates)
        local eastGroup
        for _, group in ipairs(groups) do
            if not eastGroup or group.centerX > eastGroup.centerX then eastGroup = group end
        end
        if not eastGroup or #eastGroup.walls < 2 then
            return nil, "Lobby east gate could not be resolved"
        end

        local intervals = {}
        local eastInnerFace, eastOuterFace, minimumEastWallY
        for _, wall in ipairs(eastGroup.walls) do
            table.insert(intervals, { minZ = wall.minZ, maxZ = wall.maxZ })
            eastInnerFace = eastInnerFace and math.min(eastInnerFace, wall.minX) or wall.minX
            eastOuterFace = eastOuterFace and math.max(eastOuterFace, wall.maxX) or wall.maxX
            minimumEastWallY = minimumEastWallY and math.min(minimumEastWallY, wall.minY) or wall.minY
        end
        table.sort(intervals, function(a, b) return a.minZ < b.minZ end)

        local merged = {}
        for _, interval in ipairs(intervals) do
            local last = merged[#merged]
            if last and interval.minZ <= last.maxZ then
                last.maxZ = math.max(last.maxZ, interval.maxZ)
            else
                table.insert(merged, { minZ = interval.minZ, maxZ = interval.maxZ })
            end
        end

        local gateMinZ, gateMaxZ, gapWidth
        for index = 1, #merged - 1 do
            local gapMin = merged[index].maxZ
            local gapMax = merged[index + 1].minZ
            local width = gapMax - gapMin
            if width > 0 and (not gapWidth or width > gapWidth) then
                gateMinZ, gateMaxZ, gapWidth = gapMin, gapMax, width
            end
        end
        if not gapWidth or gapWidth - safetyMargin * 2 <= LobbyRouteMath.MIN_USABLE_GATE_WIDTH then
            return nil, "Lobby east gate could not be resolved"
        end

        local lobbyMinX, lobbyMaxX, lobbyMinZ, lobbyMaxZ
        for _, wall in ipairs(valid) do
            local vertical = wallShape(wall)
            if vertical then
                lobbyMinX = lobbyMinX and math.min(lobbyMinX, wall.minX) or wall.minX
                lobbyMaxX = lobbyMaxX and math.max(lobbyMaxX, wall.maxX) or wall.maxX
                lobbyMinZ = lobbyMinZ and math.min(lobbyMinZ, wall.minZ) or wall.minZ
                lobbyMaxZ = lobbyMaxZ and math.max(lobbyMaxZ, wall.maxZ) or wall.maxZ
            end
        end

        return {
            gateMinZ = gateMinZ,
            gateMaxZ = gateMaxZ,
            gateCenterZ = (gateMinZ + gateMaxZ) * 0.5,
            usableMinZ = gateMinZ + safetyMargin,
            usableMaxZ = gateMaxZ - safetyMargin,
            usableWidth = gapWidth - safetyMargin * 2,
            eastInnerFace = eastInnerFace,
            eastOuterFace = eastOuterFace,
            innerX = eastInnerFace - safetyMargin,
            outerX = eastOuterFace + safetyMargin,
            minimumEastWallY = minimumEastWallY,
            lobbyMinX = lobbyMinX,
            lobbyMaxX = lobbyMaxX,
            lobbyMinZ = lobbyMinZ,
            lobbyMaxZ = lobbyMaxZ,
            safetyMargin = safetyMargin,
            sourceWalls = eastGroup.walls,
        }
    end

    function LobbyRouteMath.ClassifyPoint(position, gate)
        local value = point(position)
        if not value or type(gate) ~= "table" then return "UNCLASSIFIED" end
        if value.x > gate.outerX then return "EAST_EXTERIOR" end

        local margin = gate.safetyMargin or LobbyRouteMath.DEFAULT_SAFETY_MARGIN
        local westBound = gate.lobbyMinX and gate.lobbyMinX + margin or nil
        local northBound = gate.lobbyMinZ and gate.lobbyMinZ + margin or nil
        local southBound = gate.lobbyMaxZ and gate.lobbyMaxZ - margin or nil
        local hasLobbyWidth = westBound and westBound < gate.innerX
        if hasLobbyWidth and northBound and southBound
            and value.x >= westBound and value.x <= gate.innerX
            and value.z >= northBound and value.z <= southBound then
            return "INTERIOR"
        end
        return "UNCLASSIFIED"
    end

    local function distance(a, b)
        local dx, dy, dz = a.x - b.x, a.y - b.y, a.z - b.z
        return math.sqrt(dx * dx + dy * dy + dz * dz)
    end

    function LobbyRouteMath.BuildRoute(startPosition, targetPosition, gate, arrivalRadius)
        local start = point(startPosition)
        local target = point(targetPosition)
        if not start or not target or type(gate) ~= "table" then
            return nil, "Lobby route input is invalid"
        end
        local startClass = LobbyRouteMath.ClassifyPoint(start, gate)
        local targetClass = LobbyRouteMath.ClassifyPoint(target, gate)
        if startClass == targetClass and startClass ~= "UNCLASSIFIED" then
            return { kind = "direct", waypoints = {}, startClass = startClass, targetClass = targetClass }
        end

        local direction
        if startClass == "INTERIOR" and targetClass == "EAST_EXTERIOR" then
            direction = "interior -> east exterior"
        elseif startClass == "EAST_EXTERIOR" and targetClass == "INTERIOR" then
            direction = "east exterior -> interior"
        else
            local crossesEastWall = (start.x <= gate.eastInnerFace and target.x > gate.outerX)
                or (target.x <= gate.eastInnerFace and start.x > gate.outerX)
            if crossesEastWall then
                return nil, "Lobby boundary crossing could not be classified safely"
            end
            return {
                kind = "direct",
                waypoints = {},
                startClass = startClass,
                targetClass = targetClass,
                diagnostic = "unclassified",
            }
        end

        local gateY = math.max(start.y, target.y, gate.minimumEastWallY + 3)
        local inner = { x = gate.innerX, y = gateY, z = gate.gateCenterZ, state = "ROUTE_TO_GATE_INNER" }
        local outer = { x = gate.outerX, y = gateY, z = gate.gateCenterZ, state = "ROUTE_TO_GATE_OUTER" }
        local ordered = direction == "interior -> east exterior" and { inner, outer } or { outer, inner }
        ordered[2].state = "ROUTE_CROSS_GATE"
        local radius = tonumber(arrivalRadius) or LobbyRouteMath.DEFAULT_ARRIVAL_RADIUS
        local waypoints = {}
        for _, waypoint in ipairs(ordered) do
            if distance(start, waypoint) > radius then table.insert(waypoints, waypoint) end
        end
        return {
            kind = direction,
            waypoints = waypoints,
            startClass = startClass,
            targetClass = targetClass,
            gateY = gateY,
        }
    end

    return LobbyRouteMath
end
