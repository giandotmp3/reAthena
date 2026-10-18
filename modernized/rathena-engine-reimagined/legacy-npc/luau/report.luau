--!nonstrict
-- Graceful degradation (architecture section 7): a command with no implementation raises a typed `unsupported` error at that NPC only, and
-- the corpus load reports, per file, which NPCs use commands the compatibility layer does not provide.
--
-- Static analysis over the lowered flat ops: every `E_CALL` node of every op is a command use; `callfunc "Name"` with a constant target
-- adds the commands of that function script (transitively). A command is
--   supported   present in the command table of the runtime (commands_rt -> commands_ext -> commands),
--   gated       supported but needs a permission (interp.GATED); it does not disable the NPC, running it without the grant is a typed
--               PERMISSION_DENIED, and the report lists it so an operator sees what a grant would unlock,
--   unsupported anything else: the NPC is disabled (it keeps its entry, but run() answers `unsupported` instead of starting it).
local P = require("./parser")
local Lw = require("./lower")
local I = require("./interp")
local Cmds = require("./commands_rt")

local K = P.K
local E_CALL, E_BIN, E_UN, E_TERN, E_ASSIGN, E_INCDEC, E_VAR = K.E_CALL, K.E_BIN, K.E_UN, K.E_TERN, K.E_ASSIGN, K.E_INCDEC, K.E_VAR
local OP = Lw.OP
local E_TMPSET = Lw.E_TMPSET

local M = {}

-- walks an expression tree (iteratively), calling visit(name, node) for each command call
local function walk(e, visit)
	local stack, n = { e }, 1
	while n > 0 do
		local x = stack[n]; stack[n] = nil; n -= 1
		local k = x[1]
		if k == E_CALL then
			visit(x[2], x)
			local args = x[3]
			for i = 1, #args do n += 1; stack[n] = args[i] end
		elseif k == E_BIN then n += 1; stack[n] = x[3]; n += 1; stack[n] = x[4]
		elseif k == E_UN then n += 1; stack[n] = x[3]
		elseif k == E_TERN then n += 1; stack[n] = x[2]; n += 1; stack[n] = x[3]; n += 1; stack[n] = x[4]
		elseif k == E_ASSIGN then
			if x[3][4] then n += 1; stack[n] = x[3][4] end
			n += 1; stack[n] = x[4]
		elseif k == E_INCDEC then
			if x[4][4] then n += 1; stack[n] = x[4][4] end
		elseif k == E_VAR then
			if x[4] then n += 1; stack[n] = x[4] end
		elseif k == E_TMPSET then n += 1; stack[n] = x[3]
		end
	end
end

-- calls visit(name, line) for every command a program uses, and onFunc(name) for every constant-target callfunc / user-function call
function M.eachCommand(prog, visit, onFunc)
	for _, ins in prog.code do
		local op = ins[1]
		local line = ins[5]
		local function v(name, node) visit(name, node[4] or line) end
		if op == OP.EXPR or op == OP.JZ or op == OP.JNZ or op == OP.SWITCH or op == OP.GOTO then
			walk(ins[2], v)
		elseif op == OP.RETURN then
			if ins[2] then walk(ins[2], v) end
		elseif op == OP.CALLSUB or op == OP.CALLFUNC then
			walk(ins[2], v)
			for _, a in ins[3] do walk(a, v) end
			if op == OP.CALLFUNC and ins[2][1] == K.E_STR and onFunc then onFunc(ins[2][2], line) end
		elseif op == OP.MENU then
			for _, a in ins[2] do walk(a, v) end
		end
	end
end

-- classification of one command name
function M.classify(name: string): string
	if I.GATED[name] and Cmds.commands[name] ~= nil then return "gated" end
	if Cmds.commands[name] ~= nil then return "supported" end
	if I.GATED[name] then return "gated" end -- gated and not even implemented: a grant will not help, but it is not "unsupported" by name
	return "unsupported"
end

