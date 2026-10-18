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
	local isStr = string.byte(ref[2] or "", -1) == 36
	rec(st, "input", ref[2])
	local v = yield(st, "input", { str = isStr, min = min, max = max })
	if v == nil then v = isStr and "" or 0 end
	refset(st, ref, v)
	if not isStr then
		if min and v < min then refset(st, ref, min); return 1 end
		if max and v > max then refset(st, ref, max); return 2 end
	end
	return 0
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
	local env = st.env
	env.seed = (env.seed * 1103515245 + 12345) % 2147483648
	local r = env.seed / 2147483648
	if b == nil then
		if a <= 0 then return 0 end
		return math.floor(r * a)
	end
	if b < a then a, b = b, a end
	return a + math.floor(r * (b - a + 1))
end
C.max = function(st, ...) return math.max(...) end
C.min = function(st, ...) return math.min(...) end
C.abs = function(st, a) return math.abs(a) end
C.strlen = function(st, s) return #tostr(s) end
C.atoi = function(st, s) return tonumber(s) or 0 end
C.compare = function(st, a, b) return string.find(string.lower(tostr(a)), string.lower(tostr(b)), 1, true) and 1 or 0 end

-- ---- inventory / player ---------------------------------------------------------------------
C.getitem = function(st, id, amount)
	rec(st, "getitem", id, amount)
	local inv = st.player.inv
	if type(id) == "number" then inv[id] = (inv[id] or 0) + (amount or 1) end
	return 0
end
C.delitem = function(st, id, amount)
	rec(st, "delitem", id, amount)
	local inv = st.player.inv
	if type(id) == "number" then
		inv[id] = (inv[id] or 0) - (amount or 1)
		if inv[id] <= 0 then inv[id] = nil end
	end
	return 0
end
C.countitem = function(st, id)
	rec(st, "countitem", id)
	local d = st.player.defaultItemCount
	if type(id) ~= "number" then return d end
	return st.player.inv[id] or d
end
C.checkweight = function(st, ...) rec(st, "checkweight", ...); return 1 end
C.strcharinfo = function(st, n)
	rec(st, "strcharinfo", n)
	if n == 0 then return st.player.name elseif n == 1 then return st.player.party or "" elseif n == 2 then return st.player.guild or "" end
	return st.player.map or "prontera"
end
C.strnpcinfo = function(st, n)
	rec(st, "strnpcinfo", n)
	local full = st.npc.name or "?"
	if n == 0 then return full
	elseif n == 1 then return (string.match(full, "^([^#:]*)")) or full
	elseif n == 2 then return (string.match(full, "#([^:]*)")) or full
	elseif n == 3 then return st.npc.map or "prontera"
	elseif n == 4 then return st.npc.map or "prontera"
	end
	return full
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
stub("playerattached", 1)
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
stub("getmonsterinfo", 0)
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
stub("checkre", 0)
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
