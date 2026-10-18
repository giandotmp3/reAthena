--!nonstrict
--!optimize 2
-- Step 2b: lower the statement AST to a flat op list with jump targets (pc assignments).
--   goto / callsub / switch / loops / if become pc jumps; labels map to pcs; no recursion in statements.
local P = require("./parser")
local K = P.K
local E_NUM, E_STR, E_VAR, E_CALL, E_BIN, E_UN, E_TERN, E_ASSIGN, E_INCDEC, E_NEXTLINE =
	K.E_NUM, K.E_STR, K.E_VAR, K.E_CALL, K.E_BIN, K.E_UN, K.E_TERN, K.E_ASSIGN, K.E_INCDEC, K.E_NEXTLINE
local S_EXPR, S_IF, S_WHILE, S_DO, S_FOR, S_SWITCH, S_BLOCK, S_LABEL, S_CASE, S_DEFAULT, S_BREAK, S_CONTINUE, S_FUNCDEF, S_FUNCDECL =
	K.S_EXPR, K.S_IF, K.S_WHILE, K.S_DO, K.S_FOR, K.S_SWITCH, K.S_BLOCK, K.S_LABEL, K.S_CASE, K.S_DEFAULT, K.S_BREAK,
	K.S_CONTINUE, K.S_FUNCDEF, K.S_FUNCDECL

local M = {}
-- extra expression kind produced here
local E_PARAM = 11
local E_LABELREF = 12 -- patched to E_NUM after all labels are known
local E_TMPSET = 13 -- {E_TMPSET, name, expr}: raw store of a hoisted command result into a frame temp (keeps its type)
M.E_TMPSET = E_TMPSET
M.E_PARAM = E_PARAM
-- opcodes
local OP_EXPR, OP_JZ, OP_JMP, OP_JNZ, OP_SWITCH, OP_GOTO, OP_CALLSUB, OP_CALLFUNC, OP_RETURN, OP_END, OP_MENU =
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
M.OP = { EXPR = OP_EXPR, JZ = OP_JZ, JMP = OP_JMP, JNZ = OP_JNZ, SWITCH = OP_SWITCH, GOTO = OP_GOTO, CALLSUB = OP_CALLSUB,
	CALLFUNC = OP_CALLFUNC, RETURN = OP_RETURN, END = OP_END, MENU = OP_MENU }

local consts, params = nil, nil
local function loadconsts()
	if not consts then
		local c = require("./consts")
		consts, params = c.consts, c.params
	end
end

local function lerr(msg: string, line: number?): never
	error({ lower = true, cause = msg, line = line }, 0)
end

-- state for the current lowering
local code, labels, labelSet, loops, sw, pendingRefs, tmpN, BUILTINS, userfuncs, nest, synIdx
M.switchCompat = true -- compat flag (default ON): reproduce the legacy compare-chain switch incl. the $@__swN_val global it leaves behind
local function nextIdx() local i = synIdx; synIdx += 1; return i end
M.ctx = nil
M.nestedLabels, M.nestedCallSites, M.dynamicTargets, M.funcDefs = {}, {}, {}, {}

