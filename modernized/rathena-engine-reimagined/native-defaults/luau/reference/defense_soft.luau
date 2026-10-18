--!strict
-- Pure-Luau reference for native.damage.defense_soft@1. Parameters come from the ruleset tables.
-- ctx integer math goes through ctx methods (BR-RULESET-115); shown here with idiv for clarity.
local M = {}

function M.apply(ctx, tables)
	local d = tables.defense
	if d.curve == "renewal" then
		-- BR-MAP-014: dmg * (k_base + def) / (k_base + k_slope * def) - soft_def
		ctx:set_damage(ctx:idiv(ctx.damage * (d.k_base + ctx.target_def), d.k_base + d.k_slope * ctx.target_def) - ctx.target_soft_def)
	else
		-- BR-MAP-015: pre-renewal; the randomised VIT term must draw from the main stream with legacy draw counts.
		error("TODO(BR-MAP-015): pre-renewal defense reference")
	end
end

return M
