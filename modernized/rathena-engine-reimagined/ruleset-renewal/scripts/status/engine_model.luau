--!nonstrict
-- A Luau transcription of engine-core's AttributeSystem contract (engine-core/include/engine/attrs/attrs.hpp, src/attrs.cpp): contributions
-- per (entity, attribute) in insertion order, a ruleset-supplied stage list per attribute, value = left fold of the stage list over the whole
-- contribution list starting from 0, cached until the (entity, attribute) is touched. The engine never interprets `tag`.
--
-- scripts/status/aggregate.luau computes with exactly this contract and nothing else (a stage function sees only (acc, contributions) and a
-- contribution is { source, tag, value }), so the same stage lists can be handed to the C++ class through a binding. script-host has no
-- `attrs` namespace today (api/engine.yaml): the status layer therefore folds in Luau, tests/acceptance/status_model_test.luau checks that
-- this transcription and the aggregator agree on every attribute.
local M = {}

local Sys = {}
Sys.__index = Sys

function M.new()
	return setmetatable({ stages = {}, slots = {}, names = {} }, Sys)
end

-- the pure fold (AttributeSystem::fold)
function M.fold(stages, contributions)
	local acc = 0
	for _, st in ipairs(stages) do acc = st.fn(acc, contributions) end
	return acc
end

function Sys:define_stages(attr, stages)
	self.stages[attr] = stages
	for key, slot in pairs(self.slots) do
		if slot.attr == attr then slot.dirty = true end
	end
end

local function key(entity, attr) return tostring(entity) .. "/" .. attr end

function Sys:add_contribution(entity, attr, c)
	local k = key(entity, attr)
	local slot = self.slots[k]
	if not slot then slot = { attr = attr, list = {}, cached = 0, dirty = true }; self.slots[k] = slot end
	slot.list[#slot.list + 1] = { source = c.source, tag = c.tag, value = c.value }
	slot.dirty = true
end

function Sys:remove_source(entity, source)
	local removed = 0
	for k, slot in pairs(self.slots) do
		if k:sub(1, #tostring(entity) + 1) == tostring(entity) .. "/" then
			local kept = {}
			for _, c in ipairs(slot.list) do
				if c.source == source then removed += 1 else kept[#kept + 1] = c end
			end
			slot.list, slot.dirty = kept, true
		end
	end
	return removed
end

function Sys:get(entity, attr)
	local slot = self.slots[key(entity, attr)]
	local stages = self.stages[attr]
	if not stages then return 0 end -- an attribute with no stage list reads 0
	if not slot then return M.fold(stages, {}) end
	if slot.dirty then slot.cached, slot.dirty = M.fold(stages, slot.list), false end
	return slot.cached
end

function Sys:recompute_from_scratch(entity, attr)
	local slot = self.slots[key(entity, attr)]
	local stages = self.stages[attr]
	if not stages then return 0 end
	return M.fold(stages, slot and slot.list or {})
end

return M
