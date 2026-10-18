--!nonstrict
--!optimize 2
-- Step 3a: AST/flat-op interpreter hosted as a coroutine. Explicit heap frame stack, `.@` locals per frame,
-- blocking commands are coroutine.yield, command table pluggable.
local P = require("./parser")
local Lw = require("./lower")
local K = P.K
local E_NUM, E_STR, E_VAR, E_CALL, E_BIN, E_UN, E_TERN, E_ASSIGN, E_INCDEC, E_NEXTLINE =
	K.E_NUM, K.E_STR, K.E_VAR, K.E_CALL, K.E_BIN, K.E_UN, K.E_TERN, K.E_ASSIGN, K.E_INCDEC, K.E_NEXTLINE
local E_PARAM = Lw.E_PARAM
local E_TMPSET = Lw.E_TMPSET
local OP = Lw.OP
local OP_EXPR, OP_JZ, OP_JMP, OP_JNZ, OP_SWITCH, OP_GOTO, OP_CALLSUB, OP_CALLFUNC, OP_RETURN, OP_END, OP_MENU =
	OP.EXPR, OP.JZ, OP.JMP, OP.JNZ, OP.SWITCH, OP.GOTO, OP.CALLSUB, OP.CALLFUNC, OP.RETURN, OP.END, OP.MENU
local byte, floor, ceil, fmod, format = string.byte, math.floor, math.ceil, math.fmod, string.format
local bit_and, bit_or, bit_xor, bit_lsh, bit_rsh = bit32.band, bit32.bor, bit32.bxor, bit32.lshift, bit32.arshift

local M = {}
local END = setmetatable({}, { __tostring = function() return "<END>" end })
M.END = END
M.MAX_DEPTH = 2048

local function terr(st, t: string, fields)
	fields = fields or {}
	fields.type = t
	error(fields, 0)
end
M.terr = terr

-- a command that returns SCRIPT_CMD_FAILURE in the legacy VM calls this (host harnesses read st.cmdFailed; the interpreter keeps going
-- unless the handler also ends the script, as the legacy command does with `st->state = END`)
function M.cmdFail(st)
	st.cmdFailed = true
end

local function tostr(v)
	if type(v) == "number" then
		if v == floor(v) then return format("%d", v) end
		return tostring(v)
	end
	return v
end
M.tostr = tostr
local function tonum(v)
	if type(v) == "number" then return v end
	return tonumber(v) or 0
end

---------------------------------------------------------------------------------------------
-- variable stores
---------------------------------------------------------------------------------------------
local function scopeTable(st, scope)
	if scope == ".@" then return st.frame.locals
	elseif scope == "" then return st.player.perm
	elseif scope == "@" then return st.player.temp
	elseif scope == "." then return st.npc.vars
	elseif scope == "$" then return st.env.global
	elseif scope == "$@" then return st.env.gtemp
	elseif scope == "#" then return st.player.acc
	elseif scope == "##" then return st.player.acc2
	elseif scope == "'" then
		-- instance variables exist only while the script runs inside an instance (legacy: reads default, writes are errors)
		if (st.env.instanceId or 0) > 0 then return st.env.instance end
		return {}
	end
	terr(st, "BAD_SCOPE", { scope = scope })
end

