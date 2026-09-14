-- MemoryTools Carry Runback Probe V1.0.6 GUI loader
local VERSION = "1.0.6-gui"
local BASE_URL = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/probe_parts/carry_runback_v1_0_6/"
local parts = {}
for index = 1, 9 do
    local url = BASE_URL .. string.format("part%02d.lua?v=%s", index, VERSION)
    local ok, source = pcall(function()
        return game:HttpGet(url)
    end)
    if not ok or type(source) ~= "string" or source == "" then
        error("Carry Runback Probe download failed for part " .. tostring(index) .. ": " .. tostring(source))
    end
    parts[index] = source
end
local source = table.concat(parts)
local chunk, compileError = loadstring(source)
if not chunk then
    error("Carry Runback Probe compile failed: " .. tostring(compileError))
end
return chunk()
