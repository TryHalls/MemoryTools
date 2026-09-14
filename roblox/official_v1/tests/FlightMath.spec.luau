local FlightMath = require("../core/FlightMath")({})

assert(FlightMath.ClampFlightSpeed(10) == 30)
assert(FlightMath.ClampFlightSpeed(140) == 140)
assert(FlightMath.ClampFlightSpeed(999) == 150)
assert(FlightMath.ClampFlightSpeed(0 / 0) == 140)
assert(FlightMath.ResolveOptions({ horizontalSpeed = 1 }).horizontalSpeed == 30)
assert(FlightMath.ResolveOptions({ horizontalSpeed = 151 }).horizontalSpeed == 150)

local arrived, distance = FlightMath.IsArrived(3, 0, 4, 5)
assert(arrived and distance == 5)
assert(not FlightMath.IsArrived(3.1, 0, 4, 5))

assert(FlightMath.BrakedHorizontalSpeed(25, 140) == 140)
assert(FlightMath.BrakedHorizontalSpeed(10, 140) == 70)
assert(FlightMath.BrakedHorizontalSpeed(1, 140) == 25)
assert(FlightMath.BrakedHorizontalSpeed(1, 20) == 20)

local vx, vy, vz = FlightMath.DesiredVelocity(30, 100, 40, {
    horizontalSpeed = 140,
    ascentSpeed = 999,
})
assert(math.abs(vx - 84) < 0.001 and math.abs(vz - 112) < 0.001)
assert(vy == 110)

vx, vy, vz = FlightMath.DesiredVelocity(0, -100, 0, { descentSpeed = 140 })
assert(vx == 0 and vz == 0 and vy == -140)
vx, vy, vz = FlightMath.DesiredVelocity(0, 0.5, 0, {})
assert(vy == 2)

print("FlightMath.spec: PASS")
