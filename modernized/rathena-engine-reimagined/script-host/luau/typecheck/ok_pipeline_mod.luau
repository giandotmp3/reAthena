--!strict
-- A mod written against the pipeline API in api/engine.d.luau must type-check cleanly.
pipeline.insert_after("weapon", "element", "mymod:fire_brand", function(ctx)
  ctx:mul_pct(110)
  local snap = ctx:view()
  if snap ~= nil and ctx:get() > 9999 then
    ctx:cancel("too much")
  end
end, { priority = 50, mode = "modify" })
pipeline.insert_before("weapon", "final", "mymod:audit", function(ctx) ctx:enqueue("mymod:log", "target") end)
pipeline.replace("weapon", "defense", "mymod:flat_def", function(ctx) ctx:set(1) end, { precedence = true })
pipeline.cancel("weapon", "size_race", { optional = true })
pipeline.move("weapon", "card_attacker", { after = "base_attack" }, { priority = 1 })

local steps, meta = pipeline.describe("weapon")
local first: string = steps[1].stage
local who: string = steps[1].edits[1].mod
local owner: string = meta.owner
local ids: { string } = pipeline.order("weapon")
local q: boolean = config.legacy_quirks
local r: number = num.f32(0.1)
