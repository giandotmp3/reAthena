--!nonstrict
-- The status aggregation expressed in the terms of the engine's generic attribute system (engine-core/include/engine/attrs/attrs.hpp):
--
--   struct Contribution { SourceId source; std::string tag; int64_t value; }     per (entity, attribute), insertion order
--   struct AggregationStage { std::string id; int64_t fn(int64_t acc, span<const Contribution>); }
--   value(entity, attr) = left fold of the ruleset-supplied stage list over the whole contribution list, starting from 0
--
-- This file is the proof that scripts/status/aggregate.luau does not need anything the engine does not offer: it turns the ordered
-- contribution list of an attribute into flat {source, tag, value} records and folds them with five stage functions. A contribution that
-- has a second operand is encoded as two records: the operation tag with the first operand, then "<tag>.2" with the second. Unit facts
-- (permanent speed, Mado Gear, ...) would enter as tagged contributions of their own; only the attributes with the standard stage list
-- (and, as the group example, aspd_rate) are carried over here, the other custom evaluators (speed, mode, max HP) keep their control flow
-- in scripts/status/*.luau and are tested against the legacy directly.
local define = import("scripts/status/define")
local attrs = import("scripts/status/attrs")
local aggregate = import("scripts/status/aggregate")
local ops = import("scripts/status/ops")

local M = {}

local TERMINAL_TAGS = { ["override/final"] = true, ["override/final_div"] = true, ["override/final_clamp"] = true, ["override/stop"] = true }

-- ordered contribution list -> engine records
function M.records(attr, base, contribs)
	local out = { { source = "base", tag = "base", value = base } }
	for _, c in ipairs(contribs) do
		local stage = "ordered"
		if c.stage == "pre" then stage = "pre"
		elseif c.stage == "override" or define.TERMINAL[c.op] then stage = "override" end
		local tag = stage .. "/" .. c.op
		if c.op == "gset" or c.op == "gmax" then tag = "group:" .. c.group .. "/" .. c.op end
		out[#out + 1] = { source = c.src or "rule", tag = tag, value = c.v or 0 }
		if c.v2 ~= nil then out[#out + 1] = { source = c.src or "rule", tag = tag .. ".2", value = c.v2 } end
	end
	return out
end

local function operands(span, i)
	local r = span[i]
	local nxt = span[i + 1]
	if nxt and nxt.tag == r.tag .. ".2" then return r.value, nxt.value end
	return r.value, nil
end

local function has_terminal(span)
	for _, r in ipairs(span) do
		if TERMINAL_TAGS[r.tag] then return true end
	end
	return false
end

-- stage functions: fn(idiv, acc, span) -> acc
local function stage_base(attr)
	return { id = "base", fn = function(e, acc, span)
		for _, r in ipairs(span) do
			if r.tag == "base" then acc = acc + r.value end
		end
		return acc
	end }
end

local function by_prefix(prefix, id)
	return { id = id, fn = function(e, acc, span)
		for i, r in ipairs(span) do
			if r.tag:sub(1, #prefix) == prefix and r.tag:sub(-2) ~= ".2" then
				local op = r.tag:sub(#prefix + 1)
				local v, v2 = operands(span, i)
				if aggregate.OPS[op] then acc = aggregate.OPS[op](e, acc, v, v2) end
			end
		end
		return acc
	end }
end

local function stage_override(attr)
	local cfg = attrs[attr]
	return { id = "override", fn = function(e, acc, span)
		for i, r in ipairs(span) do
			if r.tag:sub(1, 9) == "override/" and r.tag:sub(-2) ~= ".2" then
				local op = r.tag:sub(10)
				local v, v2 = operands(span, i)
				if op == "final" then return ops.narrow(v, cfg.ret[1], cfg.ret[2])
				elseif op == "final_div" then return ops.narrow(e.idiv(acc, v), cfg.ret[1], cfg.ret[2])
				elseif op == "final_clamp" then return ops.cap(v, cfg.clamp[1], cfg.clamp[2])
				elseif op == "stop" then return ops.cap(acc, cfg.clamp[1], cfg.clamp[2])
				else acc = aggregate.OPS[op](e, acc, v, v2) end
			end
		end
		return acc
	end }
end

local function stage_group_quicken()
	-- aspd_rate: `aspd_rate -= max` where max is the gset / gmax fold of the group (status.cpp:8434-8490)
	return { id = "quicken", fn = function(e, acc, span)
		local g = 0
		for _, r in ipairs(span) do
			if r.tag == "group:quicken/gset" then g = r.value
			elseif r.tag == "group:quicken/gmax" and r.value > g then g = r.value end
		end
		return acc - g
	end }
end

local function stage_clamp(attr)
	local cfg = attrs[attr]
	return { id = "clamp", fn = function(e, acc, span)
		if has_terminal(span) or not cfg.clamp then return acc end
		return ops.cap(acc, cfg.clamp[1], cfg.clamp[2])
	end }
end

-- the stage list of an attribute (what a ruleset hands to AttributeSystem::define_stages)
function M.stages(attr)
	if attr == "aspd_rate" then
		local ordered = by_prefix("ordered/", "ordered")
		return { stage_base(attr), stage_group_quicken(), ordered, stage_clamp(attr) }
	end
	local ordered = by_prefix("ordered/", "ordered")
	local inner = ordered.fn
	ordered.fn = function(e, acc, span)
		if has_terminal(span) then return acc end
		return inner(e, acc, span)
	end
	return { stage_base(attr), by_prefix("pre/", "pre"), stage_override(attr), ordered, stage_clamp(attr) }
end

-- AttributeSystem::fold: left fold of the stages over the record list, starting from 0
function M.fold(e, stages, span)
	local acc = 0
	for _, st in ipairs(stages) do acc = st.fn(e, acc, span) end
	return acc
end

-- value of `attr` computed the engine way
function M.value(e, attr, base)
	local span = M.records(attr, base, aggregate.emit(attr, e))
	return M.fold(e, M.stages(attr), span), span
end

return M
