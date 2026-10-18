--!nonstrict
-- Replays golden weapon-attack cases through the Luau pipeline with the recorded RNG draws fed in order and compares
-- damage, damage2, type, div, flag, dmg_lv and the NUMBER (and kind/bounds) of RNG draws. Stage probes are compared too
-- but reported separately (diagnostic: they localise a mismatch, they never turn a mismatch into a match).
import("tests/support/booted") -- CLI only: registers the package with the fake host
local weapon = import("scripts/damage/weapon")
local unit = import("scripts/damage/unit")
local cfg_defaults = import("data/damage_cfg")
local fake_ctx = import("tests/support/fake_ctx")
local scripted_rng = import("tests/support/scripted_rng").scripted

local M = {}

local OUT_FIELDS = { "damage", "damage2", "type", "div", "flag", "dmg_lv" }

function M.load()
	return import("build/golden_weapon_re")
end

local function case_cfg(c)
	local cfg = {}
	for k, v in pairs(cfg_defaults) do cfg[k] = v end
	for k, v in pairs(c.cfg) do cfg[k] = v end
	return cfg
end

-- Runs one case. Returns { ok = bool, class = string|nil, detail = string|nil, stage_ok = bool, result = table|nil }
function M.run(c)
	local rng = scripted_rng(c.out.draws)
	local ctx = fake_ctx.new(rng)
	local trace = {}
	local ok, res = pcall(weapon.calc_attack, ctx, case_cfg(c), unit.normalize(c.a), unit.normalize(c.t), { trace = trace, map = c.map })
	if not ok then
		if type(res) == "table" and res.code == "RNG_SEQUENCE" then
			return { ok = false, class = "draw_sequence", detail = res.msg }
		end
		return { ok = false, class = "error", detail = type(res) == "table" and (res.code .. ": " .. tostring(res.msg)) or tostring(res) }
	end
	local bad = {}
	for _, f in ipairs(OUT_FIELDS) do
		if res[f] ~= c.out[f] then bad[#bad + 1] = f .. " luau=" .. tostring(res[f]) .. " legacy=" .. tostring(c.out[f]) end
	end
	local class
	if #bad > 0 then class = "output" end
	if rng.used ~= rng.total then
		bad[#bad + 1] = "draws used=" .. rng.used .. " recorded=" .. rng.total
		class = class or "draw_count"
	end
	-- stage probes (diagnostic)
	local stage_ok = #trace == #c.out.stages
	if stage_ok then
		for i, s in ipairs(c.out.stages) do
			local t = trace[i]
			if t[1] ~= s[1] or t[2] ~= s[2] or t[3] ~= s[3] then
				stage_ok = false
				break
			end
		end
	end
	return { ok = #bad == 0, class = class, detail = table.concat(bad, "; "), stage_ok = stage_ok, result = res, trace = trace }
end

-- Runs every case: returns { total, matched, classes = {class -> n}, cats = {cat -> {total, matched}}, first = {cat -> detail},
-- stage_mismatch = n }
function M.run_all(cases)
	local rep = { total = #cases, matched = 0, classes = {}, cats = {}, first = {}, first_class = {}, stage_mismatch = 0, first_id = {} }
	for _, c in ipairs(cases) do
		local r = M.run(c)
		local cat = rep.cats[c.cat]
		if not cat then cat = { total = 0, matched = 0 }; rep.cats[c.cat] = cat end
		cat.total = cat.total + 1
		if r.ok then
			rep.matched = rep.matched + 1
			cat.matched = cat.matched + 1
		else
			rep.classes[r.class] = (rep.classes[r.class] or 0) + 1
			if not rep.first[c.cat] then rep.first[c.cat] = r.detail; rep.first_id[c.cat] = c.id end
		end
		if r.stage_ok == false then rep.stage_mismatch = rep.stage_mismatch + 1 end
	end
	return rep
end

return M
