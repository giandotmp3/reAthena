--!nonstrict
-- usage: luau run_cases.luau -a [case id ...]     (no ids = all cases of the generated ./cases module)
-- Prints, per case:  "=== CASE <id>" , the trace lines, "=== END <id> <seconds>".
local T = require("./replay_trace")
local cases = require("./cases")
local want = { ... }
local only = nil
if #want > 0 then only = {}; for _, id in want do only[id] = true end end
local t0 = os.clock()
local n, failed = T.setup()
print(string.format("### registry %d function scripts (%d failed) %.2fs", n, failed, os.clock() - t0))
for _, c in cases do
	if not only or only[c.id] then
		local t1 = os.clock()
		print("=== CASE " .. c.id)
		local ok, lines = pcall(T.runCase, c)
		if ok then
			for _, l in lines do print(l) end
		else
			print("#end error:HARNESS:" .. tostring(lines))
		end
		print(string.format("=== END %s %.3f", c.id, os.clock() - t1))
	end
end
