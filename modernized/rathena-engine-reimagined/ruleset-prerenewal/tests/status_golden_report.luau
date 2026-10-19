--!nonstrict
-- Prints the status golden match rate by function, by category and by status (development / CI report):
--   python3 -I tools/run_tests.py --luau <luau> --status-report
local runner = import("tests/support/status_golden_runner")
return function(mode)
	local full = mode == "full"
	local t0 = os.clock()
	local aggregate = import("scripts/status/aggregate")
	local status = import("scripts/status/index")
	aggregate.cover = {}
	local g = runner.load()
	local t1 = os.clock()
	local rep = runner.run_all(g)
	local t2 = os.clock()
	local function sorted(t)
		local k = {}
		for n in pairs(t) do k[#k + 1] = n end
		table.sort(k)
		return k
	end
	print("-- by function")
	for _, n in ipairs(sorted(rep.by_fn)) do
		local x = rep.by_fn[n]
		print(string.format("  %-12s %6d / %-6d %s", n, x.matched, x.total, x.matched == x.total and "" or "MISMATCH"))
	end
	print("-- by category")
	for _, n in ipairs(sorted(rep.by_cat)) do
		local x = rep.by_cat[n]
		print(string.format("  %-14s %6d / %-6d %s", n, x.matched, x.total, x.matched == x.total and "" or "MISMATCH"))
	end
	local bad = 0
	for _, n in ipairs(sorted(rep.by_status)) do
		local x = rep.by_status[n]
		if x.matched ~= x.total then bad = bad + 1 end
	end
	print(string.format("-- by status: %d statuses appear in the cases, %d with a mismatch", #sorted(rep.by_status), bad))
	for _, n in ipairs(sorted(rep.by_status)) do
		local x = rep.by_status[n]
		if full or x.matched ~= x.total then print(string.format("  %-28s %5d / %-5d (alone: %d / %d)", n, x.matched, x.total, x.single_matched, x.single_total)) end
	end
	local entries = status.all_entries()
	local unhit = {}
	for _, ent in ipairs(entries) do
		if not aggregate.cover[ent] then unhit[#unhit + 1] = string.format("%s.%s@%d", ent.owner or "(rule)", ent.attr, ent.at) end
	end
	print(string.format("-- entry coverage: %d of %d contributions fire in at least one golden case; never fired: %s", #entries - #unhit, #entries,
		#unhit == 0 and "none" or table.concat(unhit, " ")))
	print(string.format("golden status_pre: %d / %d matched (%.2f%%), %d runtime errors", rep.matched, rep.total, 100 * rep.matched / rep.total, rep.errors))
	for i = 1, math.min(#rep.mismatches, 25) do
		local m = rep.mismatches[i]
		local sc = {}
		for n, v in pairs(m.case.sc) do sc[#sc + 1] = n .. "=" .. table.concat(v, ",") end
		table.sort(sc)
		print(string.format("  #%d %s [%s] base=%s ut=%s unit=%s sc={%s} cfg=%s: luau=%s legacy=%s", m.id, m.fn, m.cat, tostring(m.case.base), m.case.ut,
			(function() local t = {} for k, v in pairs(m.case.unit) do t[#t + 1] = k .. "=" .. (type(v) == "table" and "{..}" or tostring(v)) end table.sort(t) return table.concat(t, ",") end)(),
			table.concat(sc, " "), (function() local t = {} for k, v in pairs(m.case.cfg) do t[#t + 1] = k .. "=" .. tostring(v) end table.sort(t) return table.concat(t, ",") end)(), m.got, m.want))
	end
	print(string.format("  load %.2fs, replay %.2fs (%.1f us/case)", t1 - t0, t2 - t1, (t2 - t1) / rep.total * 1e6))
	return rep.matched == rep.total and 0 or 1
end
