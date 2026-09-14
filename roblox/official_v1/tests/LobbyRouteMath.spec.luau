local LobbyRouteMath = require("../core/LobbyRouteMath")({})

local function close(actual, expected, tolerance)
    assert(math.abs(actual - expected) <= (tolerance or 0.001), tostring(actual) .. " ~= " .. tostring(expected))
end

local function fixture()
    return {
        {
            name = "Part",
            minX = 552.0217, maxX = 560.5818,
            minY = 64, maxY = 104,
            minZ = -584.9282, maxZ = -439.6219,
        },
        {
            name = "Part",
            minX = 552.2561, maxX = 560.8162,
            minY = 64, maxY = 104,
            minZ = -283.8746, maxZ = -139.9589,
        },
        {
            name = "Part",
            minX = 400, maxX = 408,
            minY = 64, maxY = 104,
            minZ = -584.9282, maxZ = -139.9589,
        },
    }
end

local gate, gateError = LobbyRouteMath.DeriveEastGate(fixture(), 8)
assert(gateError == nil and gate ~= nil)
close(gate.gateMaxZ - gate.gateMinZ, 155.7473)
close(gate.gateCenterZ, -361.74825)
close(gate.innerX, 544.0217)
close(gate.outerX, 568.8162)
assert(gate.innerX < gate.eastInnerFace)
assert(gate.outerX > gate.eastOuterFace)

local inside = { x = 500, y = 67, z = gate.gateCenterZ }
local east = { x = 600, y = 70, z = gate.gateCenterZ }

local outward = assert(LobbyRouteMath.BuildRoute(inside, east, gate))
assert(outward.kind == "interior -> east exterior")
assert(#outward.waypoints == 2)
close(outward.waypoints[1].x, gate.innerX)
close(outward.waypoints[2].x, gate.outerX)
close(outward.waypoints[1].y, 70)

local inward = assert(LobbyRouteMath.BuildRoute(east, inside, gate))
assert(inward.kind == "east exterior -> interior")
assert(#inward.waypoints == 2)
close(inward.waypoints[1].x, gate.outerX)
close(inward.waypoints[2].x, gate.innerX)

local insideDirect = assert(LobbyRouteMath.BuildRoute(inside, { x = 480, y = 67, z = -350 }, gate))
assert(insideDirect.kind == "direct" and #insideDirect.waypoints == 0)
local eastDirect = assert(LobbyRouteMath.BuildRoute(east, { x = 700, y = 67, z = -350 }, gate))
assert(eastDirect.kind == "direct" and #eastDirect.waypoints == 0)

local noGap, noGapError = LobbyRouteMath.DeriveEastGate({
    { minX = 552, maxX = 560, minY = 64, maxY = 104, minZ = -585, maxZ = -300 },
    { minX = 552, maxX = 560, minY = 64, maxY = 104, minZ = -310, maxZ = -140 },
}, 8)
assert(noGap == nil and noGapError == "Lobby east gate could not be resolved")

print("LobbyRouteMath.spec: PASS")
