from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

bootstrap = (ROOT / "bootstrap.lua").read_text(encoding="utf-8")
flight_math = 'local FlightMath = instantiate("core/FlightMath")'
flight_movement = 'local FlightMovement = instantiate("core/FlightMovement")'
assert flight_math in bootstrap
assert bootstrap.index(flight_math) < bootstrap.index(flight_movement)

auto_steal = (ROOT / "features" / "AutoStealController.lua").read_text(encoding="utf-8")
assert ":WaitForManualCarry(" in auto_steal
assert "RequestCarryAndWait" not in auto_steal
assert "CarryFieldEgg" not in auto_steal

ui = (ROOT / "ui" / "Tabs" / "AutoSteal.lua").read_text(encoding="utf-8")
assert 'C.Status(page, "Carry Mode", "MANUAL")' in ui
assert "Carry Timeout" not in ui

print("HotfixStructure.spec: PASS")
