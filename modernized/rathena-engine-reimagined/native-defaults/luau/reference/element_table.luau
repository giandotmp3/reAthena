--!strict
-- Pure-Luau reference for native.damage.element_table@1 (BR-RULESET-031, BR-MAP-017).
local M = {}

function M.apply(ctx, tables)
	local ratio = tables.element:at(ctx.attack_element, ctx.target_element, ctx.target_element_level)
	local pct = ctx.status_element_pct
	if tables.status_element_mode == "pre_multiplicative" then
		ctx:set_damage(ctx:idiv((ctx.damage + ctx:idiv(ctx.damage * pct, 100)) * ratio, 100))
	else
		ratio += pct
		ctx:set_damage(ctx.damage - ctx:idiv(ctx.damage * (100 - ratio), 100))
	end
end

return M
