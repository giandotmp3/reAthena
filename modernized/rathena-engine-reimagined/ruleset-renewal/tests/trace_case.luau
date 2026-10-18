--!nonstrict
-- Development aid: prints one golden case (inputs, legacy result, draws, probes) next to the Luau replay.
--   (insert `RE_CASE = <id>` as the second line of the bundle: tools/trace_case.sh <id>)
local runner = import("tests/support/golden_runner")
local function show(v)
	if type(v) ~= "table" then return tostring(v) end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
	local parts = {}
	for _, k in ipairs(keys) do parts[#parts + 1] = tostring(k) .. "=" .. show(v[k]) end
	return "{" .. table.concat(parts, ",") .. "}"
end
return function()
	local want = RE_CASE
	for _, c in ipairs(runner.load()) do
		if c.id == want then
			print("cat", c.cat, "map", c.map, "cfg", show(c.cfg))
			print("a", show(c.a))
			print("t", show(c.t))
			print("legacy", show({ c.out.damage, c.out.damage2, c.out.type, c.out.div, c.out.flag, c.out.dmg_lv }), "draws", show(c.out.draws))
			local r = runner.run(c)
			print("luau  ", show(r.result and { r.result.damage, r.result.damage2, r.result.type, r.result.div, r.result.flag, r.result.dmg_lv }), r.detail)
			local function tr(list) local o = {} for _, s in ipairs(list) do o[#o + 1] = s[1] .. ":" .. s[2] .. ":" .. s[3] end return table.concat(o, " ") end
			print("legacy probes", tr(c.out.stages))
			print("luau   probes", r.trace and tr(r.trace) or "-")
			return 0
		end
	end
	print("no such case")
	return 1
end
