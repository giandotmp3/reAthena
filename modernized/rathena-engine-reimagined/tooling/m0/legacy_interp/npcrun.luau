--!nonstrict
-- Parse + lower + drive every script NPC of one file.
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local D = require("./driver")

local R = {}

function R.fileIndex(path)
	for i, f in C.man.files do if f.path == path then return i end end
end

-- lower all `function script` declarations of a load set into a registry
function R.loadFuncs(setname, uf)
	local funcs = {}
	local n, fail = 0, 0
	for _, fi in C.setfiles(setname) do
		local text = C.text(fi)
		if string.find(text, "function\tscript", 1, true) then
			local r = C.ds.split(text)
			for _, d in r.decls do
				if d.kind == "function" then
					local ast = parser.parse(d.body, C.builtins, uf)
					local prog = ast and lower.lower(ast, { name = d.name, builtins = C.builtins, userfuncs = uf })
					if prog then funcs[string.lower(d.name)] = prog; n += 1 else fail += 1 end
				end
			end
		end
	end
	return funcs, n, fail
end

-- opts: setname, runBudget, soft, profiles, timeBudget(s per NPC entry)
function R.runFile(path, opts)
	local fi = R.fileIndex(path)
	local text = C.text(fi)
	local uf = opts.uf
	local res = {
		path = path, lines = select(2, string.gsub(text, "\n", "")), bodies = 0, parseFail = 0, lowerFail = 0, failures = {},
		npcs = 0, entries = 0, runs = 0, ends = {}, nEnds = 0, errors = {}, maxDepth = 0, exhaustedEntries = 0, internal = {},
		opsTotal = 0, opsCovered = 0, unsupportedSeen = {}, strictUnsupported = {}, strictNpcsDisabled = 0,
		tParse = 0, tRun = 0, yields = 0, calls = 0, nested = 0, maxChoiceDepth = 0, counts = {}, gotoOps = 0, callsubOps = 0,
		hoisted = 0, nestedLeft = 0, npcsByCmd = {}, opsCoveredFlip = 0, flipMaxDepth = 0, flipEnds = {}, nFlipEnds = 0, flipErrors = {}, flipRuns = 0, labelsTotal = 0, funcDefs = 0,
	}
	local t0 = os.clock()
	local r = C.ds.split(text)
	local progs = {}
	for _, d in r.decls do
		if d.kind == "script" then
			res.bodies += 1
			local ast, info = parser.parse(d.body, C.builtins, uf)
			if not ast then
				res.parseFail += 1
				res.failures[#res.failures + 1] = (info.cause .. ":" .. tostring(info.detail) .. " @" .. (C.ds.lineof(text, d.pos) + (info.line or 1) - 1))
			else
				local h0, n0 = lower.stats.hoisted, lower.stats.nested
				local prog, lerr = lower.lower(ast, { name = d.name, file = path, builtins = C.builtins, userfuncs = uf })
				if not prog then
					res.lowerFail += 1
					res.failures[#res.failures + 1] = ("lower:" .. tostring(lerr.cause))
				else
					prog.baseLine = C.ds.lineof(text, d.pos)
					res.hoisted += lower.stats.hoisted - h0; res.nestedLeft += lower.stats.nested - n0
					progs[#progs + 1] = prog
					for _, ins in prog.code do
						if ins[1] == lower.OP.GOTO then res.gotoOps += 1 elseif ins[1] == lower.OP.CALLSUB or ins[1] == lower.OP.CALLFUNC then res.callsubOps += 1 end
					end
					for _ in prog.labels do res.labelsTotal += 1 end
				end
			end
		end
	end
	res.tParse = os.clock() - t0
	local t1 = os.clock()
	for _, prog in progs do
		res.npcs += 1
		local covmap = {}
		local covmap2 = {}
		local entries = { { "<main>", 1 } }
		for name, pc in prog.labels do
			if string.sub(name, 1, 2) == "on" then entries[#entries + 1] = { name, pc } end
		end
		table.sort(entries, function(a, b) return a[2] < b[2] end)
		local npcStrictErr = false
		local npcSeen = {}
		for _, e in entries do
			res.entries += 1
			for _, profile in opts.profiles do
				-- strict pass: UNSUPPORTED raises a typed error (path ends)
				local agg = D.explore(prog, e[2], { runBudget = opts.runBudget, profile = profile, funcs = opts.funcs, covmap = covmap, ext = opts.ext,
					npcName = prog.name, deadline = opts.timeBudget and (os.clock() + opts.timeBudget) or nil, limit = opts.limit })
				res.runs += agg.runs; res.yields += agg.yields; res.calls += agg.calls; res.nested += agg.nested
				if agg.maxDepth > res.maxDepth then res.maxDepth = agg.maxDepth end
				if agg.maxChoiceDepth > res.maxChoiceDepth then res.maxChoiceDepth = agg.maxChoiceDepth end
				if agg.exhausted then res.exhaustedEntries += 1 end
				for sig, n in agg.ends do
					local key = prog.name .. "|" .. e[1] .. "|" .. profile .. "|" .. sig
					if not res.ends[key] then res.ends[key] = n; res.nEnds += 1 end
				end
				for k, v in agg.errSample or {} do
					if not res.errSample then res.errSample = {} end
					if not res.errSample[k] then res.errSample[k] = string.format("%s:%s [%s]", path, tostring(v.line and (prog.baseLine + v.line - 1)), tostring(v.npc)) .. (v.node and (" node=" .. tostring(v.node)) or "") end
				end
				for k, v in agg.errors do
					res.errors[k] = (res.errors[k] or 0) + v
					if string.sub(k, 1, 11) == "UNSUPPORTED" then res.strictUnsupported[k] = (res.strictUnsupported[k] or 0) + v; npcStrictErr = true end
				end
				if agg.internal then res.internal[#res.internal + 1] = agg.internal end
				-- soft pass (unsupported -> recorded no-op) to inventory *all* unsupported commands behind the first one
				local agg2 = D.explore(prog, e[2], { runBudget = math.max(1, math.floor(opts.runBudget / 2)), profile = profile, funcs = opts.funcs, covmap = covmap, ext = opts.ext,
					soft = true, unsupportedSeen = npcSeen, npcName = prog.name, limit = opts.limit })
				res.runs += agg2.runs
				for k, v in agg2.errors do res.softErrors = res.softErrors or {}; res.softErrors[k] = (res.softErrors[k] or 0) + v end
				if agg2.maxDepth > res.maxDepth then res.maxDepth = agg2.maxDepth end
				for k, v in agg2.counts do res.counts[k] = (res.counts[k] or 0) + v end
				if opts.flipBudget and opts.flipBudget > 0 then
					res.covNatural = res.covNatural or 0
					local agg3 = D.explore(prog, e[2], { runBudget = opts.flipBudget, profile = profile, funcs = opts.funcs, covmap = covmap2, ext = opts.ext,
						soft = true, unsupportedSeen = {}, npcName = prog.name, limit = opts.limit, flip = true, maxFlips = opts.maxFlips or 3, maxChoices = 200 })
					res.flipRuns = (res.flipRuns or 0) + agg3.runs
					if agg3.maxDepth > res.flipMaxDepth then res.flipMaxDepth = agg3.maxDepth end
					for sig in agg3.ends do
						local key = prog.name .. "|" .. e[1] .. "|" .. profile .. "|" .. sig
						if not res.flipEnds[key] then res.flipEnds[key] = true; res.nFlipEnds += 1 end
					end
					for k, v in agg3.errors do res.flipErrors[k] = (res.flipErrors[k] or 0) + v end
					if agg3.internal then res.internal[#res.internal + 1] = agg3.internal end
				end
			end
		end
		if npcStrictErr then res.strictNpcsDisabled += 1 end
		for cmd, cnt in npcSeen do
			res.unsupportedSeen[cmd] = (res.unsupportedSeen[cmd] or 0) + cnt
			res.npcsByCmd[cmd] = (res.npcsByCmd[cmd] or 0) + 1
		end
		for _, ins in prog.code do res.opsTotal += 1 end
		for _ in covmap[prog] or {} do res.opsCovered += 1 end
		local u = {}
		for pc in covmap[prog] or {} do u[pc] = true end
		for pc in covmap2[prog] or {} do u[pc] = true end
		for _ in u do res.opsCoveredFlip += 1 end
	end
	res.tRun = os.clock() - t1
	return res
end

function R.report(res, opts)
	local out = {}
	local function p(...) out[#out + 1] = string.format(...) end
	p("FILE %s (%d lines)", res.path, res.lines)
	p("  script bodies=%d parseFail=%d lowerFail=%d  %s", res.bodies, res.parseFail, res.lowerFail, table.concat(res.failures, "; "))
	p("  NPCs driven=%d entry-points=%d (main + On* labels)  labels=%d  goto-ops=%d call(sub|func)-ops=%d  hoisted-calls=%d nested-calls-left=%d", res.npcs, res.entries, res.labelsTotal, res.gotoOps, res.callsubOps, res.hoisted, res.nestedLeft)
	p("  DFS runs=%d (entry x profile DFS trees fully exhausted: %d/%d) distinct end states=%d maxMenuDepth=%d", res.runs, res.exhaustedEntries, res.entries * #opts.profiles, res.nEnds, res.maxChoiceDepth)
	p("  max callsub/callfunc frame depth=%d  frames pushed=%d  nested(host-recursive) calls=%d  yields=%d", res.maxDepth, res.calls, res.nested, res.yields)
	p("  op coverage (union of all DFS runs, main+On* entries): %d/%d = %.1f%%", res.opsCovered, res.opsTotal, 100 * res.opsCovered / math.max(1, res.opsTotal))
	if res.flipRuns > 0 then
		p("  branch-flip DFS (every JZ/JNZ/switch is a choice point, <=3 flips/run): runs=%d distinct end states=%d op coverage natural+flip=%d/%d = %.1f%%  max frame depth=%d", res.flipRuns, res.nFlipEnds, res.opsCoveredFlip, res.opsTotal, 100 * res.opsCoveredFlip / math.max(1, res.opsTotal), res.flipMaxDepth)
	end
	p("  time: parse+lower %.3fs  drive %.3fs", res.tParse, res.tRun)
	local function dump(label, t)
		local arr = {}
		for k, v in t do arr[#arr + 1] = k .. "=" .. v end
		table.sort(arr)
		p("  %s: %s", label, #arr > 0 and table.concat(arr, " ") or "(none)")
	end
	local ek = {}
	for k, v in res.errors do ek[k] = v end
	do
		local sel = {}
		for _, k in { "mes", "next", "select", "menu", "donpcevent", "freeloop", "sleep", "sleep2", "monster", "setquest", "getitem", "warp", "close", "close2" } do
			if res.counts[k] then sel[k] = res.counts[k] end
		end
		dump("commands executed (soft passes, recorded stub calls)", sel)
	end
	dump("run-end errors (strict mode, count of DFS runs ending that way)", ek)
	for k, v in res.errSample or {} do if string.sub(k, 1, 11) ~= "UNSUPPORTED" then p("  first %s at %s", k, v) end end
	p("  NPCs disabled by >=1 UNSUPPORTED command (strict): %d/%d", res.strictNpcsDisabled, res.npcs)
	dump("unsupported commands behind the first (soft pass; call counts)", res.unsupportedSeen)
	if #res.internal > 0 then p("  INTERNAL errors: %d, first: %s %s", #res.internal, tostring(res.internal[1].msg), tostring(res.internal[1].trace)) end
	return table.concat(out, "\n")
end

return R
