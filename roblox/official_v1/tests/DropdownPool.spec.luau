local DropdownPoolFactory = require("../ui/DropdownPool")
local DropdownPool = DropdownPoolFactory({})

local records = {}
local created = 0
local connected = 0

local function createRecord()
    created = created + 1
    connected = connected + 1
    return {
        Id = created,
        Value = nil,
        Visible = false,
        Active = false,
        Text = "",
    }
end

local function updateRecord(record, visible, text)
    record.Visible = visible
    record.Active = visible
    record.Text = text
end

local ok, err = DropdownPool.Apply(records, { "A", "B" }, createRecord, updateRecord)
assert(ok, err)
assert(created == 2 and connected == 2)
assert(records[1].Value == "A" and records[2].Value == "B")

local first, second = records[1], records[2]
ok, err = DropdownPool.Apply(records, { "C" }, createRecord, updateRecord)
assert(ok, err)
assert(records[1] == first and records[2] == second)
assert(records[1].Value == "C" and records[1].Visible and records[1].Active)
assert(records[2].Value == nil and not records[2].Visible and not records[2].Active)
assert(created == 2 and connected == 2)

ok, err = DropdownPool.Apply(records, { "D", "E", "F" }, createRecord, updateRecord)
assert(ok, err)
assert(records[1] == first and records[2] == second)
assert(records[1].Value == "D" and records[2].Value == "E" and records[3].Value == "F")
assert(created == 3 and connected == 3)

ok, err = DropdownPool.Apply(records, "invalid", createRecord, updateRecord)
assert(ok == false and type(err) == "string")
assert(created == 3 and connected == 3)

print("DropdownPool.spec: PASS")
