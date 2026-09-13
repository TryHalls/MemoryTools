return function(Context)
    local AreaService = {}
    AreaService.__index = AreaService

    function AreaService.new(context)
        return setmetatable({
            Context = context,
            Logger = context.Logger,
            EggService = context.Services.EggService,
        }, AreaService)
    end

    function AreaService:ResolveNest(record)
        local resolver = self.Context.Dependencies:Get("NestResolver")
        if not resolver or type(resolver.Resolve) ~= "function" then
            return nil, "NestResolver.Resolve unavailable"
        end
        local ok, nest = pcall(resolver.Resolve, record)
        if not ok then
            return nil, tostring(nest)
        end
        if typeof(nest) ~= "Instance" or not nest.Parent then
            return nil, "Nest is not available"
        end
        return nest
    end

    function AreaService:GetNestCFrame(record)
        local nest, err = self:ResolveNest(record)
        if not nest then return nil, err end
        local ok, pivot = pcall(function()
            if nest:IsA("Model") then
                return nest:GetPivot()
            elseif nest:IsA("BasePart") then
                return nest.CFrame
            end
            return nest:GetPivot()
        end)
        if not ok or typeof(pivot) ~= "CFrame" then
            return nil, ok and "Nest has no usable pivot" or tostring(pivot)
        end
        return pivot, nest
    end

    function AreaService:Snapshot()
        local eggs, err = self.EggService:ReadAll()
        if not eggs then return nil, nil, err end
        local areaSet, assetSet = {}, {}
        for _, record in ipairs(eggs) do
            if type(record.AreaId) == "string" and record.AreaId ~= "" then
                areaSet[record.AreaId] = true
            end
            if type(record.AssetCategory) == "string" and record.AssetCategory ~= "" then
                assetSet[record.AssetCategory] = true
            end
        end
        local areas, assets = {}, {}
        for value in pairs(areaSet) do table.insert(areas, value) end
        for value in pairs(assetSet) do table.insert(assets, value) end
        table.sort(areas)
        table.sort(assets)
        return areas, assets, nil, eggs
    end

    function AreaService:GetResolvableAreas()
        local areas, _, err, eggs = self:Snapshot()
        if not areas then return {}, err end
        local resolved, records = {}, {}
        for _, record in ipairs(eggs) do
            local areaId = record.AreaId
            if areaId and not resolved[areaId] then
                local pivot = self:GetNestCFrame(record)
                if pivot then
                    resolved[areaId] = true
                    records[areaId] = record
                end
            end
        end
        local list = {}
        for _, areaId in ipairs(areas) do
            if resolved[areaId] then table.insert(list, areaId) end
        end
        return list, nil, records
    end

    function AreaService:Destroy() end
    return AreaService
end
