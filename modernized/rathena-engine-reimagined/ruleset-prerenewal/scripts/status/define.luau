--!nonstrict
-- Declarative status front end (BR-RULESET-051, 065, 069, 075; answers open question Q-F for the covered statuses).
--
--   status.define("INCREASEAGI", {
--     fields      = { val1 = "lv", val2 = "agi_inc" },          -- names of the legacy val1..val4 (docs/STATUS_FIELDS.md)
--     contributes = { agi = { { at = 6882, op = "add", v = function(s, e) return s.agi_inc end }, ... }, ... },
--     states = {...}, flags = {...}, calc_flags = {...}, ...     -- the status record (defaults to data/status_records)
--   })
--
-- A contribution (`entry`) says: attribute `attr`, operation `op`, operand(s) `v` / `v2` (a number or function(s, e) -> number),
-- an optional guard `when(s, e) -> bool`, and `at` = the line of the legacy statement in src/map/status.cpp. `at` is both the
-- citation and the evaluation order: the legacy functions are straight-line code, so inside one attribute the contributions fold in
-- source order (scripts/status/aggregate.luau). `s` is the named-field table of the status that owns the entry, `e` the environment
-- (e.sc[NAME] = every active status' named fields, e.unit = the unit snapshot, e.cfg, e.skill(name), e.idiv, e.K).
--
-- Operations (acc = the running value of the attribute; D = ctx.idiv, truncating toward zero):
--   add v            acc + v                          mul_pct v      acc + D(acc * v, 100)       mul_frac v v2   acc + D(acc * v, v2)
--   scale v v2       D(acc * v, v2)                   div v          D(acc, v)                   set v           v
--   max v            max(acc, v)
--   pct_over v v2    acc + D((acc - v) * v2, 100)     sub_pct2 v v2  acc - (D(acc * v, 100) + D(acc * v2, 100))
--   terminal (early return of the legacy function): final v | final_div v | final_clamp v | stop
--   groups (speed, aspd_rate keep a `val` / `max` variable): gmax v (max into the group), gset v (assign)
-- Every operation is a pure function of (acc, 1..2 operands): a contribution is therefore expressible as the engine attrs module's
-- {source, tag, value} (the second operand is the following contribution tagged "<op>.2"), see scripts/status/engine_model.luau.
local M = {}

M.OPS = {
	add = 1, mul_pct = 1, mul_frac = 2, scale = 2, div = 1, set = 1, pct_over = 2, sub_pct2 = 2, max = 1,
	final = 1, final_div = 1, final_clamp = 1, stop = 0, gmax = 1, gset = 1,
}
M.TERMINAL = { final = true, final_div = true, final_clamp = true, stop = true }

local defs = {}        -- NAME -> definition
local order = {}       -- NAME in definition order
local by_owner = {}    -- attr -> NAME -> { entry }
local rules = {}       -- attr -> { entry }  (no owning status)
local counter = 0

local function check_entry(name, attr, ent)
	local where = name .. "." .. attr .. " at=" .. tostring(ent.at)
	if type(ent.at) ~= "number" then error("status.define: entry without `at` (status.cpp line): " .. where, 3) end
	local arity = M.OPS[ent.op]
	if arity == nil then error("status.define: unknown op '" .. tostring(ent.op) .. "': " .. where, 3) end
	if arity >= 1 and ent.v == nil then error("status.define: op " .. ent.op .. " needs v: " .. where, 3) end
	if arity >= 2 and ent.v2 == nil then error("status.define: op " .. ent.op .. " needs v2: " .. where, 3) end
	if ent.when ~= nil and type(ent.when) ~= "function" then error("status.define: when must be a function: " .. where, 3) end
	if (ent.op == "gmax" or ent.op == "gset") and type(ent.group) ~= "string" then error("status.define: group op without group: " .. where, 3) end
end

local function register(name, attr, ent)
	check_entry(name or "(rule)", attr, ent)
	counter = counter + 1
	ent.ord = counter
	ent.attr = attr
	ent.owner = name
	if name == nil then
		rules[attr] = rules[attr] or {}
		table.insert(rules[attr], ent)
		return
	end
	by_owner[attr] = by_owner[attr] or {}
	local list = by_owner[attr]
	for _, owner in ipairs({ name, table.unpack(ent.also or {}) }) do
		list[owner] = list[owner] or {}
		table.insert(list[owner], ent)
	end
end

local function add_contributes(name, spec)
	for attr, list in pairs(spec.contributes or {}) do
		for _, ent in ipairs(list) do register(name, attr, ent) end
	end
end

-- Defines (or, with extend, adds to) a status. Unknown keys are kept on the definition (records add states, flags, ...).
function M.define(name, spec)
	if defs[name] ~= nil then error("status.define: duplicate definition of " .. name .. " (use status.extend)", 2) end
	local d = { name = name, fields = {}, field_index = {}, in_pre_db = spec.in_pre_db ~= false }
	for k, v in pairs(spec) do
		if k ~= "contributes" and k ~= "fields" then d[k] = v end
	end
	for n = 1, 4 do
		local fname = (spec.fields or {})["val" .. n]
		if fname then
			if d.field_index[fname] then error("status.define: " .. name .. ": field name '" .. fname .. "' used twice", 2) end
			d.fields[n] = fname
			d.field_index[fname] = n
		end
	end
	defs[name] = d
	order[#order + 1] = name
	add_contributes(name, spec)
	return d
end

-- Adds contributions (and keys) to an existing definition: used by the hand-written overlay for statements the first draft could not
-- express (unit dependent branches, weapon level checks, ...).
function M.extend(name, spec)
	local d = defs[name]
	if d == nil then error("status.extend: unknown status " .. name, 2) end
	for k, v in pairs(spec) do
		if k ~= "contributes" and k ~= "fields" then d[k] = v end
	end
	add_contributes(name, spec)
	return d
end

-- A unit-level rule: a contribution no status owns (map penalties, item speed bonus, ...).
function M.rule(attr, ent)
	register(nil, attr, ent)
end

function M.get(name) return defs[name] end
function M.names() return order end
function M.owner_entries(attr) return by_owner[attr] or {} end
function M.rule_entries(attr) return rules[attr] or {} end

-- raw { val1..val4 } -> table of the status' named fields (also keeps val1..val4 so unnamed data is never lost)
function M.instance(name, raw)
	local d = defs[name]
	local out = {}
	for n = 1, 4 do
		local v = raw["val" .. n] or raw[n] or 0
		out["val" .. n] = v
		local fname = d and d.fields[n]
		if fname then out[fname] = v end
	end
	return out
end

-- Merges the status records (data/status_records: states, calc flags, flags, lists, script, ... of db/pre-re/status.yml) into the
-- definitions; statuses without a definition get a record-only definition (no fields, no contributions).
function M.load_records(records)
	local names = {}
	for name in pairs(records) do names[#names + 1] = name end
	table.sort(names)
	for _, name in ipairs(names) do
		local d = defs[name]
		if d == nil then
			d = M.define(name, {})
			d.record_only = true
		end
		d.record = records[name]
		d.states = records[name].states or {}
		d.calc_flags = records[name].calc or {}
		d.flags = records[name].flags or {}
	end
end

-- BR-RULESET-051: the schema of a status record. Returns true, or false and the reason.
local function is_name_list(v)
	if v == nil then return true end
	if type(v) ~= "table" then return false end
	for _, x in ipairs(v) do
		if type(x) ~= "string" or x == "" then return false end
	end
	return true
end

function M.validate_record(d)
	if type(d) ~= "table" then return false, "no definition" end
	local r = d.record
	if type(r) ~= "table" then return false, "no status record (data/status_records)" end
	if type(r.id) ~= "string" or r.id:sub(1, 26) ~= "ruleset.prerenewal:status/" then return false, "id is not a namespaced status id" end
	if type(r.sc) ~= "number" or r.sc < 0 or math.floor(r.sc) ~= r.sc then return false, "sc is not a non-negative integer" end
	for _, k in ipairs({ "states", "calc", "flags", "fail", "end_on_start", "end_return", "end_on_end" }) do
		if not is_name_list(r[k]) then return false, k .. " is not a list of names" end
	end
	for _, k in ipairs(r.states or {}) do
		if not k:match("^NO[A-Z]+$") then return false, "unknown state " .. k end
	end
	if type(r.min_rate) ~= "number" or type(r.min_duration) ~= "number" then return false, "min_rate / min_duration missing" end
	if r.script ~= nil and type(r.script) ~= "string" then return false, "script is not text" end
	local seen = {}
	for n = 1, 4 do
		local f = d.fields[n]
		if f ~= nil then
			if type(f) ~= "string" or seen[f] then return false, "field names must be unique strings" end
			seen[f] = true
		end
	end
	return true
end

-- Test support: drops everything (the definition files re-register when imported again through scripts/status/index).
function M.reset()
	defs, order, by_owner, rules, counter = {}, {}, {}, {}, 0
end

-- Every registered contribution once (an entry owned by several statuses is listed once): used by the coverage report.
function M.all_entries()
	local out, seen = {}, {}
	local function take(list)
		for _, ent in ipairs(list) do
			if not seen[ent] then seen[ent] = true; out[#out + 1] = ent end
		end
	end
	for _, owners in pairs(by_owner) do
		for _, list in pairs(owners) do take(list) end
	end
	for _, list in pairs(rules) do take(list) end
	table.sort(out, function(a, b) return a.ord < b.ord end)
	return out
end

function M.count()
	local n_entries, n_rules = 0, 0
	for _, owners in pairs(by_owner) do
		local seen = {}
		for _, list in pairs(owners) do
			for _, ent in ipairs(list) do
				if not seen[ent] then seen[ent] = true; n_entries = n_entries + 1 end
			end
		end
	end
	for _, list in pairs(rules) do n_rules = n_rules + #list end
	return { statuses = #order, entries = n_entries, rules = n_rules }
end

return M
