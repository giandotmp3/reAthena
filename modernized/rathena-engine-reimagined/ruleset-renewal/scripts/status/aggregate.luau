--!nonstrict
-- The pure-Luau aggregator of the status layer (BR-RULESET-065..069): the value of an attribute = the left fold of the ruleset's stage list
-- (scripts/status/attrs_re.luau) over the contributions of the unit's base value, its facts and the active statuses' contributes.
--
--   aggregate.calc("flee", base, unit, active)  ->  number
--     unit    = see scripts/status/facts.luau
--     active  = { blessing = {10, 5, 0, 0}, ... }      status name -> its val1..val4 (missing slots read 0)
--
-- Nothing here knows a status or a number of the game: the statuses come from scripts/status/status.luau, the stage lists from the ruleset.
local status = import("scripts/status/status")
local model = import("scripts/status/engine_model")
local facts = import("scripts/status/facts")
local attrs = import("scripts/status/attrs_re")

local M = {}

local function sorted_names(active)
	local names = {}
	for name in pairs(active) do names[#names + 1] = name end
	table.sort(names)
	return names
end

local function fact_value(F, name)
	local skill = name:match("^skill%.(.+)$")
	if skill then return F.skill[skill] or 0 end
	local v = F[name]
	if v == nil then error("aggregate: unknown fact '" .. name .. "'", 0) end
	return v
end

-- the facts of a unit plus which statuses are active (`u.sc.<name>`: the cross-status conditions of the legacy code, e.g. Improve Concentration
-- is void under Quagmire)
function M.facts_for(unit, active)
	local F = facts.of(unit)
	F.sc = {}
	local n = 0
	F.val = {} -- u.val.<status>.v1..v4: the val slots of the active statuses (the `getstatus(SC_X, n)` of status scripts, bonus_scripts.luau)
	for name, raw in pairs(active) do
		F.sc[name] = 1
		n += 1
		F.val[name] = { v1 = raw[1] or 0, v2 = raw[2] or 0, v3 = raw[3] or 0, v4 = raw[4] or 0 }
	end
	F.sc_empty = n == 0 and 1 or 0 -- `sc == nullptr || sc->empty()`: several functions return before they read the unit's passive skills
	return F
end

-- The contribution list of one attribute (insertion order: base, facts, derived attributes, statuses by name, ops by their order in the list).
function M.contributions(attr_name, base, unit, active, F)
	local attr = status.attribute(attr_name)
	if not attr then error("aggregate: unknown attribute '" .. tostring(attr_name) .. "'", 0) end
	F = F or M.facts_for(unit, active)
	local out = { { source = "base", tag = "base|0||", value = base } }
	for _, name in ipairs(attr.facts) do
		out[#out + 1] = { source = "unit", tag = "fact|0|" .. name .. "|", value = fact_value(F, name) }
	end
	for dep, spec in pairs(attr.needs or {}) do
		out[#out + 1] = { source = "derived", tag = "fact|0|" .. (spec.as or dep) .. "|", value = M.calc(dep, spec.base or 0, unit, active, F) }
	end
	local cands = {}
	for _, name in ipairs(sorted_names(active)) do
		local def = status.get(name)
		local list = def and def.contributions[attr_name]
		if list then
			local raw = active[name]
			local vals = { raw[1] or 0, raw[2] or 0, raw[3] or 0, raw[4] or 0 }
			for _, c in ipairs(list) do
				if c.when == nil or c.when(vals, F) ~= 0 then
					cands[#cands + 1] = { source = name, c = c, value = c.value and c.value(vals, F) or 0, arg = c.arg and c.arg(vals, F) or nil }
				end
			end
		end
	end
	-- `group`: the statements of one `if (A || B)` / first-present list apply once; with `rank` the lowest rank among the active ones applies
	local chosen = {}
	for _, cand in ipairs(cands) do
		local g = cand.c.group
		if g then
			local cur = chosen[g]
			if cur == nil or (cand.c.rank ~= nil and (cur.c.rank == nil or cand.c.rank < cur.c.rank)) then chosen[g] = cand end
		end
	end
	for _, cand in ipairs(cands) do
		local g = cand.c.group
		if g == nil or chosen[g] == cand then
			local c = cand.c
			local arg = c.rank ~= nil and tostring(c.rank) or (cand.arg ~= nil and tostring(cand.arg) or "")
			out[#out + 1] = { source = cand.source, tag = c.stage .. "|" .. tostring(c.at) .. "|" .. c.op .. "|" .. arg, value = cand.value }
		end
	end
	return out
end

function M.calc(attr_name, base, unit, active, F)
	local attr = status.attribute(attr_name)
	if not attr then error("aggregate: unknown attribute '" .. tostring(attr_name) .. "'", 0) end
	-- a unit without a status_change object (sc == nullptr) has no statuses; the maximum HP / SP / AP functions fetch the status set themselves
	if unit.nosc and not attr.reads_unit_sc and not F then active = {} end
	F = F or M.facts_for(unit, active)
	return model.fold(attr.stages, M.contributions(attr_name, base, unit, active, F))
end

-- Registers the stage lists of the attributes with the status registry (the ruleset's call; a mod may add or replace an attribute first).
function M.install(set)
	for name, def in pairs(set or attrs.attributes) do status.register_attribute(name, def) end
end

return M
