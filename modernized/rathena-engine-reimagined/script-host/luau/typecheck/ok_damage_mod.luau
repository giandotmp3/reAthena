--!strict
-- A mod written against api/engine.d.luau must type-check cleanly.
events.on("battle:damage", { id = "fire_brand", priority = 50, phase = "on", mode = "modify", after = { "base/weapon" } }, function(ctx)
  ctx:mul_pct(120)
  ctx:min1()
  local v: number = ctx:get()
  if v > 9999 then
    ctx:cancel("too much")
  end
  ctx:enqueue("mymod:apply_status", "target", "{}")
end)

storage.migrate(2, function(from: number, value: any)
  return 2, value
end)

local scope = storage.world()
scope:set("seen", 1)
local n = scope:get("seen")
local mine = storage.character(42):mod("mymod")
local ks: { string } = mine:keys()
say("hello")
local picked: number = choose({ "yes", "no" })
local name: string = input("name?")
wait(100)
timers.after(500, function() end)
definitions.declare("mymod:burn", { "duration", "tick" })
local r: number = rng.roll(100)
