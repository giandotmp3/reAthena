--!nonstrict
-- Replays the status golden cases (conformance/golden/status_re.jsonl.gz, recorded from the REAL status_calc_* functions of the renewal legacy
-- binary) through the Luau status layer and compares every recorded output. Used by tests/acceptance/status_golden_test.luau and the report.
local S = import("scripts/status/init")
local status = S.status

local M = {}

function M.load() return import("build/status_golden_re") end

-- case.sc: { SC_NAME = {val1..val4} } -> { status name = vals }; nil when the status is unknown to the status layer
local function active_of(sc)
	local active = {}
	for name, vals in pairs(sc) do
		local key = status.by_sc(name)
		if key then active[key] = vals end -- a name with no record in the status database is not a status (the legacy sc_type has no entry)
	end
	return active
end

local function kind_of(e)
	if type(e) == "table" then return e.code or "ERROR", (e.code or "ERROR") .. " " .. tostring(e.msg) end
	return "LUAU_ERROR", tostring(e)
end

-- Runs one case. Returns { ok, outs = { fn = { want, got, ok } }, class, detail }; class "OVERFLOW" marks an intermediate int32 overflow (undefined behaviour in the
-- legacy C++), the case is then not comparable.
function M.run(case)
	local res = { ok = true, outs = {} }
	local ok, active = pcall(active_of, case.sc)
	if not ok then return { ok = false, class = "UNKNOWN_STATUS", detail = tostring(active.msg), outs = {} } end
	local unit = case.unit
	for fn, base in pairs(case["in"]) do
		local want = case.out[fn]
		local pok, got = pcall(S.calc, fn, base, unit, active)
		if not pok then
			local class, detail = kind_of(got)
			res.ok, res.class, res.detail = false, res.class or class, res.detail or (fn .. ": " .. detail)
			res.outs[fn] = { want = want, got = nil, ok = false, class = class }
		else
			local m = got == want
			res.outs[fn] = { want = want, got = got, ok = m }
			if not m then res.ok, res.class, res.detail = false, res.class or "MISMATCH", res.detail or (fn .. " expected " .. tostring(want) .. " got " .. tostring(got)) end
		end
	end
	if case.states then
		local want = case.out.states
		local pok, got = pcall(function() return S.states.mask(S.states.compute(unit, active)) end)
		local m = pok and got == want
		res.outs.states = { want = want, got = pok and got or nil, ok = m }
		if not m then res.ok, res.class, res.detail = false, res.class or "MISMATCH", res.detail or ("states expected " .. tostring(want) .. " got " .. tostring(got)) end
	end
	return res
end

-- Replays every case; returns the counts by function, by status (every case that contains it) and by category, and the mismatches.
function M.run_all(cases, opts)
	opts = opts or {}
	local rep = { total = 0, matched = 0, overflow = 0, errors = 0, by_fn = {}, by_status = {}, by_status_single = {}, by_cat = {}, bad = {}, comparisons = 0 }
	local function bump(t, k, ok)
		local e = t[k]
		if not e then e = { n = 0, ok = 0 }; t[k] = e end
		e.n += 1
		if ok then e.ok += 1 end
	end
	for _, case in ipairs(cases) do
		local r = M.run(case)
		rep.total += 1
		if r.class == "INT32_OVERFLOW" then rep.overflow += 1 end
		if r.ok then rep.matched += 1 elseif #rep.bad < (opts.max_bad or 200) then rep.bad[#rep.bad + 1] = { case = case, res = r } end
		bump(rep.by_cat, case.cat, r.ok)
		local count = 0
		for _ in pairs(case.sc) do count += 1 end
		for fn, o in pairs(r.outs) do
			rep.comparisons += 1
			bump(rep.by_fn, fn, o.ok)
		end
		for sc in pairs(case.sc) do
			bump(rep.by_status, sc, r.ok)
			if count == 1 then bump(rep.by_status_single, sc, r.ok) end
		end
	end
	return rep
end

-- Which contribution sites of the definitions the golden cases exercise: a site (status, attribute, op, at) counts as applied when some case emits it
-- (its `when` held and, for a group, it won), as skipped when some case has the status active but the site did not apply.
local function with_deps(fn, out)
	out[#out + 1] = fn
	local attr = status.attribute(fn)
	for dep in pairs(attr.needs or {}) do with_deps(dep, out) end
	return out
end

function M.coverage(cases)
	local applied, skipped = {}, {}
	for _, case in ipairs(cases) do
		local active = {}
		for name, vals in pairs(case.sc) do
			local key = status.by_sc(name)
			if key then active[key] = vals end
		end
		for fn, base in pairs(case["in"]) do
			if status.attribute(fn) then
				for _, a in ipairs(with_deps(fn, {})) do
					local attr = status.attribute(a)
					local act = (case.unit.nosc and not status.attribute(fn).reads_unit_sc) and {} or active
					local used = {}
					for _, c in ipairs(S.aggregate.contributions(a, a == fn and base or 0, case.unit, act, S.aggregate.facts_for(case.unit, act))) do
						local stage, at, op = c.tag:match("^([^|]*)|([^|]*)|([^|]*)|")
						if stage ~= "base" and stage ~= "fact" then
							local k = c.source .. "|" .. a .. "|" .. op .. "|" .. at
							applied[k] = (applied[k] or 0) + 1
							used[k] = true
						end
					end
					for name in pairs(act) do
						local def = status.get(name)
						for _, c in ipairs(def and def.contributions[a] or {}) do
							local k = name .. "|" .. a .. "|" .. c.op .. "|" .. tostring(c.at)
							if not used[k] then skipped[k] = (skipped[k] or 0) + 1 end
						end
					end
				end
			end
		end
	end
	return applied, skipped
end

return M
