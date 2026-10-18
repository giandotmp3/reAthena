--!nonstrict
-- Runtime per attack of the pure-Luau pipeline with the pure-Luau fake ctx (asserted integer-safe arithmetic, closure-based
-- accumulators). The engine's native ctx replaces the fake, so this is an upper bound for the arithmetic part.
--   luau build/bundle_bench.luau   (tools/bundle_luau.py tests/bench_weapon build/bundle_bench.luau)
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg_defaults = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local runner = import("tests/support/golden_runner")

return function()
	local cases = runner.load()
	local prepared, hits = {}, 0
	for _, c in ipairs(cases) do
		local cfg = {}
		for k, v in pairs(cfg_defaults) do cfg[k] = v end
		for k, v in pairs(c.cfg) do cfg[k] = v end
		prepared[#prepared + 1] = { cfg = cfg, a = unit.normalize(c.a), t = unit.normalize(c.t), map = c.map, draws = c.out.draws, hit = c.out.dmg_lv == 5 }
		if c.out.dmg_lv == 5 then hits = hits + 1 end
	end
	local cur, i = nil, 0
	local rng = {
		rnd = function() i = i + 1; return cur[i] end,
		value = function(lo, hi) i = i + 1; return cur[i][3] end,
	}
	local ctx = fake_ctx.new(rng)
	local function pass(only_hits)
		local n = 0
		for _, p in ipairs(prepared) do
			if not only_hits or p.hit then
				cur, i = p.draws, 0
				weapon.calc_attack(ctx, p.cfg, p.a, p.t, { map = p.map })
				n = n + 1
			end
		end
		return n
	end
	pass(false) -- warm-up
	local passes = 20
	local t0 = os.clock()
	local n = 0
	for _ = 1, passes do n = n + pass(false) end
	local all_us = (os.clock() - t0) / n * 1e6
	t0 = os.clock()
	n = 0
	for _ = 1, passes do n = n + pass(true) end
	local hit_us = (os.clock() - t0) / n * 1e6
	print(string.format("weapon attack, pure-Luau fake ctx: %.2f us/attack over all %d golden cases, %.2f us/attack over the %d connected hits",
		all_us, #prepared, hit_us, hits))
	return 0
end
