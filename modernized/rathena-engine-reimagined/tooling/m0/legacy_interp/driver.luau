--!nonstrict
-- Step 4: scripted-client driver. Runs a lowered NPC program as a coroutine; at each blocking yield it answers
-- deterministically. Menu/select/prompt answers are enumerated depth-first (option 1, then 2, ...) by replaying
-- the NPC from scratch with a pick prefix, with a bounded run budget.
local I = require("./interp")
local Cmds = require("./commands")
local Ext = require("./commands_ext")
local W = require("./world")

local D = {}

local function softHandler(st, name, args)
	local u = st.env.unsupportedSeen
	u[name] = (u[name] or 0) + 1
	return 0
end

-- one execution. picks: array of menu answers to replay (beyond it -> option 1)
function D.runOnce(prog, entry, opts)
	local env = W.newEnv(opts.funcs, 12345)
	env.unsupportedSeen = opts.unsupportedSeen or {}
	local player = W.newPlayer(opts.profile)
	local st = I.newState{
		player = player, env = env, npc = { vars = {}, name = opts.npcName or "?", map = "prontera" },
		commands = opts.ext and Ext.commands or Cmds.commands, unsupported = opts.soft and softHandler or nil,
		log = opts.wantLog and {} or nil, counts = {}, covmap = opts.covmap,
		limit = opts.limit, glimit = opts.glimit,
	}
	st.baseLimit, st.baseGlimit = st.limit, st.glimit
	env.npcvars[st.npc.name] = st.npc.vars
	local co = I.spawn(st, prog, entry, {})
	local picks = opts.picks or {}
	local choices = {}
	if opts.flip then
		-- branch-flip oracle: every conditional branch / switch is a choice point; option 1 = natural outcome.
		local flips, recorded = 0, 0
		st.oracle = function(n)
			if n < 2 or recorded >= (opts.maxBranchRecord or 40) then return 1 end
			local ci = #choices + 1
			local pick = picks[ci] or 1
			if pick == 1 and flips >= (opts.maxFlips or 3) then return 1 end -- natural, unrecorded once the flip budget is spent
			if pick > n then pick = n end
			choices[ci] = { pick, n }
			recorded += 1
			if pick ~= 1 then flips += 1 end
			return pick
		end
	end
	local answer
	local totalYields = 0
	local status, err
	while true do
		local ok, kind, payload = coroutine.resume(co, answer)
		if not ok then status, err = "error", { type = "HOST_ERROR", msg = tostring(kind) }; break end
		if coroutine.status(co) == "dead" then
			status = kind
			err = payload
			break
		end
		totalYields += 1
		answer = nil
		if kind == "menu" or kind == "select" or kind == "prompt" then
			local n = #payload
			local ci = #choices + 1
			local pick = picks[ci] or 1
			if pick > n then pick = n end
			choices[ci] = { pick, n }
			answer = pick
		elseif kind == "input" then
			if payload.str then answer = "test" else answer = payload.min or 1 end
		elseif kind == "sleep" or kind == "sleep2" then
			env.clock += payload
		end
		if totalYields > (opts.maxYields or 300) or #choices > (opts.maxChoices or 24) then
			status, err = "error", { type = "DEPTH_CAP" }
			break
		end
	end
	local fr = st.frame
	local endPc = fr and fr.pc
	return {
		status = status, err = err, choices = choices, endPc = endPc, st = st, yields = totalYields,
		maxDepth = st.maxDepth, log = st.log, counts = st.counts, calls = st.callCount, nested = st.nestedCalls,
	}
end

-- DFS over menu choices. Returns aggregate.
function D.explore(prog, entry, opts)
	local agg = { runs = 0, ends = {}, nEnds = 0, errors = {}, maxDepth = 0, exhausted = false, yields = 0, maxChoiceDepth = 0, sample = nil, counts = {}, calls = 0, nested = 0 }
	local picks = {}
	local budget = opts.runBudget or 300
	local tEnd = opts.deadline
	while agg.runs < budget do
		local o = {
			picks = picks, profile = opts.profile, funcs = opts.funcs, covmap = opts.covmap, soft = opts.soft,
			unsupportedSeen = opts.unsupportedSeen, ext = opts.ext, npcName = opts.npcName, wantLog = (agg.runs == 0 and opts.wantLog), flip = opts.flip, maxFlips = opts.maxFlips, maxChoices = opts.maxChoices, limit = opts.limit, glimit = opts.glimit,
		}
		local r = D.runOnce(prog, entry, o)
		agg.runs += 1
		agg.yields += r.yields
		agg.calls += r.calls; agg.nested += r.nested
		if r.maxDepth > agg.maxDepth then agg.maxDepth = r.maxDepth end
		if #r.choices > agg.maxChoiceDepth then agg.maxChoiceDepth = #r.choices end
		for k, v in r.counts do agg.counts[k] = (agg.counts[k] or 0) + v end
		local sig
		if r.status == "end" then
			sig = "end@" .. tostring(r.endPc)
		else
			local e = r.err or {}
			sig = "error:" .. tostring(e.type) .. (e.cmd and ("(" .. e.cmd .. ")") or "") .. "@" .. tostring(r.endPc)
			local key = tostring(e.type) .. (e.cmd and (":" .. e.cmd) or "")
			agg.errors[key] = (agg.errors[key] or 0) + 1
			if not agg.errSample then agg.errSample = {} end
			if not agg.errSample[key] then agg.errSample[key] = { npc = e.npc, line = e.line, pc = e.pc, msg = e.msg, node = e.node } end
			if e.type == "INTERNAL" and not agg.internal then agg.internal = e end
		end
		if not agg.ends[sig] then agg.ends[sig] = 0; agg.nEnds += 1 end
		agg.ends[sig] += 1
		if agg.runs == 1 then agg.sample = r end
		-- backtrack: increment the deepest choice that still has options
		local ch = r.choices
		local i = #ch
		while i >= 1 and ch[i][1] >= ch[i][2] do i -= 1 end
		if i < 1 then agg.exhausted = true; break end
		local np = {}
		for j = 1, i - 1 do np[j] = ch[j][1] end
		np[i] = ch[i][1] + 1
		picks = np
		if tEnd and os.clock() > tEnd then break end
	end
	return agg
end

return D
