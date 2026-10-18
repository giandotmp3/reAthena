--!nonstrict
-- Minimal test harness. Statuses: PASS, FAIL, XFAIL (expected failure observed),
-- XPASS (expected failure that passed: counts as a failure, remove the marker).
local T = {}
local results = {}

local function describe(err)
	if type(err) == "table" then
		return (err.code or "ERROR") .. (err.rule and (" " .. err.rule) or "") .. (err.msg and (": " .. err.msg) or "")
	end
	return tostring(err)
end

local function show(v)
	if type(v) ~= "table" then return tostring(v) end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
	local parts = {}
	for _, k in ipairs(keys) do parts[#parts + 1] = tostring(k) .. "=" .. show(v[k]) end
	return "{" .. table.concat(parts, ",") .. "}"
end

function T.assert_deep_equal(actual, expected, rule)
	local a, e = show(actual), show(expected)
	if a ~= e then error({ code = "MISMATCH", rule = rule, msg = "expected " .. e .. " got " .. a }, 2) end
end

function T.run(id, title, expect_fail, fn)
	local ok, err = pcall(fn)
	local status
	if expect_fail then status = ok and "XPASS" or "XFAIL" else status = ok and "PASS" or "FAIL" end
	results[#results + 1] = { id = id, title = title, status = status, err = (not ok) and describe(err) or nil }
end

-- Runs one conformance case; `pending` maps rule IDs to true for expected-failure. `env.make(draws)` (optional) returns
-- the rule's injected ctx and its rng; a case that lists `given.draws` must consume exactly those draws.
function T.case(case, rules, pending, env)
	T.run(case.rule, case.mode .. ": " .. case.title, pending[case.rule] == true, function()
		local fn = rules[case.fn]
		if not fn then error({ code = "MISSING_RULE_FUNCTION", rule = case.rule }) end
		local ctx, rng
		if env then ctx, rng = env.make(case.given.draws or {}) end
		local out = fn(case.given, ctx)
		if case.expected == nil then error({ code = "NO_GOLDEN_TRACE", rule = case.rule }) end
		if rng and case.given.draws and rng.used ~= rng.total then
			error({ code = "DRAW_COUNT", rule = case.rule, msg = "used " .. rng.used .. " of " .. rng.total .. " recorded draws" })
		end
		T.assert_deep_equal(out, case.expected, case.rule)
	end)
end

function T.summary()
	local counts = { PASS = 0, FAIL = 0, XFAIL = 0, XPASS = 0 }
	for _, r in ipairs(results) do
		counts[r.status] = counts[r.status] + 1
		if r.status == "FAIL" or r.status == "XPASS" then
			print(r.status .. "  " .. r.id .. "  " .. r.title .. (r.err and ("  <" .. r.err .. ">") or ""))
		end
	end
	print(string.format("pass=%d xfail=%d fail=%d xpass=%d total=%d",
		counts.PASS, counts.XFAIL, counts.FAIL, counts.XPASS, #results))
	return (counts.FAIL + counts.XPASS == 0) and 0 or 1
end

return T
