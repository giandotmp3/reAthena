--!nonstrict
-- Luau counterpart of the legacy record hook (replay/patch/replay_hook.inc): runs ONE NPC of the legacy
-- dialect through legacy_interp with scripted answers and emits the same trace format.
-- Does not modify legacy_interp: commands are wrapped, the driver loop is its own.
-- Needs the legacy_interp modules (and the prep.py data modules) next to it: see replay/run_replay.sh.
local I = require("./interp")
local Base = require("./commands")
local Ext = require("./commands_ext")
local W = require("./world")
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local R = require("./npcrun")
local K = parser.K

local T = {}

-- core VM operations that the legacy side does not record either (they are compiled to jumps / locals there)
local SKIP = { set = true, setr = true, jump_zero = true, ["goto"] = true, callsub = true, callfunc = true, ["return"] = true,
	getarg = true, getargcount = true, ["end"] = true, getelementofarray = true, getd = true, setd = true }
-- commands that block: the legacy hook records them when they are called, before the client answers
local PRE = { next = true, close2 = true, sleep = true, sleep2 = true }

local function fmtStr(s: string): string
	local out = { '"' }
	for i = 1, #s do
		local c = string.byte(s, i)
		if c == 92 then out[#out + 1] = "\\\\"
		elseif c == 34 then out[#out + 1] = '\\"'
		elseif c == 10 then out[#out + 1] = "\\n"
		elseif c == 13 then out[#out + 1] = "\\r"
		elseif c == 9 then out[#out + 1] = "\\t"
		elseif c < 32 or c == 127 then out[#out + 1] = string.format("\\x%02x", c)
		else out[#out + 1] = string.char(c) end
	end
	out[#out + 1] = '"'
	return table.concat(out)
end
local function fmtNum(v: number): string
	if v == math.floor(v) and math.abs(v) < 2^53 then return string.format("%d", v) end
	return string.format("%.14g", v)
end
local function refName(ref): string
	if ref[1] == "param" then return string.lower(ref[2]) end
	local n = string.lower(ref[1] .. ref[2])
	local idx = ref[3]
	if idx and idx ~= 0 then n ..= "[" .. fmtNum(idx) .. "]" end
	return n
end
local function fmtVal(v): string
	local t = type(v)
	if t == "number" then return fmtNum(v)
	elseif t == "string" then return fmtStr(v)
	elseif t == "table" then return "&" .. refName(v)
	end
	return "<" .. t .. ">"
end

-- is argument position k (1-based) a reference position of the builtin spec? (same walk as the C++ side)
local function specIsRef(spec: string?, k: number): boolean
	if not spec then return false end
	local n = #spec
	local c
	local i = k
	if i <= n and string.sub(spec, i, i) ~= "?" and string.sub(spec, i, i) ~= "*" then
		c = string.sub(spec, i, i)
	else
		local j = math.min(i - 1, n)
		while j > 0 and (string.sub(spec, j, j) == "?" or string.sub(spec, j, j) == "*") do j -= 1 end
		c = j > 0 and string.sub(spec, j, j) or ""
	end
	return c == "r"
end

local function scriptedRand(env, a, b)
	env.randN = (env.randN or 0) + 1
	local r = (1103515245 * env.randN + 12345) % 2147483648
	if b ~= nil then
		if b < a then a, b = b, a end
		return a + r % (b - a + 1)
	end
	if a <= 0 then return 0 end
	return r % a
end

-- wrap a command table so that every call is recorded (after completion, as on the legacy side)
local function traced(cmdTable)
	local out = {}
	local names = {}
	local seen = {}
	local function collect(tbl)
		for k in tbl do if not seen[k] then seen[k] = true; names[#names + 1] = k end end
		local mt = getmetatable(tbl)
		if mt and type(mt.__index) == "table" then collect(mt.__index) end
	end
	collect(cmdTable)
	for _, name in names do
		local h = cmdTable[name]
		if SKIP[name] then
			out[name] = h
		else
			out[name] = function(st, ...)
				local n = select("#", ...)
				local args = table.pack(...)
				local ok, r
				local lineIdx
				if PRE[name] then lineIdx = #st.replayOut + 1; st.replayOut[lineIdx] = name .. "(" .. table.concat((function() local t = {} for i = 1, n do t[i] = fmtVal(args[i]) end return t end)(), ",") .. ")" end
				if name == "rand" then
					ok, r = true, scriptedRand(st.env, ...)
				elseif name == "gettime" then
					local a = ...
					ok, r = true, (a >= 1 and a <= 8) and ({ 56, 34, 12, 0, 18, 10, 2026, 291 })[a] or -1
				elseif name == "gettimetick" then
					local a = ...
					ok, r = true, a == 0 and 1000000 or a == 1 and 1792326896 or a == 2 and 45296 or 0
				else
					ok, r = pcall(h, st, ...)
				end
				local parts = table.create(n)
				for i = 1, n do parts[i] = fmtVal(args[i]) end
				local line = name .. "(" .. table.concat(parts, ",") .. ")"
				if ok and r ~= nil and type(r) ~= "table" then line ..= " -> " .. fmtVal(r) end
				if not lineIdx then st.replayOut[#st.replayOut + 1] = line end
				if not ok then error(r, 0) end
				return r
			end
		end
	end
	return out
end
local TRACED_EXT = traced(Ext.commands)

local function softHandler(st, name, argNodes)
	local spec = C.builtins[name]
	local parts = {}
	for i, node in argNodes do
		local ok, v
		if node[1] == K.E_VAR and specIsRef(spec, i) then
			ok, v = pcall(function()
				local idx = node[4]
				if idx then idx = I.eval(st, idx) end
				return { node[2], node[3], idx }
			end)
		else
			ok, v = pcall(I.eval, st, node)
		end
		parts[i] = ok and fmtVal(v) or "<err>"
	end
	st.replayOut[#st.replayOut + 1] = name .. "(" .. table.concat(parts, ",") .. ") !U"
	return 0
end

local function snapshotInto(snap, prefix, store)
	for name, v in store do
		if type(v) == "table" then
			for idx, x in v do
				local key = prefix .. name .. (idx ~= 0 and ("[" .. idx .. "]") or "")
				snap[key] = type(x) == "number" and fmtNum(x) or fmtStr(x)
			end
		else
			snap[prefix .. name] = type(v) == "number" and fmtNum(v) or fmtStr(v)
		end
	end
end
local function snapshot(player, npcVars, env)
	local s = {}
	snapshotInto(s, "", player.perm); snapshotInto(s, "@", player.temp); snapshotInto(s, "#", player.acc)
	snapshotInto(s, "##", player.acc2); snapshotInto(s, ".", npcVars); snapshotInto(s, "$", env.global)
	snapshotInto(s, "$@", env.gtemp); snapshotInto(s, "'", env.instance)
	return s
end

local PARAMS = { "zeny", "baselevel", "joblevel", "class", "statuspoint", "skillpoint", "baseexp", "jobexp", "karma", "manner" }

local function newPlayer(cfg, map)
	local p = W.newPlayer("poor")
	p.name = cfg.name or "Tester"; p.party = ""; p.guild = ""; p.map = (map == "-") and "prontera" or map
	for k in p.params do p.params[k] = 0 end
	p.params.baselevel = 1; p.params.joblevel = 1; p.params.sex = 1; p.params.maxweight = 20000 -- as the legacy synthetic player
	for _, k in PARAMS do if cfg[k] then p.params[k] = cfg[k] end end
	if cfg.sex then p.params.sex = cfg.sex end
	p.defaultItemCount = 0
	return p
end

-- ---- "nosc" mode: the legacy VM's operators && || ?: evaluate ALL operands (they are plain stack ops, parse_subexpr()/op_2()/op_3()
-- in script.cpp); legacy_interp short-circuits. This post-lowering rewrite makes the interpreter evaluate every operand, so that the
-- effect of that single difference can be measured. It touches only the lowered program, never legacy_interp's files.
local OP = lower.OP
local E_BIN, E_TERN = K.E_BIN, K.E_TERN
local BIN = I.BIN
local function truthy(v) return v ~= 0 and v ~= "" and v ~= nil end
BIN["&&*"] = function(a, b) return (truthy(a) and truthy(b)) and 1 or 0 end
BIN["||*"] = function(a, b) return (truthy(a) or truthy(b)) and 1 or 0 end
BIN[",*"] = function(a, b) return { a, b } end
BIN["?:*"] = function(c, pair) if truthy(c) then return pair[1] end return pair[2] end
local function rewrite(e, seen)
	if type(e) ~= "table" or seen[e] then return end
	seen[e] = true
	local k = e[1]
	if type(k) == "table" then for _, x in e do rewrite(x, seen) end return end
	if k == E_BIN then
		if e[2] == "&&" then e[2] = "&&*" elseif e[2] == "||" then e[2] = "||*" end
		rewrite(e[3], seen); rewrite(e[4], seen)
	elseif k == E_TERN then
		local c, a, b = e[2], e[3], e[4]
		rewrite(c, seen); rewrite(a, seen); rewrite(b, seen)
		e[1] = E_BIN; e[2] = "?:*"; e[3] = c; e[4] = { E_BIN, ",*", a, b }
	else
		for i = 2, #e do if type(e[i]) == "table" then rewrite(e[i], seen) end end
	end
end
local function rewriteProg(prog)
	local seen = {}
	for _, ins in prog.code do
		local op = ins[1]
		if op == OP.CALLSUB or op == OP.CALLFUNC then
			rewrite(ins[2], seen); rewrite(ins[3], seen)
		else
			rewrite(ins[2], seen)
		end
	end
end

-- registries of function scripts (shared by all cases of a process), one per mode
local registries = {}
local function loadFuncs(uf, mode)
	local funcs, n, failed = {}, 0, 0
	for _, fi in C.setfiles("re") do
		local text = C.text(fi)
		if string.find(text, "function\tscript", 1, true) then
			local r = C.ds.split(text)
			for _, d in r.decls do
				if d.kind == "function" then
					local ast = parser.parse(d.body, C.builtins, uf)
					local prog = ast and lower.lower(ast, { name = d.name, builtins = C.builtins, userfuncs = uf })
					if prog then
						if mode == "nosc" then rewriteProg(prog) end
						funcs[string.lower(d.name)] = prog; n += 1
					else failed += 1 end
				end
			end
		end
	end
	return funcs, n, failed
end
local function registry(mode)
	local r = registries[mode]
	if not r then
		local uf = C.userfuncs()
		local funcs, n, failed = loadFuncs(uf, mode)
		r = { uf = uf, funcs = funcs, n = n, failed = failed }
		registries[mode] = r
	end
	return r
end
function T.setup(mode)
	local r = registry(mode or "asis")
	return r.n, r.failed
end

-- find and lower one NPC. returns prog, mapname, line or nil, err
function T.loadNpc(path: string, npcName: string, mode: string)
	local registry = registry(mode or "asis")
	local fi = R.fileIndex(path)
	if not fi then return nil, "file-not-in-manifest" end
	local text = C.text(fi)
	local r = C.ds.split(text)
	for _, d in r.decls do
		if d.kind == "script" and d.name == npcName then
			local ast, info = parser.parse(d.body, C.builtins, registry.uf)
			if not ast then return nil, "parse:" .. tostring(info.cause) end
			local prog, lerr = lower.lower(ast, { name = d.name, file = path, builtins = C.builtins, userfuncs = registry.uf })
			if not prog then return nil, "lower:" .. tostring(lerr.cause) end
			prog.baseLine = C.ds.lineof(text, d.pos)
			if mode == "nosc" then rewriteProg(prog) end
			local map = string.match(d.w1 or "", "^([^,]*)") or "prontera"
			
			return prog, map
		end
	end
	return nil, "npc-not-found"
end

local function errTag(e)
	local s = tostring(e.type)
	if e.cmd then s ..= ":" .. e.cmd end
	return s
end

-- Run one case; returns array of trace lines (same format as the legacy hook writes).
-- case: { path, npc, answers = {string}, player = {..}, cap = 60 }
function T.runCase(case)
	local mode = case.mode or "asis"
	local prog, map = T.loadNpc(case.path, case.npc, mode)
	if not prog then return { "#end npc-not-found" }, nil, map end
	local env = W.newEnv(registry(mode).funcs, 12345)
	local npcVars = {}
	env.npcvars[prog.name] = npcVars
	local npc = { vars = npcVars, name = prog.name, map = (map == "-") and "" or map }
	local function mkState(player, out)
		local st = I.newState{ player = player, env = env, npc = npc, commands = TRACED_EXT, unsupported = softHandler,
			counts = {}, limit = 500000, glimit = 2048 }
		st.baseLimit, st.baseGlimit = st.limit, st.glimit
		st.replayOut = out
		return st
	end
	-- start state: either seeded from the legacy VM (case.init = { {key, value}, ... }: '$', '$@' and this NPC's '.' variables as the
	-- real server left them after its OnInit run), or by running this NPC's own OnInit here with no player (untraced)
	local initLabel = not case.init and prog.labels["oninit"]
	if case.init then
		for _, kv in case.init do
			local scope, name, idx = I.parseVarName(kv[1])
			local store = scope == "$" and env.global or scope == "$@" and env.gtemp or scope == "." and npcVars or nil
			if store then I.setvarIn(store, name, idx, kv[2]) end
		end
	end
	if initLabel then
		local scratch = mkState(newPlayer({}, map), {})
		local co = I.spawn(scratch, prog, initLabel, {})
		local guard = 0
		while true do
			local ok, kind, payload = coroutine.resume(co, nil)
			if not ok or coroutine.status(co) == "dead" then break end
			guard += 1
			if guard > 50 then break end
		end
	end
	local player = newPlayer(case.player or {}, map)
	local before = snapshot(player, npcVars, env)
	local lines = {}
	local st = mkState(player, lines)
	local entry = 1
	if case.label then entry = prog.labels[string.lower(case.label)]; if not entry then return { "#end label-not-found" }, nil, prog end end
	local co = I.spawn(st, prog, entry, {})
	local answers = case.answers or {}
	local ai = 0
	local cap = case.cap or 60
	local inter = 0
	local reason
	local answer
	while true do
		local ok, kind, payload = coroutine.resume(co, answer)
		if not ok then reason = "error:HOST:" .. tostring(kind); break end
		if coroutine.status(co) == "dead" then
			if kind == "end" then reason = "end" else reason = "error:" .. errTag(payload or {}); st.replayErr = payload end
			break
		end
		inter += 1
		if inter > cap then reason = "cap"; break end
		answer = nil
		local tok
		if kind == "menu" or kind == "select" or kind == "prompt" or kind == "input" then
			ai += 1
			tok = answers[ai]
		end
		if kind == "menu" then
			-- label-form menu: recorded here (completion), visible option texts only
			local parts = {}
			for i, s in payload do parts[i] = fmtStr(s) end
			lines[#lines + 1] = "menu(" .. table.concat(parts, ",") .. ")"
		end
		if kind == "menu" or kind == "select" or kind == "prompt" then
			local n = #payload
			local pick = tok and tonumber(tok) or 1
			if pick <= 0 then answer = nil
			else answer = math.min(math.floor(pick), n) end
		elseif kind == "input" then
			if payload.str then answer = tok or "test" else answer = tok and tonumber(tok) or 1 end
		elseif kind == "sleep" or kind == "sleep2" then
			env.clock += payload
		end
	end
	lines[#lines + 1] = "#end " .. reason
	-- final state: delta of variable stores + absolute params / items
	local after = snapshot(player, npcVars, env)
	local state = {}
	for k, v in after do if before[k] ~= v then state[#state + 1] = "=" .. k .. "=" .. v end end
	for k in before do if after[k] == nil then state[#state + 1] = "=" .. k .. "=<unset>" end end
	for _, k in PARAMS do
		local v = player.params[k]
		if v and v ~= 0 then state[#state + 1] = "%" .. k .. "=" .. fmtNum(v) end
	end
	for id, amount in player.inv do
		if amount ~= 0 then state[#state + 1] = "+item " .. fmtNum(id) .. "=" .. fmtNum(amount) end
	end
	table.sort(state)
	for _, s in state do lines[#lines + 1] = s end
	return lines, st, prog
end

return T
