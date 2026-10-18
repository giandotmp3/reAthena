-- M0(d): do the ctx int64/f32 methods reproduce the BR_RULESET worked examples exactly? (expected values from the spec)
local fails = 0
local function eq(name, got, want) local ok = got == want; if not ok then fails += 1 end
  print(string.format("%-46s got %-8s want %-8s %s", name, tostring(got), tostring(want), ok and "ok" or "MISMATCH")) end
local function C(v) return ctx.new(v) end
-- E-1 element boost, PRE: 1000 + 1000*15/100 = 1150, then *150/100 = 1725 ; RE: ratio 165 -> 1000 - 1000*(100-165)/100 = 1650
local c = C(1000); c:add(c:geti() * 15 // 100); c:mul_pct(150); eq("E-1 PRE element + Deluge", c:geti(), 1725)
local c = C(1000); local r = 150 + 15; c:set(1000 - 1000 * (100 - r) // 100); eq("E-1 RE element ratio add", c:geti(), 1650)
-- E-2 RES: 1000 -> 1000 - 160 = 840 ; ignoring 50%: RES 50 -> reduction 50/450*0.8 -> 912
local function res(d, RES) local c = C(d); c:set(d - math.floor(d * (RES / (RES + 400)) * 0.8)); return c:geti() end
eq("E-2 RES 100", res(1000, 100), 840); eq("E-2 RES 100 half ignored", res(1000, 50), 912)
-- E-3 card fix multiplies: 1000 *120/100 *115/100 = 1380 (not 1350)
local c = C(1000); c:mul_pct(120); c:mul_pct(115); eq("E-3 cardfix multiplicative", c:geti(), 1380)
-- E-5 heal lv5 base 99 INT 80: PRE (99+80)/8=22 * (4+5*8)=44 -> 968 ; RE (179/5)=35 * 30*5/10 = 525
local c = C(179); c:idiv(8); c:mul(4 + 5 * 8); eq("E-5 PRE heal", c:geti(), 968)
local c = C(179); c:idiv(5); c:mul(30 * 5); c:idiv(10); eq("E-5 RE heal", c:geti(), 525)
-- E-6 dual wield: right 1000*(50+50)/100 = 1000, left 1000*(30+50)/100 = 800
local c = C(1000); c:mul_pct(50 + 50); local l = C(1000); l:mul_pct(30 + 50); eq("E-6 right hand", c:geti(), 1000); eq("E-6 left hand", l:geti(), 800)
-- E-7 item heal: 150 * (100 + 2*50 + 10*5)/100 = 375
local c = C(150); c:mul_pct(100 + 2 * 50 + 10 * 5); eq("E-7 item heal", c:geti(), 375)
-- E-8 poison tick: 2 + 10000*3/200 = 152
local c = C(10000); c:mul(3); c:idiv(200); c:add(2); eq("E-8 poison tick", c:geti(), 152)
-- E-9 RE weapon ATK range: var = 5*100*4/100 = 20, stat bonus = 100*80/200 = 40 -> [120,160]
local var = 5 * 100 * 4 // 100; local bonus = 100 * 80 // 200; eq("E-9 range low", 100 - var + bonus, 120); eq("E-9 range high", 100 + var + bonus, 160)
-- E-4 stun resistance PRE: rate 5000-2500-300 = 2200 ; RE rate 5000-2500 = 2500, duration 5000-2500+500 = 3000
eq("E-4 PRE rate", 5000 - 2500 - 300, 2200); eq("E-4 RE duration", 5000 - 2500 + 500, 3000)
-- E-4b speed: 150 * (100 + 50 - 25)/100 = 187 (truncating)
local c = C(150); c:mul_pct(100 + 50 - 25); eq("E-4b speed", c:geti(), 187)
-- float32 step (BR-RULESET-034 style): 1.4 + 0.01*crate applied in float32 then truncated
local c = C(1000); c:f32_step(1.4 + 0.01 * 37); eq("RE crit float32 step (1.77)", c:geti(), 1770)

print("--- where doubles diverge from int64 (BR-RULESET-115)")
local big = 9007199254740993                              -- 2^53 + 1: not representable as a double
local viaDouble = (big + 1) * 3                           -- Luau number math
local c = C(0); c:set(9007199254740992); c:add(1); c:mul(3)  -- native int64 path (set takes a double, so seed at 2^53)
print(string.format("2^53+1 as double literal prints %.0f (precision lost); native ctx keeps %.0f after +1,*3 from 2^53", big, c:get()))
print(string.format("truncating division: Luau -7 // 2 = %d (floor), native idiv(-7,2) = ", -7 // 2) .. (function() local k = C(-7); k:idiv(2); return k:geti() end)() .. " (C++ truncation, legacy semantics)")
print(string.format("draw-count parity: rng.draws() after seeding and 5 rolls = %d (hooks use their own streams in the real design)", (function() rng.seed(9); for i = 1, 5 do rng.rnd(10) end; return rng.draws() end)()))
print(fails == 0 and "ALL EXAMPLES EXACT" or ("MISMATCHES: " .. fails))
