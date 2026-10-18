-- M0(a) stress: realistic hit = 13 stages, 40 attribute reads through native handles (read-through, not copies),
-- 52 status-effect checks (BR-RULESET-038), 3 third-party hooks on 'final'. Is pure Luau still inside budget?
local N = tonumber(ARGS[1]) or 100000
local attrs = {} for i = 1, 40 do attrs[i] = 10 + i end
-- native read-through handle: a ctx whose :get() stands in for a generation-checked attribute read
local h = ctx.new(100)
local statuses = {} for i = 1, 300 do statuses[i] = (i % 7 == 0) end   -- sparse active set
local watched = {} for i = 1, 52 do watched[i] = i * 5 end             -- the 52 statuses battle_calc_damage consults
local hooks = { function(c) c.x:mul_pct(100) end, function(c) end, function(c) c.x:add(0) end }
local function hit()
  local c = { x = ctx.new(1000) }
  for s = 1, 13 do
    for r = 1, 3 do local _ = h:get() end                              -- ~40 native reads per hit overall
    c.x:mul_pct(100 + (attrs[s] % 5))
  end
  local bonus = 0
  for i = 1, 52 do if statuses[watched[i]] then bonus = bonus + 1 end end -- status checks
  c.x:mul_pct(100 + bonus)
  for i = 1, #hooks do hooks[i](c) end                                  -- 3 hooks on one stage
  c.x:min1()
  return c.x:geti()
end
rng.seed(1)
local t0 = clock(); local sum = 0
for i = 1, N do sum = sum + hit() end
local dt = clock() - t0
print(string.format("heavy hit (13 stages, ~40 reads, 52 status checks, 3 hooks): %.2f us/hit, %d hits/s, interrupt=%s", dt / N * 1e6, N / dt, INTERRUPT_ON == 1 and "ON" or "off"))
print(string.format("AoE burst of 50 targets: %.2f ms;  bursts per 5 ms script budget: %.1f", dt / N * 50 * 1000, 5 / (dt / N * 50 * 1000)))
