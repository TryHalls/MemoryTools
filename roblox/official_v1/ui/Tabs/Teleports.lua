return function(Context)
    return function(page, window)
        local C = Context.Modules["ui/Components"]
        C.Label(page, "TELEPORTS", 34, "title")
        C.Button(page, "TP TO BASE", function()
            Context.Controllers.TeleportController:ToBase()
        end, C.Theme.accent)
        C.Label(page, "LOCAL TP - server may reconcile", 28, "muted")
        C.Section(page, "Area Teleports")
        local container = Instance.new("Frame")
        container.BackgroundTransparency = 1
        container.Size = UDim2.new(1, 0, 0, 46)
        container.AutomaticSize = Enum.AutomaticSize.Y
        container.Parent = page
        local layout = Instance.new("UIListLayout")
        layout.Padding = UDim.new(0, 8)
        layout.Parent = container

        local function refresh()
            for _, child in ipairs(container:GetChildren()) do
                if child:IsA("TextButton") or child:IsA("TextLabel") then child:Destroy() end
            end
            local areas, err = Context.Services.AreaService:GetResolvableAreas()
            if err then
                C.Label(container, "Areas unavailable: " .. tostring(err), 46, "muted")
                return
            end
            if #areas == 0 then
                C.Label(container, "No resolvable areas visible.", 46, "muted")
                return
            end
            for _, areaId in ipairs(areas) do
                local captured = areaId
                C.Button(container, "TP  •  " .. captured, function()
                    local ok, teleportError = Context.Controllers.TeleportController:ToArea(captured)
                    if not ok then Context.Logger:Error("Area TP: " .. tostring(teleportError)) end
                end)
            end
        end
        C.Button(page, "REFRESH AREAS", refresh)
        refresh()
    end
end