local function getvarIn(s, name, idx)
	local v = s[name]
	if type(v) == "table" then v = v[idx or 0]
	elseif idx and idx ~= 0 then v = nil end
	if v == nil then
		if byte(name, #name) == 36 then return "" end
		return 0
	end
	return v
end
local function getvar(st, scope, name, idx)
	return getvarIn(scopeTable(st, scope), name, idx)
end
M.getvar = getvar

local function setvarIn(s, name, idx, val)
	if byte(name, #name) == 36 then
		if type(val) ~= "string" then val = tostr(val) end
		if val == "" then val = nil end
	else
		if type(val) ~= "number" then val = tonumber(val) or 0 end
		if val == 0 then val = nil end
	end
	local cur = s[name]
	if idx and idx ~= 0 then
		if type(cur) ~= "table" then
			cur = { [0] = cur }
			s[name] = cur
		end
		cur[idx] = val
	elseif type(cur) == "table" then
		cur[0] = val
	else
		s[name] = val
	end
end
local function setvar(st, scope, name, idx, val)
	return setvarIn(scopeTable(st, scope), name, idx, val)
end
M.setvar = setvar
M.getvarIn, M.setvarIn = getvarIn, setvarIn

function M.arraysize(st, scope, name)
	local s = scopeTable(st, scope)
	local v = s[name]
	if type(v) ~= "table" then return v ~= nil and 1 or 0 end
	local mx = -1
	for k in v do if k > mx then mx = k end end
	return mx + 1
end

local function getparam(st, name)
	local v = st.player.params[name]
	if v == nil then return 0 end
	return v
end

---------------------------------------------------------------------------------------------
-- expression evaluation
---------------------------------------------------------------------------------------------
local eval

-- array subscript: the legacy getelementofarray() ends the script (st->state = END, SCRIPT_CMD_FAILURE) for an index < 0 or >= SCRIPT_MAX_ARRAYSIZE
local function evalIndex(st, ie)
	local i = eval(st, ie)
	if type(i) ~= "number" then i = tonumber(i) or 0 end
	if i < 0 or i >= 4294967295 then error(END, 0) end
	return i
end

local BIN = {}
BIN["+"] = function(a, b)
	if type(a) == "string" or type(b) == "string" then return tostr(a) .. tostr(b) end
	return a + b
end
BIN["-"] = function(a, b) return tonum(a) - tonum(b) end
BIN["*"] = function(a, b) return tonum(a) * tonum(b) end
BIN["/"] = function(a, b)
	a, b = tonum(a), tonum(b)
	if b == 0 then error({ type = "DIV_BY_ZERO" }, 0) end
	local q = a / b
	return q >= 0 and floor(q) or ceil(q)
end
BIN["%"] = function(a, b)
	a, b = tonum(a), tonum(b)
	if b == 0 then error({ type = "DIV_BY_ZERO" }, 0) end
	return fmod(a, b)
end
BIN["=="] = function(a, b) return a == b and 1 or 0 end
BIN["!="] = function(a, b) return a ~= b and 1 or 0 end
BIN["<"] = function(a, b) if type(a) == type(b) then return a < b and 1 or 0 end return tonum(a) < tonum(b) and 1 or 0 end
BIN["<="] = function(a, b) if type(a) == type(b) then return a <= b and 1 or 0 end return tonum(a) <= tonum(b) and 1 or 0 end
BIN[">"] = function(a, b) if type(a) == type(b) then return a > b and 1 or 0 end return tonum(a) > tonum(b) and 1 or 0 end
BIN[">="] = function(a, b) if type(a) == type(b) then return a >= b and 1 or 0 end return tonum(a) >= tonum(b) and 1 or 0 end
-- 64-bit bitwise operators on doubles (legacy ints are int64; bit32 is 32-bit only). Operands are split into a signed high part and an
-- unsigned 32-bit low part (two's complement), combined per half. Exact for |x| < 2^53.
local TWO32 = 4294967296
local function split64(x)
	x = x >= 0 and floor(x) or ceil(x)
	local hi = floor(x / TWO32)
	local lo = x - hi * TWO32
	return hi, lo
end
local function bitop(f)
	return function(a, b)
		a, b = tonum(a), tonum(b)
		if a >= 0 and b >= 0 and a < TWO32 and b < TWO32 then return f(a, b) end
		local ah, al = split64(a)
		local bh, bl = split64(b)
		local hi = f(ah % TWO32, bh % TWO32)
		if hi >= 2147483648 then hi -= TWO32 end -- back to signed high part
		return hi * TWO32 + f(al, bl)
	end
end
BIN["&"] = bitop(bit_and)
BIN["|"] = bitop(bit_or)
BIN["^"] = bitop(bit_xor)
BIN["<<"] = function(a, b)
	a, b = tonum(a), tonum(b)
	if b < 0 or b >= 64 then return 0 end
	return a * 2 ^ b
end
BIN[">>"] = function(a, b)
	a, b = tonum(a), tonum(b)
	if b < 0 or b >= 64 then return a < 0 and -1 or 0 end
	return floor(a / 2 ^ b) -- arithmetic shift
end
M.BIN = BIN

local function truthy(v)
	return v ~= 0 and v ~= "" and v ~= nil
end

local ASSIGNOP = { ["+="] = "+", ["-="] = "-", ["*="] = "*", ["/="] = "/", ["%="] = "%", ["&="] = "&", ["|="] = "|",
	["^="] = "^", ["<<="] = "<<", [">>="] = ">>", ["~="] = "&" }

function M.parseVarName(str)
	local scope, rest
	local c = string.sub(str, 1, 1)
	local two = string.sub(str, 1, 2)
	if two == ".@" then scope, rest = ".@", string.sub(str, 3)
	elseif two == "$@" then scope, rest = "$@", string.sub(str, 3)
	elseif two == "##" then scope, rest = "##", string.sub(str, 3)
	elseif c == "." then scope, rest = ".", string.sub(str, 2)
	elseif c == "$" then scope, rest = "$", string.sub(str, 2)
	elseif c == "@" then scope, rest = "@", string.sub(str, 2)
	elseif c == "#" then scope, rest = "#", string.sub(str, 2)
	elseif c == "'" then scope, rest = "'", string.sub(str, 2)
	else scope, rest = "", str end
	local name, idx = string.match(rest, "^([%w_]+%$?)%[(%d+)%]$")
	if not name then name = rest; idx = nil end
	return scope, string.lower(name), idx and tonumber(idx)
end

-- build a reference descriptor for 'r' arguments
local mkref
function mkref(st, node)
	local k = node[1]
	if k == E_VAR then
		local idx = node[4]
		if idx then idx = evalIndex(st, idx) end
		return { node[2], node[3], idx, node[2] == ".@" and st.frame.locals or nil }
	elseif false then
	elseif k == E_CALL and node[2] == "getarg" then
		-- a reference passed through callsub/callfunc (arrays/variables are passed by reference in rAthena)
		local r = st.frame.argrefs and st.frame.argrefs[eval(st, node[3][1]) + 1]
		if not r then terr(st, "BAD_REF", { node = "getarg-not-a-reference" }) end
		return r
	elseif k == E_PARAM then
		return { "param", node[2] }
	elseif k == E_CALL and node[2] == "getvariableofnpc" then
		-- reference to a '.' variable of another NPC
		local r = mkref(st, node[3][1])
		local npcname = tostr(eval(st, node[3][2]))
		local reg = st.env.npcvars
		local t = reg[npcname]
		if not t then t = {}; reg[npcname] = t end
		r[5] = t
		return r
	elseif k == E_CALL and node[2] == "getd" then
		local scope, name, idx = M.parseVarName(tostr(eval(st, node[3][1])))
		return { scope, name, idx }
	elseif k == E_CALL and node[2] == "getelementofarray" then
		local r = mkref(st, node[3][1])
		r[3] = eval(st, node[3][2])
		return r
	end
	terr(st, "BAD_REF", { node = k })
end

local function refget(st, ref)
	if ref[1] == "param" then return getparam(st, ref[2]) end
	if ref[5] then return getvarIn(ref[5], ref[2], ref[3]) end
	local lc = ref[4]
	if lc then return getvarIn(lc, ref[2], ref[3]) end
	return getvar(st, ref[1], ref[2], ref[3])
end
local function refset(st, ref, v)
	if ref[1] == "param" then st.player.params[ref[2]] = v; return end
	if ref[5] then return setvarIn(ref[5], ref[2], ref[3], v) end
	local lc = ref[4]
	if lc then return setvarIn(lc, ref[2], ref[3], v) end
	setvar(st, ref[1], ref[2], ref[3], v)
end
M.mkref, M.refget, M.refset = mkref, refget, refset

function eval(st, e)
	local k = e[1]
	if k == E_NUM or k == E_STR then
		return e[2]
	elseif k == E_VAR then
		local ie = e[4]
		if not ie and e[2] == ".@" then -- hot path: scalar frame local
			local v = st.frame.locals[e[3]]
			if type(v) == "number" or type(v) == "string" then return v end
		end
		return getvar(st, e[2], e[3], ie and evalIndex(st, ie))
	elseif k == E_BIN then
		local op = e[2]
		-- The legacy VM has no short-circuit: C_LAND / C_LOR are plain stack ops (script.cpp op_2) and BOTH operands are evaluated.
		if op == "&&" then
			local a, b = eval(st, e[3]), eval(st, e[4])
			if truthy(a) and truthy(b) then return 1 end
			return 0
		elseif op == "||" then
			local a, b = eval(st, e[3]), eval(st, e[4])
			if truthy(a) or truthy(b) then return 1 end
			return 0
		end
		local a, b = eval(st, e[3]), eval(st, e[4])
		if type(a) == "number" and type(b) == "number" then
			-- numeric fast paths
			if op == "<" then return a < b and 1 or 0
			elseif op == "+" then return a + b
			elseif op == "==" then return a == b and 1 or 0
			elseif op == "-" then return a - b
			elseif op == "*" then return a * b
			elseif op == ">" then return a > b and 1 or 0
			elseif op == "<=" then return a <= b and 1 or 0
			elseif op == ">=" then return a >= b and 1 or 0
			elseif op == "!=" then return a ~= b and 1 or 0
			end
		end
		return BIN[op](a, b)
	elseif k == E_CALL then
		local name, args = e[2], e[3]
		local n = #args
		if name == "callsub" or name == "callfunc" then
			-- lower.luau hoists every callsub/callfunc/user-function call out of expressions (all operands of && || ?: are
			-- evaluated, so hoisting is exact); reaching this means an un-lowered AST was handed to the interpreter.
			terr(st, "NESTED_CALL", { cmd = name })
		end
		local h = st.commands[name]
		if not h then
			h = st.unsupported
			if not h then terr(st, "UNSUPPORTED", { cmd = name }) end
			return h(st, name, args)
		end
		local refs = e[5]
		local av = {}
		for i = 1, n do
			if refs and refs[i] then av[i] = mkref(st, args[i]) else av[i] = eval(st, args[i]) end
		end
		st.argNodes = args -- for handlers whose targets are variables although the arg spec has no 'r' (sscanf)
		local r = h(st, table.unpack(av, 1, n))
		if r == nil then return 0 end
		return r
	elseif k == E_ASSIGN then
		local op, tgt = e[2], e[3]
		local val = eval(st, e[4])
		if tgt[1] == E_PARAM then
			if op ~= "=" then val = BIN[ASSIGNOP[op]](getparam(st, tgt[2]), val) end
			st.player.params[tgt[2]] = val
			return val
		end
		local scope, name = tgt[2], tgt[3]
		local idx = tgt[4]
		if idx then idx = evalIndex(st, idx) end
		if op ~= "=" then val = BIN[ASSIGNOP[op]](getvar(st, scope, name, idx), val) end
		if not idx and scope == ".@" and type(val) == "number" and val ~= 0 then
			local loc = st.frame.locals
			local cur = loc[name]
			if type(cur) ~= "table" and byte(name, #name) ~= 36 then loc[name] = val; return val end
		end
		setvar(st, scope, name, idx, val)
		return val
	elseif k == E_UN then
		local op, v = e[2], eval(st, e[3])
		if op == "-" then return -tonum(v)
		elseif op == "!" then return truthy(v) and 0 or 1
		else return -tonum(v) - 1 end -- ~x on int64
	elseif k == E_TERN then
		-- legacy C_OP3: condition and both branches are evaluated (stack op), then one is selected
		local c, a, b = eval(st, e[2]), eval(st, e[3]), eval(st, e[4])
		if truthy(c) then return a end
		return b
	elseif k == E_INCDEC then
		local tgt = e[4]
		if tgt[1] == E_PARAM then
			local old = getparam(st, tgt[2]); st.player.params[tgt[2]] = old + e[2]
			return e[3] and old or old + e[2]
		end
		local idx = tgt[4]
		if idx then idx = evalIndex(st, idx) end
		local old = getvar(st, tgt[2], tgt[3], idx)
		local new = old + e[2]
		if not idx and tgt[2] == ".@" and new ~= 0 then
			local loc = st.frame.locals
			if type(loc[tgt[3]]) == "number" or loc[tgt[3]] == nil then
				loc[tgt[3]] = new
				if e[3] then return old end
				return new
			end
		end
		setvar(st, tgt[2], tgt[3], idx, new)
		if e[3] then return old end
		return new
	elseif k == E_PARAM then
		return getparam(st, e[2])
	elseif k == E_TMPSET then
		local v = eval(st, e[3])
		st.frame.locals[e[2]] = v -- hoisted command result: stored raw, keeps its type
		return v
	elseif k == E_NEXTLINE then
		return -1
	end
	terr(st, "BAD_NODE", { kind = k })
end
M.eval = eval

---------------------------------------------------------------------------------------------
-- frames and the main loop
---------------------------------------------------------------------------------------------
local function pushFrame(st, prog, pc, args, dst)
	local d = st.depth + 1
	if d > M.MAX_DEPTH then terr(st, "CALL_DEPTH", { depth = d }) end
	local fr = { prog = prog, pc = pc, locals = {}, args = args or {}, dst = dst }
	local cm = st.covmap
	if cm then
		local c = cm[prog]
		if not c then c = {}; cm[prog] = c end
		fr.cov = c
	end
	st.stack[d] = fr
	st.depth = d
	st.frame = fr
	if d > st.maxDepth then st.maxDepth = d end
	return fr
end

function M.findFunc(st, name)
	local p = st.env.funcs[string.lower(name)]
	if not p then terr(st, "UNDEFINED_FUNCTION", { name = name }) end
	return p
end

-- split a menu/select option string on ':' keeping empty pieces (they still consume an option number)
function M.splitOptions(str)
	local out, n, i = {}, 0, 1
	while true do
		local j = string.find(str, ":", i, true)
		n += 1
		if not j then out[n] = string.sub(str, i); break end
		out[n] = string.sub(str, i, j - 1)
		i = j + 1
	end
	return out
end

local function yield(st, kind, payload)
	st.ops = 0
	st.gotos = 0
	st.yields += 1
	return coroutine.yield(kind, payload)
end
M.yield = yield

local runFrames

function runFrames(st)
	local stack = st.stack
	local commands = st.commands
	while true do
		local fr = stack[st.depth]
		local code = fr.prog.code
		local pc = fr.pc
		local cov = fr.cov
		st.frame = fr
		while true do
			local ins = code[pc]
			fr.pc = pc
			if cov then cov[pc] = true end
			pc += 1
			local op = ins[1]
			if op == OP_EXPR then
				eval(st, ins[2])
			elseif op == OP_JZ then
				local v = eval(st, ins[2])
				local jump = (v == 0 or v == "")
				local orc = st.oracle
				if orc and orc(2) == 2 then jump = not jump end
				if jump then
					pc = ins[3]
					local g = st.gotos + 1; st.gotos = g
					if g > st.glimit then terr(st, "BUDGET_EXCEEDED", { what = "gotos" }) end
				end
			elseif op == OP_JMP then
				pc = ins[2]
				local g = st.gotos + 1; st.gotos = g
				if g > st.glimit then terr(st, "BUDGET_EXCEEDED", { what = "gotos" }) end
			elseif op == OP_JNZ then
				local v = eval(st, ins[2])
				local jump = (v ~= 0 and v ~= "")
				local orc = st.oracle
				if orc and orc(2) == 2 then jump = not jump end
				if jump then
					pc = ins[3]
					local g = st.gotos + 1; st.gotos = g
					if g > st.glimit then terr(st, "BUDGET_EXCEEDED", { what = "gotos" }) end
				end
			elseif op == OP_SWITCH then
				local nat = ins[3][eval(st, ins[2])] or ins[4]
				local orc = st.oracle
				if orc then
					local alts = ins[6]
					if not alts then
						alts = {}
						local seen = {}
						for _, t in ins[3] do if not seen[t] then seen[t] = true; alts[#alts + 1] = t end end
						if not seen[ins[4]] then alts[#alts + 1] = ins[4] end
						ins[6] = alts
					end
					local k = orc(#alts)
					if k > 1 then
						-- 1 = natural; others enumerate the remaining targets in order
						local j = 0
						for _, t in alts do
							if t ~= nat then j += 1; if j == k - 1 then nat = t; break end end
						end
					end
				end
				pc = nat
			elseif op == OP_GOTO then
				local t = eval(st, ins[2])
				if type(t) ~= "number" then terr(st, "BAD_LABEL", { value = tostring(t) }) end
				pc = t
				local g = st.gotos + 1; st.gotos = g
				if g > st.glimit then terr(st, "BUDGET_EXCEEDED", { what = "gotos" }) end
			elseif op == OP_CALLSUB or op == OP_CALLFUNC then
				local target = eval(st, ins[2])
				local a = ins[3]
				local av = table.create(#a)
				local arefs
				for i = 1, #a do
					local node = a[i]
					if node[1] == E_VAR then
						-- a variable argument is passed as a reference (index expression evaluated ONCE); its value is read through it
						local ref = mkref(st, node)
						arefs = arefs or {}; arefs[i] = ref
						av[i] = M.refget(st, ref)
					else
						av[i] = eval(st, node)
					end
				end
				fr.pc = pc -- return address
				local prog, entry
				if op == OP_CALLSUB then
					prog, entry = fr.prog, target
					if type(entry) ~= "number" then terr(st, "BAD_LABEL", { value = tostring(entry) }) end
				else
					prog, entry = M.findFunc(st, target), 1
				end
				st.callCount += 1
				pushFrame(st, prog, entry, av, ins[4]).argrefs = arefs
				local g = st.gotos + 1; st.gotos = g
				if g > st.glimit then terr(st, "BUDGET_EXCEEDED", { what = "gotos" }) end
				break
			elseif op == OP_RETURN then
				local v = 0
				if ins[2] then v = eval(st, ins[2]) end
				local dst = fr.dst
				stack[st.depth] = nil
				st.depth -= 1
				if st.depth == 0 then return v end -- returned from the entry frame: script ends
				local caller = stack[st.depth]
				st.frame = caller
				if dst then caller.locals[dst] = v end -- hoisted call result: raw store, keeps its type (string or number)
				break
			elseif op == OP_END then
				error(END, 0)
			elseif op == OP_MENU then
				local a = ins[2]
				local targets, vis, ids, g = {}, {}, {}, 0
				for i = 1, #a, 2 do
					local text = eval(st, a[i])
					local tgt = a[i + 1] and eval(st, a[i + 1]) or -1
					for _, piece in M.splitOptions(tostr(text)) do
						g += 1
						targets[g] = tgt
						if piece ~= "" then vis[#vis + 1] = piece; ids[#ids + 1] = g end
					end
				end
				local k
				if #vis > 0 then k = yield(st, "menu", vis) end
				if k == nil then error(END, 0) end -- cancel (or no visible option) ends the script
				local choice = ids[k]
				if not choice then terr(st, "BAD_MENU_CHOICE", { k = k }) end
				st.player.temp["menu"] = choice
				local t = targets[choice]
				if t ~= -1 then
					pc = t
					st.gotos += 1
				end
			else
				terr(st, "BAD_OP", { op = op })
			end
		end
	end
end

---------------------------------------------------------------------------------------------
-- state / spawn
---------------------------------------------------------------------------------------------
function M.newState(opts)
	local st = {
		stack = {}, depth = 0, frame = nil,
		player = opts.player, env = opts.env, npc = opts.npc or { vars = {}, name = "?" },
		commands = opts.commands,
		unsupported = opts.unsupported, -- optional soft handler: function(st, name, args)
		ops = 0, gotos = 0, limit = opts.limit or 10000, glimit = opts.glimit or 2048,
		yields = 0, callCount = 0, nestedCalls = 0, maxDepth = 0,
		log = opts.log, trace = opts.trace, covmap = opts.covmap,
		counts = opts.counts or {},
		retval = 0,
	}
	return st
end

-- returns a coroutine; resume it with the answer to the previous yield.
-- On completion the coroutine returns "end" or "error", err.
function M.spawn(st, prog, entryPc, args)
	pushFrame(st, prog, entryPc, args or {}, nil)
	return coroutine.create(function()
		local ok, e = xpcall(runFrames, function(err)
			if type(err) == "table" then
				if err ~= END and not err.pc then
					local fr = st.frame
					if fr then
						err.pc = fr.pc
						local ins = fr.prog.code[fr.pc]
						err.line = ins and ins[5]
						err.npc = fr.prog.name
					end
					err.depth = st.depth
				end
				return err
			end
			if string.find(tostring(err), "stack overflow", 1, true) then
				return { type = "HOST_STACK_OVERFLOW", msg = tostring(err), depth = st.depth }
			end
			return { type = "INTERNAL", msg = tostring(err), trace = debug.traceback("", 2) }
		end, st)
		if ok then return "end", e end
		if e == END then return "end" end
		return "error", e
	end)
end

return M
