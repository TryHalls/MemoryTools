local EggServiceFactory = require("../game/EggService")
local AreaServiceFactory = require("../game/AreaService")

local currentSnapshot
local eggState = {
    ReadFieldEggs = function()
        return currentSnapshot
    end,
    CarryFieldEgg = function() end,
}

local logger = {
    Error = function() end,
    Warn = function() end,
}

local context = {
    Logger = logger,
    Dependencies = {
        Get = function(_, name)
            if name == "EggState" then return eggState end
            return nil
        end,
    },
    Services = {},
}

local EggService = EggServiceFactory(context)
local eggService = EggService.new(context)
context.Services.EggService = eggService

currentSnapshot = {
    Records = {
        egg_a = {
            AreaId = "Area_B",
            NestId = "Slot_002",
            AssetCategory = "Moon",
        },
        egg_b = {
            Uid = "egg_b",
            AreaId = "Area_A",
            NestId = "Slot_001",
            AssetCategory = "Sun",
        },
    },
    Revision = 42,
}

local records, readError = eggService:ReadAll()
assert(readError == nil)
assert(#records == 2)
local byUid = {}
for _, record in ipairs(records) do byUid[record.Uid] = record end
assert(byUid.egg_a.AssetCategory == "Moon")
assert(byUid.egg_b.AreaId == "Area_A")

local AreaService = AreaServiceFactory(context)
local areaService = AreaService.new(context)
local areas, assets, areaError = areaService:Snapshot()
assert(areaError == nil)
assert(#areas == 2 and areas[1] == "Area_A" and areas[2] == "Area_B")
assert(#assets == 2 and assets[1] == "Moon" and assets[2] == "Sun")

currentSnapshot = { Records = {} }
records, readError = eggService:ReadAll()
assert(readError == nil and #records == 0)

currentSnapshot = {
    legacy_uid = {
        AreaId = "Legacy",
        NestId = "Slot_001",
        AssetCategory = "LegacyAsset",
    },
}
records, readError = eggService:ReadAll()
assert(readError == nil and #records == 1 and records[1].Uid == "legacy_uid")

currentSnapshot = { Records = "invalid" }
records, readError = eggService:ReadAll()
assert(records == nil and readError:find("snapshot.Records", 1, true) ~= nil)

print("EggAreaService.spec: PASS")
