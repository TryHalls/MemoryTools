return function(Context)
    local TeleportController = {}
    TeleportController.__index = TeleportController

    function TeleportController.new(context)
        return setmetatable({ Context = context }, TeleportController)
    end

    function TeleportController:ToBase()
        local cframe, err = self.Context.Services.PlotService:GetRespawnCFrame()
        if not cframe then
            self.Context.Logger:Error("TP to base: " .. tostring(err))
            return false, err
        end
        return self.Context.Teleport:To(cframe, "base", true)
    end

    function TeleportController:FlyToBase()
        local cframe, err = self.Context.Services.PlotService:GetRespawnCFrame()
        if not cframe then
            self.Context.Logger:Error("Fly to base: " .. tostring(err))
            return false, err
        end
        local ok, result = self.Context.Services.LobbyRouteService:TravelTo(cframe, { label = "base" })
        if not ok then self.Context.Logger:Error("Fly to base: " .. tostring(result)) end
        return ok, result
    end

    function TeleportController:ToArea(areaId)
        if type(areaId) ~= "string" or areaId == "" then
            return false, "Invalid AreaId"
        end
        local areas, err, records = self.Context.Services.AreaService:GetResolvableAreas()
        if err then return false, err end
        local found = false
        for _, value in ipairs(areas) do
            if value == areaId then found = true break end
        end
        if not found then return false, "Area is not currently resolvable" end
        local cframe, resolveError = self.Context.Services.AreaService:GetNestCFrame(records[areaId])
        if not cframe then return false, resolveError end
        return self.Context.Teleport:To(cframe, "area " .. areaId, true)
    end

    function TeleportController:Destroy() end
    return TeleportController
end
