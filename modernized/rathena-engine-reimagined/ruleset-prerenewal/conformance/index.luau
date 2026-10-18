--!strict-compatible
-- BR-RULESET-119: conformance suite index, keyed by BR-RULESET ID.
local M = {}

M.GROUPS = { "damage", "status", "skill", "item", "contract" }

function M.all_cases()
	local out = {}
	for _, g in ipairs(M.GROUPS) do
		for _, c in ipairs(import("conformance/cases/" .. g)) do out[#out + 1] = c end
	end
	return out
end

-- Compares case rule IDs with the assigned rule list.
function M.coverage()
	local required = import("conformance/required")
	local want, seen, dups, extra, missing = {}, {}, {}, {}, {}
	for _, id in ipairs(required) do want[id] = true end
	for _, c in ipairs(M.all_cases()) do
		if seen[c.rule] then dups[#dups + 1] = c.rule end
		seen[c.rule] = true
		if not want[c.rule] then extra[#extra + 1] = c.rule end
	end
	for _, id in ipairs(required) do
		if not seen[id] then missing[#missing + 1] = id end
	end
	table.sort(dups); table.sort(extra); table.sort(missing)
	return { missing = missing, extra = extra, duplicates = dups }
end

return M
