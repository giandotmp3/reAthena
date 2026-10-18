--!strict
-- Wrong option types and a wrong move target must be reported.
pipeline.insert_after("weapon", "element", "mymod:x", function(ctx) end, { priority = "high" })
pipeline.move("weapon", "card", { after = 3 })
pipeline.cancel("weapon")
local n: number = pipeline.order("weapon")
local q: string = config.legacy_quirks
