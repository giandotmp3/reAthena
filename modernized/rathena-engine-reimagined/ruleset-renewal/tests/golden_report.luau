--!nonstrict
-- Prints the conformance match rate overall and by category (development / CI report).
local runner = import("tests/support/golden_runner")
return function()
	local t0 = os.clock()
	local cases = runner.load()
	local t1 = os.clock()
	local rep = runner.run_all(cases)
	local t2 = os.clock()
	local names = {}
	for k in pairs(rep.cats) do names[#names + 1] = k end
	table.sort(names)
	for _, n in ipairs(names) do
		local c = rep.cats[n]
		print(string.format("  %-26s %5d / %-5d %s", n, c.matched, c.total, c.matched == c.total and "" or ("FIRST id=" .. rep.first_id[n] .. " " .. (rep.first[n] or ""))))
	end
	print(string.format("golden weapon_re: %d / %d matched (%.2f%%)", rep.matched, rep.total, 100 * rep.matched / rep.total))
	for k, v in pairs(rep.classes) do print("  mismatch class " .. k .. ": " .. v) end
	print(string.format("  stage-probe mismatches (diagnostic): %d", rep.stage_mismatch))
	print(string.format("  load %.2fs, replay %.2fs (%.1f us/case incl. compare)", t1 - t0, t2 - t1, (t2 - t1) / rep.total * 1e6))
	return rep.matched == rep.total and 0 or 1
end
