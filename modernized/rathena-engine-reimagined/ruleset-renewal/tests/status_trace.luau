--!nonstrict
-- Development aid: prints one status golden case (unit, statuses, inputs, legacy output) next to the Luau replay and the contribution list
-- of every attribute it computes.   tools/status_trace.sh <case-id>
local runner = import("tests/support/status_runner")
local S = import("scripts/status/init")
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
	for _, c in ipairs(runner.load().cases) do
		if c.id == want then
			print("cat", c.cat)
			print("unit", show(c.unit))
			print("sc", show(c.sc))
			print("in", show(c["in"]))
			print("legacy", show(c.out))
			local r = runner.run(c)
			for fn, o in pairs(r.outs) do print("  " .. fn, "legacy", o.want, "luau", o.got, o.ok and "" or "<-- DIFFERS") end
			local active = {}
			for name, v in pairs(c.sc) do active[S.status.by_sc(name) or name] = v end
			for fn, base in pairs(c["in"]) do
				if S.status.attribute(fn) then
					print("contributions of " .. fn)
					for _, k in ipairs(S.aggregate.contributions(fn, base, c.unit, active)) do print("   ", k.source, k.tag, k.value) end
				end
			end
		end
	end
	return 0
end
