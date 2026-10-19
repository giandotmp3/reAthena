--!nonstrict
-- Rule functions for group 'status'. Each rNNN takes the case `given` table and returns
-- the outcome table. Unimplemented rules raise NOT_IMPLEMENTED (expected-failure
-- acceptance tests, tagged with the rule ID, track them).
local notimpl = import("scripts/notimpl")
local status = import("scripts/status/index")
local states = import("scripts/status/states")
local aggregate = import("scripts/status/aggregate")
local scripted = import("scripts/status/scripted")
local recalc = import("scripts/status/recalc")
local K = import("scripts/status/consts")
local M = {}

-- The status rules that are implemented run on the declarative status layer (scripts/status). Their `given`:
--   unit_type   "pc" | "mob" (default "mob");  unit = extra fields of the unit snapshot (see scripts/status/aggregate.luau)
--   statuses    { NAME = {val1, val2, val3, val4} }  the active set (legacy SC_ suffix as key)
--   attr, base  the attribute and the value the legacy function receives
local function snapshot(input)
	local u = { type = input.unit_type or "mob", id = (input.unit_type == "pc") and 150000 or 200000, level = 1, skills = {}, map = "normal",
		weapon = 0, option = 0, class_ = K.MAPID_SWORDMAN }
	for k, v in pairs(input.unit or {}) do u[k] = v end
	return u
end

local function active_of(statuses)
	local a = {}
	for name, v in pairs(statuses or {}) do a[name] = { val1 = v[1] or 0, val2 = v[2] or 0, val3 = v[3] or 0, val4 = v[4] or 0 } end
	return a
end

local function cfg_of(input)
	local cfg = {}
	for k, v in pairs(K.CFG) do cfg[k] = v end
	for k, v in pairs(input.cfg or {}) do cfg[k] = v end
	return cfg
end

local function calc(input, ctx, attr, base)
	return status.calc(ctx, attr, base, snapshot(input), active_of(input.statuses), cfg_of(input))
end

