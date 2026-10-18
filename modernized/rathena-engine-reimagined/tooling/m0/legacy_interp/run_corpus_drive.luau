--!nonstrict
-- Drive EVERY script NPC of a load set through the scripted client (small budgets) to find interpreter crashes
-- (INTERNAL errors), typed-error distribution and the dynamic unsupported-command inventory.
-- usage: luau run_corpus_drive.luau -a re|pre [base|ext]
local R = require("./npcrun")
local C = require("./corpus")
local setname = ({ ... })[1] or "re"
local ext = ({ ... })[2] == "ext"
local uf = C.userfuncs()
local funcs, nf = R.loadFuncs(setname, uf)
local agg = { files = 0, npcs = 0, runs = 0, flipRuns = 0, entries = 0, internal = {}, errors = {}, npcsByCmd = {}, unsupCalls = {}, parseFail = 0, lowerFail = 0,
	disabled = 0, ops = 0, cov = 0, covFlip = 0, maxDepth = 0, nested = 0, frames = 0, maxMenu = 0, ends = 0, flipEnds = 0, bodies = 0, flipErrors = {} }
local t0 = os.clock()
local slow = {}
for _, fi in C.setfiles(setname) do
	local f = C.man.files[fi]
	if f.mod then
		local ts = os.clock()
		local res = R.runFile(f.path, { uf = uf, funcs = funcs, runBudget = 6, flipBudget = 12, maxFlips = 2, profiles = { "poor", "rich" }, timeBudget = 2, ext = ext })
		local dt = os.clock() - ts
		if dt > 2 then slow[#slow + 1] = string.format("%s %.1fs", f.path, dt) end
		agg.files += 1; agg.npcs += res.npcs; agg.runs += res.runs; agg.flipRuns += res.flipRuns; agg.entries += res.entries
		agg.bodies += res.bodies; agg.parseFail += res.parseFail; agg.lowerFail += res.lowerFail
		agg.disabled += res.strictNpcsDisabled; agg.ops += res.opsTotal; agg.cov += res.opsCovered; agg.covFlip += res.opsCoveredFlip
		agg.nested += res.nested; agg.frames += res.calls; agg.ends += res.nEnds; agg.flipEnds += res.nFlipEnds
		if res.maxDepth > agg.maxDepth then agg.maxDepth = res.maxDepth; agg.maxDepthFile = f.path end
		if res.maxChoiceDepth > agg.maxMenu then agg.maxMenu = res.maxChoiceDepth end
		for k, v in res.errSample or {} do
			if string.sub(k, 1, 11) ~= "UNSUPPORTED" and k ~= "DEPTH_CAP" then
				agg.samples = agg.samples or {}
				local l = agg.samples[k] or {}
				agg.samples[k] = l
				if #l < 4 then l[#l + 1] = v end
			end
		end
		for k, v in res.errors do agg.errors[k] = (agg.errors[k] or 0) + v end
		for k, v in res.flipErrors do agg.flipErrors[k] = (agg.flipErrors[k] or 0) + v end
		for k, v in res.npcsByCmd do agg.npcsByCmd[k] = (agg.npcsByCmd[k] or 0) + v end
		for k, v in res.unsupportedSeen do agg.unsupCalls[k] = (agg.unsupCalls[k] or 0) + v end
		for _, e in res.internal do
			if #agg.internal < 10 then agg.internal[#agg.internal + 1] = f.path .. ": " .. tostring(e.type) .. " " .. tostring(e.msg) .. " " .. tostring(e.cmd) .. " " .. string.sub(tostring(e.trace), 1, 300) end
			agg.nInternal = (agg.nInternal or 0) + 1
		end
	end
end
print(string.format("SET %s, command set: %s; function registry: %d lowered", setname, ext and "base+ext" or "base", nf))
print(string.format("files=%d script bodies=%d (parse failures %d, lower failures %d) NPC entry-points driven=%d", agg.files, agg.bodies, agg.parseFail, agg.lowerFail, agg.entries))
print(string.format("scripts driven=%d  natural-DFS runs (strict+soft)=%d  branch-flip runs=%d  total wall time=%.1fs", agg.npcs, agg.runs, agg.flipRuns, os.clock() - t0))
print(string.format("op coverage natural=%.1f%% natural+flip=%.1f%% (%d ops)  max frame depth=%d (%s)  max menu-choice depth=%d  host-recursive nested calls=%d  frames pushed=%d", 100 * agg.cov / agg.ops, 100 * agg.covFlip / agg.ops, agg.ops, agg.maxDepth, tostring(agg.maxDepthFile), agg.maxMenu, agg.nested, agg.frames))
print("INTERNAL (interpreter bug) errors:", agg.nInternal or 0)
for _, l in agg.internal do print("   ", l) end
local function dump(label, t, n)
	local arr = {}
	for k, v in t do arr[#arr + 1] = { k, v } end
	table.sort(arr, function(a, b) return a[2] > b[2] end)
	local parts = {}
	for i = 1, math.min(n or 30, #arr) do parts[#parts + 1] = arr[i][1] .. "=" .. arr[i][2] end
	print(label .. " (" .. #arr .. " distinct): " .. table.concat(parts, " "))
end
dump("natural-DFS run-end typed errors (non-UNSUPPORTED)", (function() local t = {} for k, v in agg.errors do if string.sub(k, 1, 11) ~= "UNSUPPORTED" then t[k] = v end end return t end)())
dump("branch-flip run-end typed errors (non-UNSUPPORTED; forced branches make some inconsistent)", (function() local t = {} for k, v in agg.flipErrors do if string.sub(k, 1, 11) ~= "UNSUPPORTED" then t[k] = v end end return t end)())
print(string.format("scripts with >=1 UNSUPPORTED command hit in the strict pass: %d/%d", agg.disabled, agg.npcs))
dump("NPCs reaching each unsupported command (dynamic, soft pass)", agg.npcsByCmd, 30)
print("slow files (>2s): " .. table.concat(slow, "; "))

for k, l in agg.samples or {} do print("sample " .. k .. ": " .. table.concat(l, " | ")) end