local function emit(op: number, a, b, c, d): number
	code[#code + 1] = { op, a, b, c, d }
	return #code
end

local function collectLabels(list)
	for _, s in list do
		local k = s[1]
		if k == S_LABEL then labelSet[s[2]] = true
		elseif k == S_FUNCDEF then labelSet[s[2]] = true; collectLabels(s[3])
		elseif k == S_BLOCK then collectLabels(s[2])
		elseif k == S_IF then
			if s[3] then collectLabels({ s[3] }) end
			if s[4] then collectLabels({ s[4] }) end
		elseif k == S_WHILE then if s[3] then collectLabels({ s[3] }) end
		elseif k == S_DO then if s[2] then collectLabels({ s[2] }) end
		elseif k == S_FOR then if s[5] then collectLabels({ s[5] }) end
		elseif k == S_SWITCH then collectLabels(s[3])
		end
	end
end

local CONTROL = { callsub = true, callfunc = true }
local function isCallLike(name: string): boolean
	return CONTROL[name] or userfuncs[name] or labelSet[name] and not BUILTINS[name]
end

-- resolve identifier nodes (labels / constants / params) in place; mark ref args
local function resolve(e)
	local k = e[1]
	if k == E_VAR then
		if e[4] then resolve(e[4]); return end
		if e[2] == "" then
			local name = e[3]
			if labelSet[name] then
				e[1] = E_LABELREF; e[2] = name; pendingRefs[#pendingRefs + 1] = e
			else
				local c = consts[name]
				if c ~= nil then e[1] = E_NUM; e[2] = c; e[3] = nil
				elseif params[name] then e[1] = E_PARAM; e[2] = name; e[3] = nil end
			end
		end
	elseif k == E_BIN then resolve(e[3]); resolve(e[4])
	elseif k == E_UN then resolve(e[3])
	elseif k == E_TERN then resolve(e[2]); resolve(e[3]); resolve(e[4])
	elseif k == E_ASSIGN then resolve(e[3]); resolve(e[4])
	elseif k == E_INCDEC then resolve(e[4])
	elseif k == E_CALL then
		local name, args = e[2], e[3]
		local spec = BUILTINS[name]
		for i = 1, #args do
			local a = args[i]
			resolve(a)
		end
		if spec then
			local refs
			for i = 1, #args do
				if string.sub(spec, i, i) == "r" then refs = refs or {}; refs[i] = true end
			end
			e[5] = refs or false
		end
	end
end

-- hoist callsub/callfunc/user-function calls out of an expression into preceding ops.
-- The legacy VM evaluates EVERY operand of && || ?: (they are stack ops, script.cpp C_LAND/C_LOR/C_OP3 via op_2/op_3), so a call
-- under them is unconditionally executed and hoisting is semantically exact: there is no "conditional" context any more and no
-- host-stack recursion path for callsub/callfunc in the interpreter.
local hoist
local nHoisted = 0
local function callToOp(e, dst)
	local name, args = e[2], e[3]
	if name == "callsub" then
		local a = {}
		for i = 2, #args do a[i - 1] = args[i] end
		emit(OP_CALLSUB, args[1], a, dst, e[4])
	elseif name == "callfunc" then
		local a = {}
		for i = 2, #args do a[i - 1] = args[i] end
		emit(OP_CALLFUNC, args[1], a, dst, e[4])
	elseif labelSet[name] and not BUILTINS[name] then
		emit(OP_CALLSUB, { E_LABELREF, name }, args, dst, e[4])
		pendingRefs[#pendingRefs + 1] = code[#code][2]
	else
		emit(OP_CALLFUNC, { E_STR, name }, args, dst, e[4])
	end
end

-- Order of effects. The legacy VM runs a command the moment the expression walk reaches it (operands left to right, calls included),
-- while variables are read lazily at the operator. So hoisting a callsub/callfunc out of an expression must keep every command
-- call that is evaluated BEFORE it ahead of it as well: all E_CALL nodes up to and including the last callsub-like one (post-order =
-- evaluation order) are hoisted into temporaries, the rest of the expression stays inline.
local hoistCur, hoistLast = 0, 0

local function countCalls(x)
	-- same traversal order as hoist(); returns (#E_CALL nodes in post-order, post-order index of the last call-like one)
	local n, last = 0, 0
	local function walk(e)
		local k = e[1]
		if k == E_CALL then
			for i = 1, #e[3] do walk(e[3][i]) end
			n += 1
			if isCallLike(e[2]) then last = n end
		elseif k == E_BIN then walk(e[3]); walk(e[4])
		elseif k == E_UN then walk(e[3])
		elseif k == E_TERN then walk(e[2]); walk(e[3]); walk(e[4])
		elseif k == E_ASSIGN then
			if e[3][4] then walk(e[3][4]) end
			walk(e[4])
		elseif k == E_INCDEC then
			if e[4][4] then walk(e[4][4]) end
		elseif k == E_VAR then
			if e[4] then walk(e[4]) end
		end
	end
	walk(x)
	return n, last
end

function hoist(e)
	local k = e[1]
	if k == E_CALL then
		local name, args = e[2], e[3]
		for i = 1, #args do args[i] = hoist(args[i]) end
		hoistCur += 1
		if hoistCur > hoistLast then return e end
		tmpN += 1
		local tn = "\0t" .. tmpN
		if isCallLike(name) then
			callToOp(e, tn)
			nHoisted += 1
		else
			emit(OP_EXPR, { E_TMPSET, tn, e }, nil, nil, e[4])
		end
		return { E_VAR, ".@", tn }
	elseif k == E_BIN then
		e[3] = hoist(e[3]); e[4] = hoist(e[4])
	elseif k == E_UN then e[3] = hoist(e[3])
	elseif k == E_TERN then
		e[2] = hoist(e[2]); e[3] = hoist(e[3]); e[4] = hoist(e[4])
	elseif k == E_ASSIGN then
		if e[3][4] then e[3][4] = hoist(e[3][4]) end
		e[4] = hoist(e[4])
	elseif k == E_INCDEC then
		if e[4][4] then e[4][4] = hoist(e[4][4]) end
	elseif k == E_VAR then
		if e[4] then e[4] = hoist(e[4]) end
	end
	return e
end

local function prepList(args)
	-- a statement-level call: its arguments are one left-to-right evaluation sequence
	for i = 1, #args do resolve(args[i]) end
	local total, lastAll = 0, 0
	for i = 1, #args do
		local n, l = countCalls(args[i])
		if l > 0 then lastAll = total + l end
		total += n
	end
	if lastAll == 0 then return end
	hoistCur, hoistLast = 0, lastAll
	for i = 1, #args do args[i] = hoist(args[i]) end
end

local function prep(e)
	resolve(e)
	local _, last = countCalls(e)
	if last == 0 then return e end
	hoistCur, hoistLast = 0, last
	return hoist(e)
end

local lowerStmt
local function lowerList(list)
	for i = 1, #list do lowerStmt(list[i]) end
end

local function patch(pcs, target)
	for _, pc in pcs do
		local ins = code[pc]
		if ins[1] == OP_JZ or ins[1] == OP_JNZ then ins[3] = target else ins[2] = target end
	end
end

function lowerStmt(s)
	local k = s[1]
	if k == S_EXPR then
		local e = s[2]
		local line = s[3]
		if e[1] == E_CALL then
			local name, args = e[2], e[3]
			if name == "goto" then
				if args[1][1] ~= E_VAR or args[1][4] or not labelSet[args[1][3]] then M.dynamicTargets[#M.dynamicTargets + 1] = { ctx = M.ctx, line = line, name = name } end
				resolve(args[1])
				emit(OP_GOTO, args[1], nil, nil, line); return
			elseif name == "end" then
				emit(OP_END, nil, nil, nil, line); return
			elseif name == "return" then
				local v = args[1]
				if v then v = prep(v) end
				emit(OP_RETURN, v, nil, nil, line); return
			elseif name == "menu" then
				prepList(args)
				emit(OP_MENU, args, nil, nil, line); return
			elseif name == "callsub" or name == "callfunc" or (labelSet[name] and not BUILTINS[name]) or (userfuncs[name] and not BUILTINS[name]) then
				if name == "callsub" and (args[1][1] ~= E_VAR or args[1][4] or not labelSet[args[1][3]]) then M.dynamicTargets[#M.dynamicTargets + 1] = { ctx = M.ctx, line = line, name = name } end
				prepList(args)
				callToOp(e, nil)
				return
			end
		end
		local e2 = prep(e)
		emit(OP_EXPR, e2, nil, nil, line)
	elseif k == S_IF then
		nextIdx()
		local cond = prep(s[2])
		local jz = emit(OP_JZ, cond, 0, nil, s[5])
		nest += 1
		if s[3] then lowerStmt(s[3]) end
		if s[4] then
			local jmp = emit(OP_JMP, 0)
			code[jz][3] = #code + 1
			lowerStmt(s[4])
			nest -= 1
			code[jmp][2] = #code + 1
		else
			nest -= 1
			code[jz][3] = #code + 1
		end
	elseif k == S_WHILE then
		nextIdx()
		local top = #code + 1
		local cond = prep(s[2])
		local jz = emit(OP_JZ, cond, 0, nil, s[4])
		local L = { breaks = {}, conts = {}, cont = top }
		loops[#loops + 1] = L
		nest += 1
		if s[3] then lowerStmt(s[3]) end
		nest -= 1
		loops[#loops] = nil
		emit(OP_JMP, top)
		local fin = #code + 1
		code[jz][3] = fin
		patch(L.breaks, fin); patch(L.conts, top)
	elseif k == S_DO then
		nextIdx()
		local top = #code + 1
		local L = { breaks = {}, conts = {} }
		loops[#loops + 1] = L
		nest += 1
		if s[2] then lowerStmt(s[2]) end
		nest -= 1
		loops[#loops] = nil
		local contpc = #code + 1
		local cond = prep(s[3])
		emit(OP_JNZ, cond, top, nil, s[4])
		patch(L.breaks, #code + 1); patch(L.conts, contpc)
	elseif k == S_FOR then
		nextIdx()
		if s[2] then
			local e = prep(s[2]); emit(OP_EXPR, e, nil, nil, s[6])
		end
		local test = #code + 1
		local jz
		if s[3] then
			local cond = prep(s[3])
			jz = emit(OP_JZ, cond, 0, nil, s[6])
		end
		local L = { breaks = {}, conts = {} }
		loops[#loops + 1] = L
		nest += 1
		if s[5] then lowerStmt(s[5]) end
		nest -= 1
		loops[#loops] = nil
		local contpc = #code + 1
		if s[4] then
			local e = prep(s[4]); emit(OP_EXPR, e, nil, nil, s[6])
		end
		emit(OP_JMP, test)
		local fin = #code + 1
		if jz then code[jz][3] = fin end
		patch(L.breaks, fin); patch(L.conts, contpc)
	elseif k == S_SWITCH then
		local idx = nextIdx()
		local val = prep(s[2])
		local S = { breaks = {}, isSwitch = true }
		sw[#sw + 1] = S
		loops[#loops + 1] = S
		S.conts = nil
		nest += 1
		local function lookupKey(st)
			local key = st[2]
			if type(key) == "string" then
				local v = consts[key]
				if v == nil then lerr("unknown case constant '" .. key .. "'", st[3]) end
				key = v
			end
			return key
		end
		if M.switchCompat then
			-- Exact image of the legacy compare chain (script.cpp parse_syntax 'switch'/'case'/'default', parse_curly_close):
			--   set $@__SWn_VAL, expr;  <statements before the first case run here>
			--   case v:   [goto SWn_cJ;]  SWn_c: if (v != $@__SWn_VAL) goto SWn_(c+1);  SWn_cJ: set $@__SWn_VAL,0;  <body>
			--   default:  SWn_c: goto SWn_(c+1);  SWn_DEF: <body>
			--   close:    set $@__SWn_VAL,0; goto SWn_FIN; SWn_last: [goto SWn_DEF]; SWn_FIN:
			-- so the global $@__SWn_VAL is left behind exactly as the real VM leaves it (non-zero when no case matched), a body that falls
			-- into 'default' continues through the remaining compares with VAL=0, and 'break' jumps straight to SWn_FIN.
			-- n = the parse-order index of the construct (if/while/do/for/function{}/switch share one counter, printed in hex).
			local vname = string.format("__sw%x_val", idx)
			local function setVal(e, line) return emit(OP_EXPR, { E_ASSIGN, "=", { E_VAR, "$@", vname }, e }, nil, nil, line) end
			setVal(val, s[4])
			local count, pending, defaultPc, seen = 1, {}, nil, {}
			for _, st in s[3] do
				local sk = st[1]
				if sk == S_CASE then
					local key = lookupKey(st)
					if seen[key] then lerr("duplicate case " .. tostring(key), st[3]) end
					seen[key] = true
					local jj
					if count ~= 1 then
						jj = emit(OP_JMP, 0)
						patch(pending, #code + 1); pending = {}
					end
					pending = { emit(OP_JZ, { E_BIN, "==", { E_NUM, key }, { E_VAR, "$@", vname } }, 0, nil, st[3]) }
					if jj then code[jj][2] = #code + 1 end
					setVal({ E_NUM, 0 }, st[3])
					count += 1
				elseif sk == S_DEFAULT then
					if defaultPc then lerr("duplicate default") end
					patch(pending, #code + 1)
					pending = { emit(OP_JMP, 0) }
					defaultPc = #code + 1
					count += 1
				else
					lowerStmt(st)
				end
			end
			setVal({ E_NUM, 0 }, s[4])
			local jfin = emit(OP_JMP, 0)
			patch(pending, #code + 1)
			if defaultPc then emit(OP_JMP, defaultPc) end
			local fin = #code + 1
			code[jfin][2] = fin
			patch(S.breaks, fin)
		else
			-- compat off: table dispatch, no global left behind. Statements before the first case still run (the value is
			-- evaluated first into a hidden frame local, then the statements, then the dispatch).
			tmpN += 1
			local tn = "\0sw" .. tmpN
			emit(OP_EXPR, { E_ASSIGN, "=", { E_VAR, ".@", tn }, val }, nil, nil, s[4])
			local ins
			local function dispatch()
				if not ins then
					ins = emit(OP_SWITCH, { E_VAR, ".@", tn }, {}, 0, s[4])
					S.cases = code[ins][3]
				end
			end
			for _, st in s[3] do
				local sk = st[1]
				if sk == S_CASE then
					dispatch()
					local key = lookupKey(st)
					if S.cases[key] then lerr("duplicate case " .. tostring(key), st[3]) end
					S.cases[key] = #code + 1
				elseif sk == S_DEFAULT then
					dispatch()
					if S.default then lerr("duplicate default") end
					S.default = #code + 1
				else
					lowerStmt(st)
				end
			end
			local fin = #code + 1
			if ins then code[ins][4] = S.default or fin end
			patch(S.breaks, fin)
		end
		nest -= 1
		loops[#loops] = nil
		sw[#sw] = nil
	elseif k == S_BLOCK then
		nest += 1
		lowerList(s[2])
		nest -= 1
	elseif k == S_LABEL then
		if nest > 0 then M.nestedLabels[#M.nestedLabels + 1] = { ctx = M.ctx, line = s[3], name = s[2], depth = nest } end
		if labels[s[2]] then lerr("duplicate label " .. s[2]) end
		labels[s[2]] = #code + 1
	elseif k == S_BREAK then
		local L = loops[#loops]
		if not L then lerr("unexpected break") end
		L.breaks[#L.breaks + 1] = emit(OP_JMP, 0)
	elseif k == S_CONTINUE then
		local i = #loops
		while i > 0 and loops[i].isSwitch do i -= 1 end
		local L = loops[i]
		if not L then lerr("unexpected continue") end
		L.conts[#L.conts + 1] = emit(OP_JMP, 0)
	elseif k == S_FUNCDEF then
		nextIdx()
		local skip = emit(OP_JMP, 0)
		if labels[s[2]] then lerr("duplicate label/function " .. s[2]) end
		labels[s[2]] = #code + 1
		local saveLoops = loops
		loops = {}
		M.funcDefs[#M.funcDefs + 1] = { ctx = M.ctx, name = s[2] }
		lowerList(s[3])
		loops = saveLoops
		emit(OP_RETURN, nil)
		code[skip][2] = #code + 1
	elseif k == S_FUNCDECL then
		-- nothing
	end
end

M.stats = { hoisted = 0, nested = 0, scripts = 0, ops = 0 }

-- lower(ast, opts{builtins=, userfuncs=, name=, file=}) -> prog | nil, err
function M.lower(ast, opts)
	loadconsts()
	code, labels, labelSet, loops, sw, pendingRefs, tmpN, nest, synIdx = {}, {}, {}, {}, {}, {}, 0, 0, 0
	BUILTINS = opts.builtins or require("./builtins")
	userfuncs = opts.userfuncs or {}
	nHoisted = 0
	local ok, err = pcall(function()
		collectLabels(ast)
		lowerList(ast)
		emit(OP_END)
		for _, ref in pendingRefs do
			local pc = labels[ref[2]]
			if not pc then lerr("unresolved label " .. tostring(ref[2])) end
			ref[1] = E_NUM; ref[2] = pc
		end
	end)
	if not ok then
		if type(err) == "table" then return nil, err end
		return nil, { lower = true, cause = "internal: " .. tostring(err) }
	end
	local st = M.stats
	st.hoisted += nHoisted; st.scripts += 1; st.ops += #code
	return { code = code, labels = labels, name = opts.name, file = opts.file }
end

return M
