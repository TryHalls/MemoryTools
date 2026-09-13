return function(Context)
    local PlotService = {}
    PlotService.__index = PlotService

    function PlotService.new(context)
        return setmetatable({ Context = context, Logger = context.Logger }, PlotService)
    end

    function PlotService:Resolve()
        local module = self.Context.Dependencies:Get("PlotState")
        if not module or type(module.ResolvePlot) ~= "function" then
            return nil, "PlotState.ResolvePlot unavailable"
        end
        local ok, plot = pcall(module.ResolvePlot)
        if not ok then
            self.Logger:Error("ResolvePlot failed: " .. tostring(plot))
            return nil, tostring(plot)
        end
        if type(plot) ~= "table" then
            return nil, "Local plot is not ready"
        end
        return plot
    end

    function PlotService:GetRespawnCFrame()
        local plot, err = self:Resolve()
        if not plot then return nil, err end
        if typeof(plot.RespawnPointCFrame) ~= "CFrame" then
            return nil, "Plot RespawnPointCFrame is unavailable"
        end
        return plot.RespawnPointCFrame
    end

    function PlotService:Destroy() end
    return PlotService
end
