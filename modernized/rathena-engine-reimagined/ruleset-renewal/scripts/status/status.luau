--!nonstrict
-- Declarative status definitions (BR-RULESET-051, 065..069, 071, 073).
--
--   status.define(name, {
--       fields      = { val1 = "level", val2 = "stat_bonus" },        -- the names of the legacy val1..val4 slots (answers Q-F); unused slots are omitted
--       contributes = { str = { "add", "stat_bonus", when = "...", at = 6811 }, ... },   -- per attribute: one op or a list of ops
--       state_when  = { nomove = "target == BCT_SELF" },               -- conditional restrictions (the *COND states of the status record)
--       states = {...}, flags = {...},                                 -- optional: override the data record (data/status_records.luau)
--       note = "...",                                                  -- optional: documentation (quirks of the legacy slots) shown in docs/STATUS_FIELDS.md
--   })
--
-- An op is { <op>, <value expr>, at = <status.cpp line>, when = <condition expr>, arg = <second expr>, group = <id>, rank = <n> }:
--   at      the position of the statement in the legacy function: the contributions of an attribute are applied in ascending `at` (the legacy
--           functions are an ordered list of statements; the value after a pct term depends on every term before it). The line of
--           src/map/status.cpp is the citation AND the order key; a mod uses any number (7650.5 sits between 7650 and 7651).
--   when    the statement's condition on the status' own fields (by name) and on the unit (u.is_pc, u.vit, ...); false = no contribution.
--   group   statements with the same group id apply once (`if (A || B) ...`); rank orders the members of a chain of `else if` / first-present
--           lists (the member with the lowest rank among the active ones applies).
-- The vocabulary of ops per attribute and how they fold is the RULESET's: scripts/status/attrs_re.luau (the ordered stage list per attribute).
local expr = import("scripts/status/expr")
local consts = import("scripts/status/consts")

local M = {}

local defs, order = {}, {}
local attrs = {}          -- attribute name -> attribute definition (scripts/status/attrs_re)
local records_cache, by_sc_cache

function M.records()
	if not records_cache then
		records_cache = import("data/status_records")
		by_sc_cache = {}
		for key, rec in pairs(records_cache) do by_sc_cache["SC_" .. rec.name] = key end
	end
	return records_cache
end

-- "SC_BLESSING" -> "blessing" (the key of the status; the legacy sc_type names are the record names)
function M.by_sc(sc_name)
	M.records()
	return by_sc_cache[sc_name]
end

function M.record(name) return M.records()[name] end

function M.register_attribute(name, def)
	attrs[name] = def
end

function M.attribute(name) return attrs[name] end

local function compile_op(status_name, attr_name, attr, op, slots)
	local opname = op[1]
	local stage = attr.ops[opname]
	if not stage then
		error(("status.define(%s): attribute '%s' has no op '%s'"):format(status_name, attr_name, tostring(opname)), 0)
	end
	if type(op.at) ~= "number" then
		error(("status.define(%s): %s.%s needs `at` (the status.cpp line of the statement)"):format(status_name, attr_name, opname), 0)
	end
	local c = { op = opname, stage = stage, at = op.at, group = op.group, rank = op.rank, src = op.src, note = op.note }
	local function comp(src) return expr.compile(src, slots, consts) end
	local v = op[2]
	if v ~= nil then c.value_src = tostring(v); c.value = comp(tostring(v)) end
	if op.arg ~= nil then c.arg_src = tostring(op.arg); c.arg = comp(tostring(op.arg)) end
	if op.when ~= nil then c.when_src = op.when; c.when = comp(op.when) end
	return c
end

function M.define(name, spec)
	if defs[name] then error("status.define: '" .. name .. "' is already defined", 0) end
	local rec = M.record(name)
	local fields, slots, names = {}, {}, {}
	for i = 1, 4 do
		local nm = spec.fields and spec.fields["val" .. i]
		if nm then
			if slots[nm] then error("status.define(" .. name .. "): duplicate field name " .. nm, 0) end
			fields[i], slots[nm], names[#names + 1] = nm, i, nm
		end
	end
	for k in pairs(spec.fields or {}) do
		if not k:match("^val[1-4]$") then error("status.define(" .. name .. "): field key must be val1..val4, got " .. k, 0) end
	end
	local def = { name = name, fields = fields, field_slots = slots, contributions = {}, state_when = {}, record = rec, note = spec.note,
		states = spec.states or (rec and rec.states) or {}, flags = spec.flags or (rec and rec.flags) or {}, spec = spec }
	for attr_name, ops in pairs(spec.contributes or {}) do
		local attr = attrs[attr_name]
		if not attr then error(("status.define(%s): unknown attribute '%s'"):format(name, attr_name), 0) end
		if type(ops[1]) == "string" then ops = { ops } end
		local list = {}
		for _, op in ipairs(ops) do list[#list + 1] = compile_op(name, attr_name, attr, op, slots) end
		def.contributions[attr_name] = list
	end
	for state, src in pairs(spec.state_when or {}) do
		def.state_when[state] = { src = src, fn = expr.compile(src, slots, consts) }
	end
	defs[name] = def
	order[#order + 1] = name
	return def
end

function M.get(name) return defs[name] end

-- names of the statuses with behaviour (in definition order)
function M.names() return order end

function M.count() return #order end

-- test support: forget one definition (a test defines a throw-away status, uses it, removes it)
function M.undefine(name)
	if defs[name] == nil then return end
	defs[name] = nil
	for i, n in ipairs(order) do if n == name then table.remove(order, i) break end end
end

return M