-- Commands of one NPC: returns { unsupported = {name -> first line}, gated = {name -> true}, calls = n } following constant callfunc targets.
function M.analyze(loader, entry)
	local prog = loader:program(entry)
	if not prog then return nil, entry.loadError end
	local out = { unsupported = {}, gated = {}, used = {} }
	local seenFuncs = {}
	local function scan(p, base)
		M.eachCommand(p, function(name, line)
			local c = out.used[name]
			out.used[name] = (c or 0) + 1
			local cls = M.classify(name)
			if cls == "unsupported" then
				if not out.unsupported[name] then out.unsupported[name] = (base or 0) + (line or 1) - 1 end
			elseif cls == "gated" then
				out.gated[name] = true
			end
		end, function(fname)
			local l = string.lower(fname)
			if seenFuncs[l] then return end
			seenFuncs[l] = true
			local fe = loader.funcs[l]
			local fp = fe and loader:program(fe)
			if fp then scan(fp, fe.line) end
		end)
	end
	scan(prog, entry.line)
	return out
end

local function sortedKeys(t)
	local k = {}
	for name in t do k[#k + 1] = name end
	table.sort(k)
	return k
end

-- Report over every script NPC of the loader (or of one file): rows in file order.
--   { file, npc, line, commands = {names}, gated = {names}, error = "parse error"|nil }
-- A clone of a template is analyzed once (shared body) and reported under its own name. Programs parsed for the analysis are dropped again
-- unless opts.keep.
function M.run(loader, opts)
	opts = opts or {}
	local rows = {}
	local cache = {}
	local disabledNpcs, totalNpcs = 0, 0
	for _, entry in loader.npcList do
		if (entry.kind == "script" or entry.kind == "duplicate") and (not opts.file or entry.path == opts.file) then
			local root = loader:rootOf(entry)
			if root then
				totalNpcs += 1
				local a = cache[root]
				if a == nil then
					local wasLoaded = root.prog ~= nil
					local res, err = M.analyze(loader, root)
					-- a report over the whole corpus must not leave the whole corpus parsed behind it
					if not wasLoaded and not opts.keep then root.prog = nil end
					a = res or { error = err and (err.cause or "error") or "error", unsupported = {}, gated = {} }
					cache[root] = a
				end
				local cmds, gated = sortedKeys(a.unsupported), sortedKeys(a.gated)
				if #cmds > 0 or #gated > 0 or a.error then
					if #cmds > 0 or a.error then disabledNpcs += 1 end
					rows[#rows + 1] = { file = entry.path, npc = entry.name, line = entry.line, commands = cmds, gated = gated, error = a.error }
				end
			end
		end
	end
	return { rows = rows, npcs = totalNpcs, disabled = disabledNpcs }
end

-- text table: file <TAB> npc <TAB> commands; one line per NPC that uses at least one unsupported command (or failed to parse)
function M.format(report)
	local lines = { "file\tline\tnpc\tunsupported commands\tgated commands" }
	for _, r in report.rows do
		local cmds = table.concat(r.commands, ",")
		if r.error then cmds = "<" .. r.error .. ">" .. (cmds ~= "" and ("," .. cmds) or "") end
		lines[#lines + 1] = string.format("%s\t%d\t%s\t%s\t%s", r.file, r.line, r.npc, cmds, table.concat(r.gated, ","))
	end
	lines[#lines + 1] = string.format("# %d NPCs, %d disabled, %d rows", report.npcs, report.disabled, #report.rows)
	return table.concat(lines, "\n")
end

-- per-command totals over a report: { {name, npcs} } sorted by number of NPCs descending
function M.byCommand(report)
	local counts = {}
	for _, r in report.rows do
		for _, c in r.commands do counts[c] = (counts[c] or 0) + 1 end
	end
	local arr = {}
	for name, n in counts do arr[#arr + 1] = { name = name, npcs = n } end
	table.sort(arr, function(a, b) if a.npcs ~= b.npcs then return a.npcs > b.npcs end return a.name < b.name end)
	return arr
end

return M
