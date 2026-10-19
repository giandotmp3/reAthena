--!nonstrict
-- status_calc_speed (status.cpp:8039-8246), PRE branch. The status dependent parts are contributions of the definitions in four groups
-- (gset/gmax ops with `group`): mount (GetMoveHasteValue2, first active wins), slow_force (Hiding with Tunnel Drive, negative Chase Walk:
-- they replace the slow group), slow (GetMoveSlowValue, max), haste (GetMoveHasteValue1, max). What is left here is the control flow of
-- the function: Mado Gear, Free Cast / Exceed Break, the 40 floor, cart, Paralysis, Steel Body, Defender / Armor, Change Speed.
-- E-4b: speed 150, Quagmire (slow 50), Increase AGI (haste 25): rate 100 + 50 - 25 = 125, speed = 150 * 125 / 100 = 187.
local aggregate = import("scripts/status/aggregate")
local attrs = import("scripts/status/attrs")
local ops = import("scripts/status/ops")

local M = {}

local function final(e, speed)
	local c = attrs.speed.clamp
	return ops.cap(speed, c[1], c[2])
end

function M.calc(e, base)
	local u, sc, K = e.unit, e.sc, e.K
	local pc = u.type == "pc"
	local speed = base
	if pc and u.permanent_speed then return final(e, speed) end -- 8044
	if pc and ops.band(u.option, K.OPTION_MADOGEAR) ~= 0 then -- 8047: Mado speed is not affected by other statuses
		local val = 0
		local lv = e.skill("NC_MADOLICENCE")
		if lv < 5 then val = 50 - 10 * lv else val = val - 25 end -- 8050-8053
		if sc.ACCELERATION then val = val - 25 end -- 8054
		speed = speed + e.idiv(speed * val, 100) -- 8056
		return final(e, speed) -- 8058
	end
	local contribs = aggregate.emit("speed", e)
	local g = aggregate.groups(contribs)
	local speed_rate = 100
	if pc and u.skilltimer and (e.skill("SA_FREECAST") > 0 or u.skill_id == K.LG_EXEEDBREAK) then -- 8061
		if u.skill_id == K.LG_EXEEDBREAK then speed_rate = 160 - 10 * u.skill_lv -- 8063
		else speed_rate = 175 - 5 * e.skill("SA_FREECAST") end -- 8065
	else
		speed_rate = speed_rate - (g.mount or 0) -- 8070-8080
		local val = g.slow_force -- 8083-8086: Hiding / Chase Walk replace the max group
		if val == nil then val = g.slow or 0 end -- 8088-8159
		speed_rate = speed_rate + val -- 8161
		if sc.MARSHOFABYSS and speed_rate > 150 then speed_rate = 150 end -- 8164
		speed_rate = speed_rate - (g.haste or 0) -- 8167-8223
		if speed_rate < 40 then speed_rate = 40 end -- 8225
	end
	-- GetSpeed() (8229-8243): the statuses' ordered operations (Paralysis +50 %, Steel Body, Defender / Armor, Change Speed: in the
	-- definitions) and the unit rules (cart: defs_hand) in source order, with the rate scaling `speed = speed * speed_rate / 100` at 8234
	local ordered = {}
	for _, c in ipairs(contribs) do
		if aggregate.OPS[c.op] then ordered[#ordered + 1] = c end
	end
	if speed_rate ~= 100 then ordered[#ordered + 1] = { at = 8234, op = "scale", v = speed_rate, v2 = 100 } end
	table.sort(ordered, function(a, b) return a.at < b.at end)
	for _, c in ipairs(ordered) do speed = aggregate.OPS[c.op](e, speed, c.v, c.v2) end
	return final(e, speed) -- 8245
end

return M
