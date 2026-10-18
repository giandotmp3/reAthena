-- M0(a): cost of one weapon hit through a 9-stage pipeline, three implementations.
-- Stages mirror BR-RULESET-018 (PRE): init, base, ratio, defense, refine, element, card x2, target mods, final.
local N = tonumber(ARGS[1]) or 200000
local ELE = { [0]=100, 150, 50, 100 }                -- tiny element row, data-driven like the real table
local atk = { min = 800, max = 1200, batk = 150, ratio = 300, def = 35, vit = 40, ele = 150, c1 = 120, c2 = 115, tmod = 90 }

-- A: pure Luau on doubles (table ctx, math.floor for truncation)
local function mkA()
  local S = {}
  S[1] = function(c) c.d = c.a.min + rng.rnd(c.a.max - c.a.min) end
  S[2] = function(c) c.d = c.d + c.a.batk end
  S[3] = function(c) c.d = c.d * c.a.ratio // 100 end
  S[4] = function(c) c.d = c.d * (100 - c.a.def) // 100 - c.a.vit end
  S[5] = function(c) if c.d < 1 then c.d = 1 end end
  S[6] = function(c) c.d = c.d * ELE[1] // 100 end
  S[7] = function(c) c.d = c.d * c.a.c1 // 100 end
  S[8] = function(c) c.d = c.d * c.a.c2 // 100 end
  S[9] = function(c) c.d = c.d * c.a.tmod // 100; if c.d < 1 then c.d = 1 end end
  return S
end
-- B: Luau stages driving the native int64 ctx through methods
local function mkB()
  local S = {}
  S[1] = function(c) c.x:set(c.a.min + rng.rnd(c.a.max - c.a.min)) end
  S[2] = function(c) c.x:add(c.a.batk) end
  S[3] = function(c) c.x:mul_pct(c.a.ratio) end
  S[4] = function(c) c.x:def_pct(c.a.def, c.a.vit) end
  S[5] = function(c) c.x:min1() end
  S[6] = function(c) c.x:mul_pct(ELE[1]) end
  S[7] = function(c) c.x:mul_pct(c.a.c1) end
  S[8] = function(c) c.x:mul_pct(c.a.c2) end
  S[9] = function(c) c.x:mul_pct(c.a.tmod); c.x:min1() end
  return S
end

local function bench(name, mk, mkctx, getv)
  local S = mk(); local n = #S
  rng.seed(12345)
  local t0 = clock(); local sum = 0
  for i = 1, N do
    local c = mkctx()
    for s = 1, n do S[s](c) end
    sum = sum + getv(c)
  end
  local dt = clock() - t0
  print(string.format("%-34s %8.3f us/hit  %9.0f hits/s  (checksum %d, rng draws %d)", name, dt / N * 1e6, N / dt, sum, rng.draws()))
  return dt / N * 1e6
end

print("interrupt callback: " .. (INTERRUPT_ON == 1 and "ON" or "off") .. ", hits per run: " .. N)
local a = bench("A pure Luau, double math", mkA, function() return { a = atk, d = 0 } end, function(c) return c.d end)
local b = bench("B Luau stages + native int64 ctx", mkB, function() return { a = atk, x = ctx.new(0) } end, function(c) return c.x:geti() end)
-- C: whole pipeline native (native-defaults stage)
rng.seed(12345)
local t0 = clock(); local sum = 0
for i = 1, N do sum = sum + native_hit(atk.min, atk.max, atk.batk, atk.ratio, atk.def, atk.vit, ELE[1], atk.c1, atk.c2, atk.tmod) end
local dt = clock() - t0
print(string.format("%-34s %8.3f us/hit  %9.0f hits/s  (checksum %d)", "C native stage (one call/hit)", dt / N * 1e6, N / dt, sum))
-- AoE burst: 50 targets in one tick, how many bursts fit in a 5 ms (25% of a 20 ms tick) script budget?
print(string.format("AoE burst (50 targets): A %.2f ms  B %.2f ms  C %.3f ms", a * 50 / 1000, b * 50 / 1000, dt / N * 50 * 1000))
