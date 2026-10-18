--!nonstrict
-- Step 3b: pluggable command table (~40 most used commands by corpus usage + recording stubs).
-- Handlers are function(st, ...) -> value?; blocking ones call Interp.yield; unknown commands are NOT here:
-- the interpreter raises the typed UNSUPPORTED error.
local I = require("./interp")
local yield, END, terr, tostr = I.yield, I.END, I.terr, I.tostr
local getvar, setvar, refget, refset = I.getvar, I.setvar, I.refget, I.refset

local M = {}
local C = {}
M.commands = C

local function rec(st, name, ...)
	local counts = st.counts
	counts[name] = (counts[name] or 0) + 1
	local log = st.log
	if log then
		local n = select("#", ...)
		local parts = table.create(n)
		for i = 1, n do
			local v = select(i, ...)
			parts[i] = type((v)) == "table" and "<ref>" or tostring(v)
		end
		log[#log + 1] = name .. "(" .. table.concat(parts, ",") .. ")"
	end
end
M.rec = rec

-- recording stub: logs the call, returns a constant
local function stub(name, ret)
	C[name] = function(st, ...)
		rec(st, name, ...)
		if type(ret) == "function" then return ret(st, ...) end
		return ret
	end
end

-- ---- dialogue ------------------------------------------------------------------------------
C.mes = function(st, ...)
	rec(st, "mes", ...)
end
C.next = function(st)
	rec(st, "next")
	yield(st, "next")
end
C.close = function(st)
	rec(st, "close")
	error(END, 0) -- close = send close button + end
end
C.close3 = C.close
C.close2 = function(st)
	rec(st, "close2")
	yield(st, "close2")
end
C.clear = function(st) rec(st, "clear") end

local function menuLike(kind)
	return function(st, ...)
		local n = select("#", ...)
		local vis, ids, g = {}, {}, 0
		for i = 1, n do
			for _, piece in I.splitOptions(tostr((select(i, ...)))) do
				g += 1
				if piece ~= "" then vis[#vis + 1] = piece; ids[#ids + 1] = g end
			end
		end
		rec(st, kind, table.concat(vis, ":"))
		local k
		if #vis > 0 then k = yield(st, kind, vis) end
		if k == nil then
			if kind == "prompt" then st.player.temp["menu"] = 255; return 255 end
			error(END, 0)
		end
		local choice = ids[k]
		if not choice then terr(st, "BAD_MENU_CHOICE", { k = k }) end
		st.player.temp["menu"] = choice
		return choice
	end
end
C.select = menuLike("select")
C.prompt = menuLike("prompt")

C.input = function(st, ref, min, max)
	-- BUILDIN_FUNC(input): defaults input_min_value 0 / input_max_value INT_MAX (script.cpp script_config defaults). A numeric variable receives the
	-- value clamped to [min,max] and the command returns 1 (above max), -1 (below min) or 0; a string variable receives the text as is
	-- and the same comparison applies to its LENGTH.
	local isStr = string.byte(ref[2] or "", -1) == 36
	rec(st, "input", ref[2])
	min = min or 0
	max = max or 2147483647
	local v = yield(st, "input", { str = isStr, min = min, max = max })
	if v == nil then v = isStr and "" or 0 end
	if isStr then
		refset(st, ref, v)
		local len = #v
		return len > max and 1 or len < min and -1 or 0
	end
	refset(st, ref, math.max(min, math.min(max, v)))
	return v > max and 1 or v < min and -1 or 0
end

C.sleep = function(st, ms) rec(st, "sleep", ms); yield(st, "sleep", ms) end
C.sleep2 = function(st, ms) rec(st, "sleep2", ms); yield(st, "sleep2", ms) end
C.freeloop = function(st, on)
	rec(st, "freeloop", on)
	if on == 0 then st.limit, st.glimit = st.baseLimit or 10000, st.baseGlimit or 2048
	else st.limit, st.glimit = math.huge, math.huge end
	return on
end

-- ---- variables -----------------------------------------------------------------------------
C.set = function(st, ref, val)
	refset(st, ref, val)
	return val
end
C.setarray = function(st, ref, ...)
	local n = select("#", ...)
	local base = ref[3] or 0
	for i = 1, n do
		local v = select(i, ...)
		if ref[1] == "param" then st.player.params[ref[2]] = v else setvar(st, ref[1], ref[2], base + i - 1, v) end
	end
	return 0
end
C.getarraysize = function(st, ref)
	return I.arraysize(st, ref[1], ref[2])
end
C.deletearray = function(st, ref, n)
	local base = ref[3] or 0
	local size = I.arraysize(st, ref[1], ref[2])
	n = n or (size - base)
	local isStr = string.byte(ref[2], -1) == 36
	for i = base, size - 1 do
		local src = i + n
		local v = src < size and getvar(st, ref[1], ref[2], src) or (isStr and "" or 0)
		setvar(st, ref[1], ref[2], i, v)
	end
	return 0
end
C.cleararray = function(st, ref, val, n)
	local base = ref[3] or 0
	for i = base, base + n - 1 do setvar(st, ref[1], ref[2], i, val) end
	return 0
end
C.getelementofarray = function(st, ref, idx)
	return getvar(st, ref[1], ref[2], idx)
end
-- "getd"/"setd": variable name given as string
local parseVarName = I.parseVarName
C.getd = function(st, str)
	local scope, name, idx = parseVarName(str)
	return getvar(st, scope, name, idx)
end
C.setd = function(st, str, val)
	local scope, name, idx = parseVarName(str)
	setvar(st, scope, name, idx, val)
	return 0
end
C.getarg = function(st, i, default)
	local v = st.frame.args[i + 1]
	-- a variable passed to callsub/callfunc is a reference in the legacy VM: its value is read when getarg runs, not when the call was made
	local refs = st.frame.argrefs
	local r = refs and refs[i + 1]
	if r and v ~= nil then v = refget(st, r) end
	if v == nil then
		if default ~= nil then return default end
		terr(st, "GETARG_OUT_OF_RANGE", { index = i })
	end
	return v
end
C.getargcount = function(st) return #st.frame.args end

-- ---- numbers / strings ---------------------------------------------------------------------
C.rand = function(st, a, b)
	-- script.cpp BUILDIN_FUNC(rand): rand(n) = 0..n-1, rand(min,max) inclusive (swapped when min > max); a range that leaves no
	-- randomness (n < 1 after the exclusive max, min == max) is SCRIPT_CMD_FAILURE and ends the script
	local lo, hi
	if b ~= nil then
		lo, hi = a, b
		if lo > hi then lo, hi = hi, lo end
	else
		lo, hi = 0, a - 1
		if a < 0 or hi < 1 then I.cmdFail(st); error(END, 0) end
	end
	if lo == hi then I.cmdFail(st); error(END, 0) end
	local env = st.env
	if env.rand then return env.rand(lo, hi) end -- a host may supply the random source (the replay harness scripts it)
	env.seed = (env.seed * 1103515245 + 12345) % 2147483648
	return lo + math.floor(env.seed / 2147483648 * (hi - lo + 1))
end
C.max = function(st, ...) return math.max(...) end
C.min = function(st, ...) return math.min(...) end
C.abs = function(st, a) return math.abs(a) end
C.strlen = function(st, s) return #tostr(s) end
C.atoi = function(st, s)
	-- C atoi(): leading blanks, optional sign, then as many decimal digits as there are ("42x" -> 42); int32 wrap is not modelled
	local sign, digits = string.match(tostr(s), "^[ \t\n\r\v\f]*([+-]?)(%d+)")
	if not digits then return 0 end
	local v = tonumber(digits)
	if sign == "-" then v = -v end
	return v
end
C.compare = function(st, a, b) return string.find(string.lower(tostr(a)), string.lower(tostr(b)), 1, true) and 1 or 0 end

-- ---- inventory / player ---------------------------------------------------------------------
-- item commands: items are validated against item_db through the data provider (world.luau data.itemid); an unknown item is a
-- failure like in the legacy VM (getitem/countitem: SCRIPT_CMD_FAILURE, delitem: failure + the script ends).
local function itemId(st, key)
	local d = st.env.data
	local id = d and d.itemid and d.itemid(key)
	return id
end
C.getitem = function(st, key, amount)
	rec(st, "getitem", key, amount)
	local id = itemId(st, key)
	if id == nil then I.cmdFail(st); return nil end
	amount = (amount or 1) % 65536 -- uint16 amount in the legacy command
	if amount <= 0 then return nil end
	local inv = st.player.inv
	inv[id] = (inv[id] or 0) + amount
	return nil
end
C.delitem = function(st, key, amount)
	rec(st, "delitem", key, amount)
	local id = itemId(st, key)
	if id == nil then I.cmdFail(st); error(END, 0) end
	if amount <= 0 then return nil end
	local inv = st.player.inv
	local have = inv[id] or st.player.defaultItemCount
	if have < amount then I.cmdFail(st); error(END, 0) end -- not enough: nothing is deleted, the script ends
	local left = (inv[id] or have) - amount
	if left > 0 then inv[id] = left else inv[id] = nil end
	return nil
end
C.countitem = function(st, key)
	rec(st, "countitem", key)
	local id = itemId(st, key)
	if id == nil then I.cmdFail(st); return 0 end
	return st.player.inv[id] or st.player.defaultItemCount
end
C.checkweight = function(st, ...) rec(st, "checkweight", ...); return 1 end
C.strcharinfo = function(st, n)
	rec(st, "strcharinfo", n)
	if n == 0 then return st.player.name elseif n == 1 then return st.player.party or "" elseif n == 2 then return st.player.guild or "" end
	return st.player.map or "prontera"
end
C.strnpcinfo = function(st, n)
	rec(st, "strnpcinfo", n)
	-- nd->name is "Visible#fragment", nd->exname the unique name (the part after "::" of a declaration, else the same as name)
	local full = st.npc.name or ""
	local disp, uniq = string.match(full, "^(.-)::(.*)$")
	if not disp then disp, uniq = full, full end
	if n == 0 then return disp
	elseif n == 1 then return (string.match(disp, "^([^#]*)")) or disp
	elseif n == 2 then return (string.match(disp, "#(.*)$")) or ""
	elseif n == 3 then return uniq
	elseif n == 4 then return st.npc.map or ""
	elseif n == 5 then return st.npc.path or ""
	end
	return ""
end

-- ---- world: record only ---------------------------------------------------------------------
for _, name in { "cutin", "warp", "emotion", "specialeffect", "specialeffect2", "enablenpc", "disablenpc", "hideonnpc", "hideoffnpc",
	"cloakonnpc", "cloakoffnpc", "cloakonnpcself", "cloakoffnpcself", "npctalk", "unittalk", "mapannounce", "announce", "viewpoint",
	"stopnpctimer", "initnpctimer", "startnpctimer", "setnpctimer", "getexp", "percentheal", "setpcblock",
	"npcshopupdate", "navigateto", "disable_items", "enable_items", "waitingroom", "delwaitingroom", "mapwarp", "savepoint", "heal",
	"sc_start", "sc_end", "skill", "setlook", "changebase", "jobchange", "resetstatus", "resetskill", "message", "dispbottom",
	"setnpcdisplay", "unloadnpc", "playbgm", "soundeffect", "pcblockmove", "progressbar", "progressbar_npc", "sleep_unused" } do
	stub(name, 0)
end
-- monster/mobcount/killmonster keep a per-event counter so spawn-until-count loops terminate
C.monster = function(st, map, x, y, name, id, amount, event)
	rec(st, "monster", map, x, y, name, id, amount, event)
	local mobs = st.env.mobs
	local key = string.lower(tostring(event or ""))
	mobs[key] = (mobs[key] or 0) + (amount or 1)
	return 0
end
C.areamonster = function(st, map, x1, y1, x2, y2, name, id, amount, event)
	rec(st, "areamonster", map, name, id, amount, event)
	local mobs = st.env.mobs
	local key = string.lower(tostring(event or ""))
	mobs[key] = (mobs[key] or 0) + (amount or 1)
	return 0
end
C.mobcount = function(st, map, event)
	rec(st, "mobcount", map, event)
	local d = st.env.data
	if d and d.mapexists and not d.mapexists(map) then return -1 end -- legacy: unknown map -> -1
	return st.env.mobs[string.lower(tostring(event or ""))] or 0
end
C.killmonster = function(st, map, event)
	rec(st, "killmonster", map, event)
	st.env.mobs[string.lower(tostring(event or ""))] = nil
	return 0
end
C.getvariableofnpc = function(st, ref) return I.refget(st, ref) end
stub("donpcevent", 0)
stub("doevent", 0)
C.playerattached = function(st) rec(st, "playerattached"); return st.player.gameid or 0 end -- the attached player's block id (rid)
stub("getnpcid", 1)
-- getcharid(type): 0 char id, 1 party id, 2 guild id, 3 account id, 4 battleground id, 5 clan id (script.cpp BUILDIN_FUNC(getcharid));
-- the ids of the scripted player live in world.luau (W.newPlayer); the replay harness uses the legacy hook's synthetic player ids
C.getcharid = function(st, t, name)
	rec(st, "getcharid", t, name)
	local p = st.player
	if t == 0 then return p.charid or 0 elseif t == 1 then return p.partyid or 0 elseif t == 2 then return p.guildid or 0
	elseif t == 3 then return p.accountid or 0 end
	return 0
end
stub("is_party_leader", 0)
-- item_db name through the data provider (world.luau); unknown id -> "null" exactly like the legacy VM (BUILDIN_FUNC(getitemname))
C.getitemname = function(st, id)
	rec(st, "getitemname", id)
	local data = st.env.data
	local n = data and data.itemname and data.itemname(id)
	if n == nil then return "null" end
	return n
end
stub("instance_npcname", function(st, name) return tostring(name) end)
stub("instance_mapname", function(st, name) return tostring(name) end)
stub("instance_id", 0)
stub("getcastledata", 0)
stub("isequipped", 0)
stub("gettime", 0)
stub("getequipcardid", 0)
stub("questinfo", 0)

-- quest family: recorded + tiny state (so scripts that test checkquest/isbegin_quest after setquest behave)
C.setquest = function(st, id, ...) rec(st, "setquest", id, ...); st.player.quests[id] = 1; return 0 end
C.completequest = function(st, id) rec(st, "completequest", id); st.player.quests[id] = 2; return 0 end
C.erasequest = function(st, id) rec(st, "erasequest", id); st.player.quests[id] = nil; return 0 end
C.changequest = function(st, old, new) rec(st, "changequest", old, new); st.player.quests[old] = nil; st.player.quests[new] = 1; return 0 end
C.checkquest = function(st, id, ...) rec(st, "checkquest", id, ...); return st.player.quests[id] or -1 end
C.isbegin_quest = function(st, id) rec(st, "isbegin_quest", id); return st.player.quests[id] or 0 end

return M
