--!nonstrict
-- Memory/time benchmark of the corpus loaders over one load set. Copied next to the generated data modules by memory_test.py /
-- unsupported_report.py (the Luau CLI has no io, and `pkg` there is a copy of this unit's luau/*.luau).
--   luau corpus_bench.luau -a <mode> [<sessionNpcs>]       mode: eager | lazy | session | report | labels
-- eager   : the M0 prototype pipeline: split every file, parse + lower every body, keep every program
-- lazy    : Loader:addFile for every file (index + event-label scan only)
-- session : lazy, then `sessionNpcs` pseudo-random NPCs are run to their first use (parse + lower on demand)
-- report  : lazy + the unsupported-command report over the whole set (every body is parsed once, programs are evicted as it goes)
-- labels  : lazy index, then every script body is lowered and its real `On*` labels are compared with the text scan the index used
-- Memory is collectgarbage("count") after a full collection, as a delta over the state in which all file texts are already resident
-- (the prototype's RESULTS.md measured the same way). Prints one "@bench" line.
local mode, sessionN = ...
sessionN = tonumber(sessionN) or 200
local man = require("./manifest")
local Loader = require("./pkg/loader")
local Declsplit = require("./pkg/declsplit")
local Parser = require("./pkg/parser")
local Lower = require("./pkg/lower")
local Report = require("./pkg/report")
local builtins = require("./pkg/builtins")

local function mem()
	collectgarbage("collect"); collectgarbage("collect")
	return collectgarbage("count") / 1024
end

local texts, bytes = {}, 0
for i, f in man.files do
	texts[i] = require("./data/" .. f.mod)[1]
	bytes += #texts[i]
end
require("./pkg/consts") -- constants are loaded on first lowering in every mode: keep them out of the deltas
Lower.lower(assert(Parser.parse("mes \"x\";", builtins, {})), { name = "warm", builtins = builtins, userfuncs = {} })
local base = mem()
local t0 = os.clock()

local result = { mode = mode }
if mode == "eager" then
	local uf = {}
	local splits = {}
	for i, text in texts do
		local r = Declsplit.split(text)
		splits[i] = r
		for _, d in r.decls do if d.kind == "function" then uf[string.lower(d.name)] = true end end
	end
	local progs, nbody, bad = {}, 0, 0
	for i, r in splits do
		for _, d in r.decls do
			if d.kind == "script" or d.kind == "function" then
				nbody += 1
				local ast = Parser.parse(d.body, builtins, uf)
				local prog = ast and Lower.lower(ast, { name = d.name, file = man.files[i].path, builtins = builtins, userfuncs = uf })
				if prog then progs[#progs + 1] = prog else bad += 1 end
			end
		end
	end
	splits = nil
	result.bodies, result.failed = nbody, bad
	result.time = os.clock() - t0
	result.delta = mem() - base
	result.keep = #progs
else
	local L = Loader.new{ builtins = builtins }
	for i, text in texts do L:addFile(man.files[i].path, text) end
	result.indexTime = os.clock() - t0
	result.npcs = #L.npcList
	result.functions = L.stats.functions
	result.indexDelta = mem() - base
	if mode == "session" then
		local seed = 12345
		local ran = 0
		local t1 = os.clock()
		local n = #L.npcList
		while ran < sessionN do
			seed = (seed * 1103515245 + 12345) % 2147483648
			local e = L.npcList[1 + seed % n]
			if e.kind ~= "duplicate" or true then
				if L:program(e) then ran += 1 end
			end
		end
		result.sessionTime = os.clock() - t1
		result.sessionNpcs = ran
		result.parsed = L.stats.parsed
		result.delta = mem() - base
	elseif mode == "labels" then
		local mismatches, checked, sample = 0, 0, {}
		for _, e in L.npcList do
			if e.kind == "script" then
				local prog = L:program(e)
				if prog then
					checked += 1
					local real, scan = {}, {}
					for name in prog.labels do if string.sub(name, 1, 2) == "on" then real[name] = true end end
					for _, l in e.events or {} do scan[string.lower(l)] = true end
					local diff = false
					for k in real do if not scan[k] then diff = true end end
					for k in scan do if not real[k] then diff = true end end
					if diff then
						mismatches += 1
						if #sample < 5 then
							local rl, sl = {}, {}
							for k in real do rl[#rl + 1] = k end
							for k in scan do sl[#sl + 1] = k end
							table.sort(rl); table.sort(sl)
							sample[#sample + 1] = e.path .. ":" .. e.line .. " " .. e.name .. " real=" .. table.concat(rl, ",") .. " scan=" .. table.concat(sl, ",")
						end
					end
					L:evict(e)
				end
			end
		end
		result.checked, result.mismatches = checked, mismatches
		print("@labels-sample " .. table.concat(sample, " | "))
	elseif mode == "report" then
		local t1 = os.clock()
		local rep = Report.run(L)
		result.reportTime = os.clock() - t1
		result.reportRows, result.reportNpcs, result.reportDisabled = #rep.rows, rep.npcs, rep.disabled
		result.parseFailed = L.stats.parseFailed
		result.delta = mem() - base
		if mode == "report" then
			local fmt = Report.format(rep)
			print("@report-begin")
			print(fmt)
			print("@report-end")
			local by = Report.byCommand(rep)
			for i = 1, math.min(#by, 25) do print(string.format("@top\t%s\t%d", by[i].name, by[i].npcs)) end
		end
	end
	result.delta = result.delta or result.indexDelta
end
result.textMB = bytes / 1048576
result.baseMB = base
local parts = { "@bench" }
for k, v in result do parts[#parts + 1] = k .. "=" .. (type(v) == "number" and string.format("%.3f", v) or tostring(v)) end
table.sort(parts, function(a, b) if a == "@bench" then return true elseif b == "@bench" then return false end return a < b end)
print(table.concat(parts, " "))
