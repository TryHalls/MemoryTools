-- Roblox Client Scanner V1.1 GUI loader
local BASE = "https://raw.githubusercontent.com/TryHalls/MemoryTools/main/roblox/scanner_v1_parts/part"
local chunks = {}

for i = 1, 5 do
    local ok, result = pcall(function()
        return game:HttpGet(BASE .. tostring(i) .. ".lua.txt")
    end)

    if not ok or type(result) ~= "string" or #result == 0 then
        error("[Scanner V1] No se pudo descargar part" .. tostring(i) .. ": " .. tostring(result))
    end

    chunks[i] = result
end

local source = table.concat(chunks, "\n")
local compiled, compileError = loadstring(source)

if not compiled then
    error("[Scanner V1] Error compilando la GUI: " .. tostring(compileError))
end

return compiled()
