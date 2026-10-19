--!nonstrict
-- The restrictions of a unit computed from its active statuses (BR-RULESET-073; status_calc_state, status.cpp:5592-5830).
-- A status record lists states: NOMOVE (always), NOMOVECOND (only while the condition of the status holds, `state_when` of its definition).
-- A conditional state without a condition is never restrictive (the legacy lambdas return "not implemented" for it). Some restrictions apply to
-- players only.
local status = import("scripts/status/status")
local facts = import("scripts/status/facts")

local M = {}

-- the order of the legacy sc->cant fields (the golden hook packs them as bits 0..11)
M.ORDER = { "move", "pickup", "drop", "cast", "chat", "equip", "unequip", "consume", "attack", "warp", "deathpenalty", "interact" }
local KEY = {
	NOMOVE = "move", NOPICKITEM = "pickup", NODROPITEM = "drop", NOCAST = "cast", NOCHAT = "chat", NOEQUIPITEM = "equip",
	NOUNEQUIPITEM = "unequip", NOCONSUMEITEM = "consume", NOATTACK = "attack", NOWARP = "warp", NODEATHPENALTY = "deathpenalty",
	NOINTERACT = "interact",
}
local PLAYER_ONLY = { pickup = true, drop = true, equip = true, unequip = true, consume = true, deathpenalty = true, interact = true } -- 5760-5830

-- active: status name -> { val1..val4 }; returns { move = bool, ... } for every state
function M.compute(unit, active)
	local out = {}
	for _, key in ipairs(M.ORDER) do out[key] = false end
	local F = facts.of(unit)
	F.sc = {}
	for name in pairs(active) do F.sc[name] = 1 end
	for name, raw in pairs(active) do
		local rec = status.record(name)
		if rec and rec.states then
			local def = status.get(name)
			local vals
			for _, state in ipairs(rec.states) do
				local base, cond = state:match("^(.-)(COND)$")
				local key = KEY[base or state]
				if key and (not PLAYER_ONLY[key] or unit.kind == "pc") then
					if cond then
						local when = def and def.state_when[base:lower()]
						vals = vals or { raw[1] or 0, raw[2] or 0, raw[3] or 0, raw[4] or 0 }
						if when and when.fn(vals, F) ~= 0 then out[key] = true end
					else
						out[key] = true
					end
				end
			end
		end
	end
	return out
end

-- the bit mask the golden hook records
function M.mask(states)
	local m = 0
	for i, key in ipairs(M.ORDER) do
		if states[key] then m = m + 2 ^ (i - 1) end
	end
	return m
end

return M
