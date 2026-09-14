        session.HealthConnection = healthConnection
    end
    return humanoid, rootPart
end

local function getAreaEvidence()
    local pieces = {}
    for _, instance in ipairs({ LocalPlayer, character, rootPart }) do
        if instance then
            local ok, attributes = pcall(function()
                return instance:GetAttributes()
            end)
            if ok then
                for key, value in pairs(attributes) do
                    local lower = string.lower(tostring(key))
                    if string.find(lower, "area", 1, true) or string.find(lower, "zone", 1, true) then
                        table.insert(pieces, instance.Name .. "." .. tostring(key) .. "=" .. clip(value, 80))
                    end
                end
            end
        end
    end
    table.sort(pieces)
    return #pieces > 0 and table.concat(pieces, ",") or "none"
end

local function getTags(instance)
    local ok, tags = pcall(function()
        return CollectionService:GetTags(instance)
    end)
    if not ok then
        return {}, "<error>"
    end
    table.sort(tags)
    return tags, table.concat(tags, ",")
end

local function containsIdentityWord(text)
    local lower = string.lower(text)
    return string.find(lower, "guard", 1, true)
        or string.find(lower, "animal", 1, true)
        or string.find(lower, "protector", 1, true)
        or string.find(lower, "enemy", 1, true)
end

local function candidateRoot(instance, model, candidateHumanoid)
    if instance:IsA("BasePart") then
        return instance
    end
    if model and model.PrimaryPart then
        return model.PrimaryPart
    end
    if candidateHumanoid and candidateHumanoid.RootPart then
        return candidateHumanoid.RootPart
    end
    if model then
        return model:FindFirstChild("HumanoidRootPart")
            or model:FindFirstChild("RootPart")
            or model:FindFirstChildWhichIsA("BasePart", true)
    end
    return nil
end

local function considerCandidate(instance, playerPosition, minimumEvidence)
    if instance == character or (character and instance:IsDescendantOf(character)) then
        return
    end
    local model = instance:IsA("Model") and instance or instance:FindFirstAncestorOfClass("Model")
    local candidateHumanoid = model and model:FindFirstChildOfClass("Humanoid") or nil
    local animation = model and model:FindFirstChildOfClass("AnimationController") or nil
    local part = candidateRoot(instance, model, candidateHumanoid)
    if not part then
        return
    end
    local distance = (part.Position - playerPosition).Magnitude
    if distance > SEARCH_RADIUS then
        return
    end
    local tags, tagText = getTags(instance)
    local modelTags = {}
    local modelTagText = ""
    if model then
        modelTags, modelTagText = getTags(model)
    end
    local identity = containsIdentityWord(instance.Name)
        or (model and containsIdentityWord(model.Name))
    for _, tag in ipairs(tags) do
        identity = identity or containsIdentityWord(tag)
    end
    for _, tag in ipairs(modelTags) do
        identity = identity or containsIdentityWord(tag)
    end
    local hasPrimary = model and model.PrimaryPart ~= nil
    local evidenceScore = identity and 4 or 0
    evidenceScore = evidenceScore + (candidateHumanoid and 3 or 0)
    evidenceScore = evidenceScore + (animation and 2 or 0)
    evidenceScore = evidenceScore + (hasPrimary and 1 or 0)
    if evidenceScore < minimumEvidence then
        return
    end
    local key = model or instance
    local item = candidates[key]
    if not item then
        if #candidateOrder >= MAX_CANDIDATES then
            return
        end
        item = {
            Instance = key,
            Root = part,
            Humanoid = candidateHumanoid,
            Tags = tagText ~= "" and tagText or modelTagText,
            Evidence = {},
            Samples = {},
            MinDistance = math.huge,
            MaxLinear = 0,
            SumLinear = 0,
            MaxDelta = 0,
            SumDelta = 0,
            DeltaCount = 0,
            Count = 0,
        }
        if identity then table.insert(item.Evidence, "name/tag") end
        if candidateHumanoid then table.insert(item.Evidence, "Humanoid") end
        if animation then table.insert(item.Evidence, "AnimationController") end
        if hasPrimary then table.insert(item.Evidence, "PrimaryPart") end
        candidates[key] = item
        table.insert(candidateOrder, item)
        terminalLine("GUARD", "Candidate discovered: " .. safeFullName(key) .. " evidence=" .. table.concat(item.Evidence, ","))
    end
end

local function discoverCandidates(playerPosition)
    local ok, nearby = pcall(function()
        return Workspace:GetPartBoundsInRadius(playerPosition, SEARCH_RADIUS)
    end)
    if not ok then
        return
    end
    local seenModels = {}
    for _, part in ipairs(nearby) do
        if containsIdentityWord(part.Name) then
            considerCandidate(part, playerPosition, 2)
        end
        local model = part:FindFirstAncestorOfClass("Model")
        if model and not seenModels[model] then
            seenModels[model] = true
        end
    end
    for model in pairs(seenModels) do
        considerCandidate(model, playerPosition, 2)
    end
    for model in pairs(seenModels) do
        considerCandidate(model, playerPosition, 1)
    end
end

local function sampleCandidates(now, playerPosition)
    for _, item in ipairs(candidateOrder) do
        local part = item.Root
        if part and part.Parent then
            local position = part.Position
            local distance = (position - playerPosition).Magnitude
            local linearSpeed = horizontalMagnitude(part.AssemblyLinearVelocity)
            local deltaSpeed = 0
            if item.PreviousPosition and item.PreviousTime then
                local dt = now - item.PreviousTime
                if dt > 0 then
