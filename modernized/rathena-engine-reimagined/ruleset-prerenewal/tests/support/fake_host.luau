--!nonstrict
-- Test double of the host API a package receives in register(engine): the parts of script-host's `engine` table the packages use
-- (pipeline, num, config) plus recording registries for status / skill / item. The Luau CLI that runs these suites has no script-host,
-- so this file stands in for it; the REAL implementations are script-host's C++ (PipelineRegistry, f32_round, the `config` table) and
-- are tested there, including a differential test that runs this double and the real runtime on the same inputs
-- (script-host/tests/test_rulesets.cpp). What the double deliberately does NOT model: load-time-only edits, priority and precedence
-- rules, protected-stage permissions, foreign states (HookContext, lanes, views, effect queues) and budgets.
local M = {}

-- ---- num.f32: round a double to binary32 (ties to even), denormals and overflow included -------------------------------------
local function round_even(s)
	local f = math.floor(s)
	local d = s - f
	if d > 0.5 or (d == 0.5 and f % 2 == 1) then f = f + 1 end
	return f
end

function M.f32(x)
	if x ~= x or x == 0 or x == math.huge or x == -math.huge then return x end
	local neg = x < 0
	if neg then x = -x end
	local r
	if x >= 3.4028235677973366e38 then
		r = math.huge -- 2^128 - 2^103 and beyond rounds to infinity
	else
		local m, e = math.frexp(x) -- x = m * 2^e, 0.5 <= m < 1
		if e < -125 then
			r = math.ldexp(round_even(math.ldexp(x, 149)), -149) -- denormal: quantum 2^-149
		else
			r = math.ldexp(round_even(m * 16777216), e - 24)
		end
	end
	return neg and -r or r
end

-- ---- pipeline ----------------------------------------------------------------------------------------------------------------
local function new_pipeline(log)
	local pl, defs = {}, {}

	local function def_of(name, who)
		local d = defs[name]
		if not d then error(who .. ": pipeline '" .. tostring(name) .. "' is not defined", 3) end
		return d
	end

	function pl.define(name, order)
		assert(type(name) == "string" and type(order) == "table", "pipeline.define(name, {stages})")
		log.pipelines[name] = order
		defs[name] = { order = order, bodies = {}, protected = {}, edits = {}, cache = nil }
	end

	function pl.stage(name, stage, fn, opts)
		local d = def_of(name, "pipeline.stage")
		assert(type(fn) == "function", "pipeline.stage: fn")
		d.bodies[stage] = fn
		d.protected[stage] = opts ~= nil and opts.protected == true
		d.cache, d.fns = nil, nil
		log.stages[#log.stages + 1] = { pipeline = name, stage = stage, protected = d.protected[stage] }
	end

	local function edit(name, who, e)
		local d = def_of(name, who)
		d.edits[#d.edits + 1] = e
		d.cache, d.fns = nil, nil
	end
	function pl.insert_before(name, anchor, id, fn, opts) edit(name, "pipeline.insert_before", { op = "insert", before = true, anchor = anchor, id = id, fn = fn }) end
	function pl.insert_after(name, anchor, id, fn, opts) edit(name, "pipeline.insert_after", { op = "insert", anchor = anchor, id = id, fn = fn }) end
	function pl.replace(name, stage, id, fn, opts) edit(name, "pipeline.replace", { op = "replace", stage = stage, id = id, fn = fn }) end
	function pl.cancel(name, stage, opts) edit(name, "pipeline.cancel", { op = "cancel", stage = stage }) end
	function pl.move(name, stage, where, opts) edit(name, "pipeline.move", { op = "move", stage = stage, where = where }) end

	-- the effective list: the defined order with the edits applied in call order (cancelled stages are dropped)
	local function steps_of(d)
		if d.cache then return d.cache end
		local list = {}
		for _, s in ipairs(d.order) do list[#list + 1] = { id = s, fn = d.bodies[s], base = true } end
		local function find(id)
			for i, st in ipairs(list) do if st.id == id then return i end end
			error("no such stage: " .. tostring(id))
		end
		for _, e in ipairs(d.edits) do
			if e.op == "insert" then
				local i = find(e.anchor)
				table.insert(list, e.before and i or i + 1, { id = e.id, fn = e.fn })
			elseif e.op == "replace" then
				list[find(e.stage)].fn = e.fn
			elseif e.op == "cancel" then
				table.remove(list, find(e.stage))
			elseif e.op == "move" then
				local st = table.remove(list, find(e.stage))
				local target = e.where.before or e.where.after
				local i = find(target)
				table.insert(list, e.where.before and i or i + 1, st)
			end
		end
		d.cache = list
		return list
	end

	function pl.order(name)
		local out = {}
		for _, st in ipairs(steps_of(def_of(name, "pipeline.order"))) do out[#out + 1] = st.id end
		return out
	end

	function pl.describe(name)
		local d = def_of(name, "pipeline.describe")
		local out = {}
		for _, st in ipairs(steps_of(d)) do
			out[#out + 1] = { stage = st.id, kind = st.base and "base" or "inserted", active = true, has_body = st.fn ~= nil, protected = d.protected[st.id] == true, edits = {} }
		end
		return out, { name = name }
	end

	-- every stage body is called with ctx (the double has no other states); opts.skip is honoured; no result table
	function pl.run(name, ctx, opts)
		local d = defs[name]
		if d == nil then def_of(name, "pipeline.run") end
		local fns, ids = d.fns, d.ids
		if fns == nil or d.cache == nil then
			local list = steps_of(d)
			fns, ids = {}, {}
			for i, st in ipairs(list) do fns[i], ids[i] = st.fn or false, st.id end
			d.fns, d.ids = fns, ids
		end
		local skip = opts and opts.skip
		for i = 1, #ids do
			if not (skip and skip[ids[i]]) then
				local fn = fns[i]
				if not fn then error({ code = "NO_STAGE_BODY", msg = "stage '" .. ids[i] .. "' has no body" }) end
				fn(ctx)
			end
		end
		return nil
	end

	return pl
end

-- engine, log = fake([config]): a fresh engine double. log.pipelines[name] = the order passed to define, log.stages = every
-- pipeline.stage call, log.status / skill / item = the definitions registered, log.native = pipeline.use_native calls.
local function new(config)
	local log = { pipelines = {}, stages = {}, status = {}, skill = {}, item = {}, native = {} }
	local function kind(name)
		return { define = function(id, def) log[name][id] = def end }
	end
	local pipeline = new_pipeline(log)
	pipeline.use_native = function(...) log.native[#log.native + 1] = { ... } end
	local engine = {
		pipeline = pipeline,
		num = { f32 = M.f32 },
		config = config or { legacy_quirks = true },
		status = kind("status"), skill = kind("skill"), item = kind("item"),
	}
	return engine, log
end

return setmetatable(M, { __call = function(_, config) return new(config) end })
