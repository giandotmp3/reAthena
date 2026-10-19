--!nonstrict
-- BR-RULESET-073: the states of a unit (no move, no cast, no chat, no attack, ...) computed from its active status set
-- (status_calc_state / status_calc_state_sub, status.cpp:5531-5850). The database says, per status, which states it sets always
-- (`states = {"NOMOVE"}`) and which only under a condition (`"NOMOVECOND"`); the condition itself is code, listed here per state.
-- A recalculation is triggered by one status starting or ending (`changed`); only the states that status touches are recomputed, the
-- others keep their current value (`cant`), exactly like the legacy function that rewrites the flags it is concerned with.
local define = import("scripts/status/define")
local ops = import("scripts/status/ops")

local M = {}

local function pc_skill(e, name) return e.skill(name) end

-- condition code of the conditional states: function(e, name, s) -> handled (false = "not implemented", the legacy logs an error and
-- skips the status), restricted (true = the state applies)
local COND = {}

COND.move = function(e, name, s)
	local K, u = e.K, e.unit
	if name == "GOSPEL" then return true, s.val4 == K.BCT_SELF
	elseif name == "BASILICA" then return true, s.val4 == u.id -- !RENEWAL: the caster cannot move (5617)
	elseif name == "GRAVITATION" then return true, s.val3 == K.BCT_SELF -- !RENEWAL (5624)
	elseif name == "CAMOUFLAGE" then return true, s.val1 < 3
	elseif name == "MAGNETICFIELD" then return true, s.val2 ~= u.id
	elseif name == "FEAR" then return true, s.val2 > 0
	elseif name == "HIDING" then
		if u.type ~= "pc" then return true, true end
		return true, pc_skill(e, "RG_TUNNELDRIVE") <= 0
	elseif name == "DANCING" then
		if s.val4 ~= 0 then
			-- Pre-Renewal: all ensembles restrict, unless Longing is active, then only Moonlit Serenade / Hermode's
			local song = ops.band(s.val1, 65535)
			if e.sc.LONGING ~= nil and song ~= K.CG_MOONLIT and song ~= K.CG_HERMODE then return true, false end
			return true, true
		end
		return true, false
	elseif name == "LONGING" then return true, false -- only triggers the recalculation of Dancing's condition
	elseif name == "CRYSTALIZE" then return true, u.type ~= "mob"
	end
	return false, false
end
COND.cast = function(e, name, s)
	if name == "WINKCHARM" then return true, e.unit.type ~= "pc" end
	if name == "OBLIVIONCURSE" then return true, s.val3 == 1 end
	return false, false
end
COND.chat = function(e, name, s)
	if name == "NOCHAT" then return true, ops.band(s.val1, e.K.MANNER_NOCHAT) ~= 0 end
	return false, false
end
COND.attack = function(e, name, s)
	if name == "DANCING" then return true, s.val4 == 0 or e.sc.LONGING == nil end
	if name == "LONGING" then return true, false end
	return false, false
end
COND.pickup = function(e, name, s)
	if name == "NOCHAT" then return true, ops.band(s.val1, e.K.MANNER_NOITEM) ~= 0 end
	return false, false
end
COND.drop = COND.pickup
COND.consume = function(e, name, s)
	if name == "GRAVITATION" then return true, s.val3 == e.K.BCT_SELF end
	if name == "NOCHAT" then return true, ops.band(s.val1, e.K.MANNER_NOITEM) ~= 0 end
	return false, false
end

-- key, db flag, db conditional flag, players only. Order of the legacy function.
local FLAGS = {
	{ "move", "NOMOVE", "NOMOVECOND", false }, { "cast", "NOCAST", "NOCASTCOND", false }, { "chat", "NOCHAT", "NOCHATCOND", false },
	{ "attack", "NOATTACK", "NOATTACKCOND", false }, { "warp", "NOWARP", "NOWARPCOND", false },
	{ "pickup", "NOPICKITEM", "NOPICKITEMCOND", true }, { "drop", "NODROPITEM", "NODROPITEMCOND", true },
	{ "equip", "NOEQUIPITEM", "NOEQUIPITEMCOND", true }, { "unequip", "NOUNEQUIPITEM", "NOUNEQUIPITEMCOND", true },
	{ "consume", "NOCONSUMEITEM", "NOCONSUMEITEMCOND", true }, { "deathpenalty", "NODEATHPENALTY", "NODEATHPENALTYCOND", true },
	{ "interact", "NOINTERACT", "NOINTERACTCOND", true },
}
M.FLAGS = FLAGS

local function set_of(list)
	local s = {}
	for _, v in ipairs(list or {}) do s[v] = true end
	return s
end

local cache = {}
local function state_set(name)
	local c = cache[name]
	if c == nil then
		local d = define.get(name)
		c = set_of(d and d.states)
		cache[name] = c
	end
	return c
end

-- e: environment (aggregate.env: e.sc, e.unit incl. id, e.skill); changed: SC name; start: bool; cant: current flags (table key -> bool)
-- Returns a new table with the recalculated flags.
function M.calc(e, changed, start, cant)
	local out = {}
	for k, v in pairs(cant or {}) do out[k] = v end
	if next(e.sc) == nil then -- sc.empty(): everything is cleared (5596)
		for _, f in ipairs(FLAGS) do out[f[1]] = false end
		return out
	end
	local main = state_set(changed)
	for _, f in ipairs(FLAGS) do
		local key, flag, condflag, pc_only = f[1], f[2], f[3], f[4]
		if (main[flag] or main[condflag]) and (not pc_only or e.unit.type == "pc") then
			if start and main[flag] then -- unconditional start: no further checks (5532)
				out[key] = true
			else
				local restriction = false
				local names = {}
				for name in pairs(e.sc) do names[#names + 1] = name end
				table.sort(names) -- the legacy iterates an unordered_map; the result does not depend on the order
				for _, name in ipairs(names) do
					local st = state_set(name)
					if st[flag] then restriction = true; break end
					if st[condflag] then
						local fn = COND[key]
						local handled, r = false, false
						if fn then handled, r = fn(e, name, e.sc[name]) end
						-- not handled: the legacy logs "condition is not implemented" and skips the status
						if handled and r then restriction = true; break end
					end
				end
				out[key] = restriction
			end
		end
	end
	return out
end

return M