-- BR-RULESET-051 (B, P0): status record schema carries id, icon, states, flags, fail/end lists
function M.r051(input)
	local d = status.get(input.status)
	local ok, why = status.validate_record(d)
	if not ok then return { valid = false, reason = why } end
	local r = d.record
	local function sorted(list)
		local o = {}
		for i, v in ipairs(list or {}) do o[i] = v end
		table.sort(o)
		return o
	end
	-- (the record also carries the namespaced id, icon, duration skill and the status' named val1..val4 fields: d.fields)
	return { valid = true, legacy_sc = r.sc, states = sorted(r.states), calc_flags = sorted(r.calc), flags = sorted(r.flags), fail = r.fail or {},
		end_on_start = r.end_on_start or {}, end_return = r.end_return or {}, end_on_end = r.end_on_end or {}, min_rate = r.min_rate,
		min_duration = r.min_duration, has_script = r.script ~= nil }
end


-- BR-RULESET-052 (B, P0): start gating: dead targets refused
function M.r052(input)
	return notimpl("BR-RULESET-052")
end

-- BR-RULESET-053 (PRE, P0): PRE resistance: sc_def from VIT, sc_def2 from LUK (Stun)
function M.r053(input)
	return notimpl("BR-RULESET-053")
end

-- BR-RULESET-055 (B, P0): resistance tail: rate rounded up to a multiple of 10
function M.r055(input)
	return notimpl("BR-RULESET-055")
end

-- BR-RULESET-056 (B, P1): immunity edge cases (Golden Thief Bug blocks magic statuses)
function M.r056(input)
	return notimpl("BR-RULESET-056")
end

-- BR-RULESET-057 (B, P1): delayed application: PRE keeps the full duration
function M.r057(input)
	return notimpl("BR-RULESET-057")
end

-- BR-RULESET-058 (B, P1): undead race is immune to Freeze
function M.r058(input)
	return notimpl("BR-RULESET-058")
end

-- BR-RULESET-059 (B, P1): post-delay prerequisites per type (Kyrie not on mobs)
function M.r059(input)
	return notimpl("BR-RULESET-059")
end

-- BR-RULESET-060 (B, P0): Blessing on a cursed player ends Curse, is not applied, returns true
function M.r060(input)
	return notimpl("BR-RULESET-060")
end

-- BR-RULESET-061 (B, P0): overlap: lower or equal incoming val1 is refused but returns true
function M.r061(input)
	return notimpl("BR-RULESET-061")
end

-- BR-RULESET-062 (B, P2): food: normal food refused if cash food has higher val1
function M.r062(input)
	return notimpl("BR-RULESET-062")
end

-- BR-RULESET-063 (B, P1): strip statuses fail if nothing is equipped
function M.r063(input)
	return notimpl("BR-RULESET-063")
end

-- BR-RULESET-064 (B, P0): ending a status with a stale timer id is ignored
function M.r064(input)
	return notimpl("BR-RULESET-064")
end

-- BR-RULESET-065 (B, P0): stat contributions clamp to 0..65535
function M.r065(input, ctx)
	-- stat contributions in code order, early-return overrides (Harmonize, Curse on LUK), result clamped to 0..65535
	return { value = calc(input, ctx, input.attr, input.base) }
end

-- BR-RULESET-066 (B, P0): PRE clamps def2 to at least 1
function M.r066(input, ctx)
	-- DEF / DEF2: override statuses first, then the additive and percent terms in code order; PRE clamps def2 to >= 1 (but not the early returns)
	return { value = calc(input, ctx, input.attr, input.base) }
end

-- BR-RULESET-067 (B, P0): speed model: 100 - haste + slow, base speed 150
function M.r067(input, ctx)
	-- speed: 100 - haste + slow (mount, slow group, haste group), floor 40, then cart / Paralysis / Steel Body / Defender / Change Speed
	return { speed = calc(input, ctx, "speed", input.base_speed) }
end

-- BR-RULESET-068 (B, P0): PRE ASPD: best of the quicken family
function M.r068(input, ctx)
	-- PRE ASPD: aspd_rate - best of the quicken family, then Berserk, potions and the slows
	return { aspd_rate = calc(input, ctx, "aspd_rate", input.base_aspd_rate) }
end

-- BR-RULESET-069 (B, P0): same contribution pattern for BATK, HIT, FLEE, CRI, MDEF, MAXHP ...
function M.r069(input, ctx)
	-- the same pattern for BATK, WATK, HIT, FLEE, FLEE2, CRI, MDEF, MDEF2, MAXHP, MAXSP, PATK, SMATK, RES, MRES, HPLUS, CRATE ...
	if input.attr == "maxhp" then return { value = status.maxhp(ctx, input.base, snapshot(input), active_of(input.statuses), cfg_of(input)) } end
	if input.attr == "maxsp" then return { value = status.maxsp(ctx, input.base, snapshot(input), active_of(input.statuses), cfg_of(input)) } end
	return { value = calc(input, ctx, input.attr, input.base) }
end

-- BR-RULESET-070 (B, P0): cast/delay/SP-cost contributions (PRE cast variants)
function M.r070(input)
	return notimpl("BR-RULESET-070")
end

-- BR-RULESET-071 (B, P1): statuses overriding element, defense element, size, race, look
function M.r071(input, ctx)
	-- statuses that override the attack element, the defense element and level (size, race and range have no overriding status in this source)
	return { value = calc(input, ctx, input.attr, input.base) }
end

-- BR-RULESET-072 (B, P0): poison tick for players: 2 + maxHP*3/200, stops at 25% HP
function M.r072(input)
	return notimpl("BR-RULESET-072")
end

-- BR-RULESET-073 (B, P0): States (NoMove, NoCast, NoAttack) computed from the active set
function M.r073(input, ctx)
	-- the states of a unit (no move / cast / chat / attack ...) from its active statuses: the flags that are set after `changed` starts or ends
	local e = aggregate.env(ctx, snapshot(input), active_of(input.statuses), cfg_of(input))
	local cant = {}
	for _, f in ipairs(states.FLAGS) do cant[f[1]] = input.cant and input.cant[f[1]] or false end
	local out = states.calc(e, input.changed, input.start ~= false, cant)
	local set = {}
	for _, f in ipairs(states.FLAGS) do
		if out[f[1]] then set[#set + 1] = f[1] end
	end
	table.sort(set)
	return { states = set }
end

-- BR-RULESET-074 (B, P0): status flags with behavior (NoDispell, RemoveOnDamaged, ...)
function M.r074(input)
	return notimpl("BR-RULESET-074")
end

-- BR-RULESET-075 (B, P0): scripted statuses contribute via script (Speedup0 bSpeedRate)
function M.r075(input, ctx)
	-- scripted statuses: the Script: of every active status runs; its bonus commands are returned as {argument count, type, ..., value}
	local names = {}
	for name in pairs(input.statuses) do names[#names + 1] = name end
	return { bonuses = scripted.run(ctx, names, active_of(input.statuses), K) }
end

-- BR-RULESET-076 (B, P2): Soul Link is class-gated
function M.r076(input)
	return notimpl("BR-RULESET-076")
end

-- BR-RULESET-077 (B, P0): recalculation re-entered more than 10 levels aborts
function M.r077(input)
	-- recalculation guard: a player recalculation re-entered more than 10 levels deep aborts; held back while delayed damage is pending
	if input.delayed_damage ~= nil then return { held = recalc.hold(input.delayed_damage, input.forced == true) } end
	local g = recalc.new(input.legacy_quirks)
	local ok = true
	for _ = 1, input.depth do ok = recalc.enter(g) end
	return { aborted = not ok }
end

return M
