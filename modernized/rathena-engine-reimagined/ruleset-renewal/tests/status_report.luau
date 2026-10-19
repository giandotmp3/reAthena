--!nonstrict
-- Prints the status conformance match rate by function, by category and by status, and the first mismatches (development / CI report).
local runner = import("tests/support/status_runner")
local function sorted_keys(t)
	local k = {}
	for key in pairs(t) do k[#k + 1] = key end
	table.sort(k)
	return k
end
return function()
	local t0 = os.clock()
	local golden = runner.load()
	local t1 = os.clock()
	local rep = runner.run_all(golden.cases)
	local t2 = os.clock()
	print("== by function (outputs compared)")
	for _, fn in ipairs(sorted_keys(rep.by_fn)) do
		local e = rep.by_fn[fn]
		print(string.format("  %-12s %6d / %-6d %s", fn, e.ok, e.n, e.ok == e.n and "" or "<-- MISMATCH"))
	end
	print("== by category (cases)")
	for _, c in ipairs(sorted_keys(rep.by_cat)) do
		local e = rep.by_cat[c]
		print(string.format("  %-14s %6d / %-6d %s", c, e.ok, e.n, e.ok == e.n and "" or "<-- MISMATCH"))
	end
	local bad_status = 0
	print("== by status (cases containing it); only statuses with a mismatch are listed")
	for _, sc in ipairs(sorted_keys(rep.by_status)) do
		local e = rep.by_status[sc]
		if e.ok ~= e.n then
			bad_status += 1
			local s = rep.by_status_single[sc]
			print(string.format("  %-28s %5d / %-5d (single-status cases %d / %d)", sc, e.ok, e.n, s and s.ok or 0, s and s.n or 0))
		end
	end
	local nstat = 0
	for _ in pairs(rep.by_status) do nstat += 1 end
	print(string.format("statuses in the golden set: %d, with a mismatch: %d", nstat, bad_status))
	for i, b in ipairs(rep.bad) do
		if i > 25 then break end
		local c = b.case
		local scs = {}
		for sc, v in pairs(c.sc) do scs[#scs + 1] = sc .. "=" .. table.concat(v, ",") end
		table.sort(scs)
		print(string.format("  BAD id=%d [%s] kind=%s %s | %s | in=%s", c.id, c.cat, c.unit.kind, b.res.class or "?", b.res.detail or "?", table.concat(scs, " ")))
	end
	local applied, skipped = runner.coverage(golden.cases)
	local S = import("scripts/status/init")
	local sites, never, never_skipped = 0, {}, 0
	for _, name in ipairs(S.status.names()) do
		local def = S.status.get(name)
		for attr, list in pairs(def.contributions) do
			for _, c in ipairs(list) do
				sites += 1
				local k = name .. "|" .. attr .. "|" .. c.op .. "|" .. tostring(c.at)
				if not applied[k] then never[#never + 1] = k end
				if c.when and not skipped[k] then never_skipped += 1 end
			end
		end
	end
	table.sort(never)
	print(string.format("== contribution sites: %d defined, %d applied by some golden case, %d never applied; %d conditional sites never skipped", sites,
		sites - #never, #never, never_skipped))
	for i, k in ipairs(never) do if i <= 40 then print("   never applied: " .. k) end end
	print(string.format("status golden: %d / %d cases matched (%.2f%%), %d outputs compared, %d int32-overflow cases", rep.matched, rep.total,
		100 * rep.matched / rep.total, rep.comparisons, rep.overflow))
	print(string.format("  load %.2fs, replay %.2fs (%.1f us/case)", t1 - t0, t2 - t1, (t2 - t1) / rep.total * 1e6))
	return rep.matched == rep.total and 0 or 1
end
