from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

bootstrap = (ROOT / "bootstrap.lua").read_text(encoding="utf-8")
flight_math = 'local FlightMath = instantiate("core/FlightMath")'
flight_movement = 'local FlightMovement = instantiate("core/FlightMovement")'
route_math = 'local LobbyRouteMath = instantiate("core/LobbyRouteMath")'
assert flight_math in bootstrap
assert bootstrap.index(flight_math) < bootstrap.index(flight_movement)
assert route_math in bootstrap
assert '"core/LobbyRouteMath"' in bootstrap
assert '"game/LobbyRouteService"' in bootstrap

auto_steal = (ROOT / "features" / "AutoStealController.lua").read_text(encoding="utf-8")
assert ":WaitForManualCarry(" in auto_steal
assert "RequestCarryAndWait" not in auto_steal
assert "CarryFieldEgg" not in auto_steal
assert "AskFieldEggCarry" not in auto_steal
assert "Context.Services.LobbyRouteService:TravelTo(nestCFrame" in auto_steal
assert "Context.Services.LobbyRouteService:TravelTo(baseCFrame" in auto_steal
assert 'return result == "Humanoid is dead"' in auto_steal
fatal_stop = 'self:Stop("Auto Steal stopped: Humanoid is dead", true)'
retry_state = 'self:_state("RETRY")'
assert fatal_stop in auto_steal
assert auto_steal.index(fatal_stop) < auto_steal.index(retry_state)
assert "flightSpeed = 40" in auto_steal

teleports = (ROOT / "features" / "TeleportController.lua").read_text(encoding="utf-8")
fly_to_base = teleports[teleports.index("function TeleportController:FlyToBase()") :]
fly_to_base = fly_to_base[: fly_to_base.index("function TeleportController:ToArea")]
assert "Context.Services.LobbyRouteService:TravelTo" in fly_to_base
assert "Context.Teleport:To" not in fly_to_base

flight_movement_source = (ROOT / "core" / "FlightMovement.lua").read_text(encoding="utf-8")
assert "PivotTo" not in flight_movement_source
assert "Tween" not in flight_movement_source
assert ".CFrame =" not in flight_movement_source

route_service = (ROOT / "game" / "LobbyRouteService.lua").read_text(encoding="utf-8")
assert ".CanTouch =" not in route_service
assert ".CanCollide =" not in route_service
assert "FireServer" not in route_service
assert "InvokeServer" not in route_service
assert "GetDescendants()" in route_service
assert 'FindFirstChild("Part")' not in route_service

ui = (ROOT / "ui" / "Tabs" / "AutoSteal.lua").read_text(encoding="utf-8")
assert 'C.Status(page, "Carry Mode", "MANUAL")' in ui
assert "Carry Timeout" not in ui
assert 'C.NumberInput(page, "Flight Speed", 40' in ui

print("HotfixStructure.spec: PASS")
