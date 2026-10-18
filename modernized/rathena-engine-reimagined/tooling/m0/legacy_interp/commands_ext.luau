--!nonstrict
-- Extension command set: strings + the extra stubs needed to drive deeper into the worst-case files.
-- Base set (commands.luau) is what was asked for; this is the "on demand" layer.
local I = require("./interp")
local Base = require("./commands")
local tostr, getvar, setvar, refset = I.tostr, I.getvar, I.setvar, I.refset
local rec = Base.rec
local cfmt = require("./cfmt")
local cmdFail = I.cmdFail
local END = I.END

local C = setmetatable({}, { __index = Base.commands })
local M = { commands = C }

local function stub(name, ret)
	C[name] = function(st, ...)
		rec(st, name, ...)
		if type(ret) == "function" then return ret(st, ...) end
		return ret
	end
end

C.substr = function(st, s, a, b)
	s = tostr(s)
	if a < 0 or b < a or a >= #s then return "" end
	return string.sub(s, a + 1, b + 1)
end
C.charat = function(st, s, i)
	s = tostr(s)
	if i < 0 or i >= #s then return "" end
	return string.sub(s, i + 1, i + 1)
end
C.strpos = function(st, hay, needle, from)
	local p = string.find(tostr(hay), tostr(needle), (from or 0) + 1, true)
	return p and p - 1 or -1
end
C.replacestr = function(st, s, from, to, case, count)
	s, from, to = tostr(s), tostr(from), tostr(to)
	if from == "" then return s end
	local out, i, n = {}, 1, 0
	while true do
		local a, b = string.find(s, from, i, true)
		if not a or (count and count > 0 and n >= count) then break end
		out[#out + 1] = string.sub(s, i, a - 1); out[#out + 1] = to
		i = b + 1; n += 1
	end
	out[#out + 1] = string.sub(s, i)
	return table.concat(out)
end
C.countstr = function(st, s, sub)
	local n, i = 0, 1
	s, sub = tostr(s), tostr(sub)
	while sub ~= "" do
		local a, b = string.find(s, sub, i, true)
		if not a then break end
		n += 1; i = b + 1
	end
	return n
end
C.explode = function(st, ref, str, delim)
	str, delim = tostr(str), tostr(delim)
	local i, n = 1, 0
	while true do
		local a = string.find(str, delim, i, true)
		setvar(st, ref[1], ref[2], n, string.sub(str, i, (a or #str + 1) - 1))
		n += 1
		if not a then break end
		i = a + #delim
	end
	return n
end
C.implode = function(st, ref, glue)
	local size = I.arraysize(st, ref[1], ref[2])
	local parts = {}
	for i = 0, size - 1 do parts[#parts + 1] = tostr(getvar(st, ref[1], ref[2], i)) end
	return table.concat(parts, glue and tostr(glue) or "")
end
C.getmapxy = function(st, mref, xref, yref, kind)
	rec(st, "getmapxy")
	local p = st.player
	refset(st, mref, p.map or "prontera"); refset(st, xref, p.x or 150); refset(st, yref, p.y or 150)
	return 0
end
C.mesitemlink = function(st, id) return "<ITEM>" .. tostr(id) .. "</ITEM>" end
C.getguildname = function(st, id) return "Guild" end
C.getcastlename = function(st, m) return "Castle" end
C.eaclass = function(st) return st.player.params.class end

for _, n in { "logmes", "achievementupdate", "npcskill", "readbook", "openstorage", "unitwarp", "awake", "guardian", "setcastledata",
	"setwall", "delwall", "setunitdata", "maprespawnguildid", "delequip", "repairall", "disable_items_unused", "bonus",
	"announce_unused", "addtimer", "deltimer", "attachnpctimer", "detachnpctimer", "mapflag", "setmapflag", "removemapflag", "setcell", "pcblockmove",
	"hateffect", "setquestinfo_unused", "unitskilluseid", "unitattack", "unitkill", "unitstop", "unitstopattack", "getunitdata", "maprespawn" } do
	stub(n, 0)
end
for _, n in { "agitcheck", "agitcheck2", "achievementinfo", "checkwall", "getpartyleader", "getd_unused" } do stub(n, 0) end
stub("basicskillcheck", 1)
stub("getskilllv", 0)
stub("readparam", 0)
stub("getpartyname", "Party")
stub("getmapname_unused", "prontera")
stub("getnpcdir", 0)
stub("rid2name", "Tester")
stub("isloggedin", 1)
stub("gettimetick", function(st) return st.env.clock end)
stub("gettimestr", "00:00:00")
C.getstrlen = Base.commands.strlen
C.copyarray = function(st, dst, src, n)
	rec(st, "copyarray")
	for i = 0, n - 1 do I.refset(st, { dst[1], dst[2], (dst[3] or 0) + i, dst[4], dst[5] }, I.refget(st, { src[1], src[2], (src[3] or 0) + i, src[4], src[5] })) end
	return 0
end
stub("getmapusers", 1)
stub("getareausers", 1)
stub("getgdid", 0)
stub("getguildmaster", "Master")
stub("getguildmasterid", 1)
stub("getguildmember", 0)
stub("guildopenstorage", 0)
stub("setd_unused", 0)
stub("movenpc", 0)
stub("setnpcdir_unused", 0)
stub("itemskill", 0)
stub("getpartymember", 0)
stub("pow", function(st, a, b) return math.floor(a ^ b) end)
stub("sqrt", function(st, a) return math.floor(math.sqrt(a)) end)

---------------------------------------------------------------------------------------------------------------------
-- Pure string / number commands (exact legacy semantics, script.cpp BUILDIN_FUNC(...))
---------------------------------------------------------------------------------------------------------------------
local function cstrcmp(a, b)
	-- glibc strcmp returns the difference of the first differing bytes (as unsigned chars)
	local n = math.min(#a, #b)
	for i = 1, n do
		local x, y = string.byte(a, i), string.byte(b, i)
		if x ~= y then return x - y end
	end
	if #a == #b then return 0 end
	if #a > #b then return string.byte(a, n + 1) end
	return -string.byte(b, n + 1)
end
C.strcmp = function(st, a, b) rec(st, "strcmp", a, b); return cstrcmp(tostr(a), tostr(b)) end
C.strtolower = function(st, s) rec(st, "strtolower", s); return string.lower(tostr(s)) end
C.strtoupper = function(st, s) rec(st, "strtoupper", s); return string.upper(tostr(s)) end
local function charTest(name, pat)
	C[name] = function(st, s, pos)
		rec(st, name, s, pos)
		s = tostr(s)
		if pos < 0 or pos >= #s then return 0 end
		return string.find(string.sub(s, pos + 1, pos + 1), pat) and 1 or 0
	end
end
charTest("charisalpha", "^%a$"); charTest("charislower", "^%l$"); charTest("charisupper", "^%u$")
local function cstrtol(s, base)
	-- C strtol: leading whitespace, optional sign, base 0/8/10/16 prefixes; capped to int32 like the legacy command
	s = tostr(s)
	local rest = string.match(s, "^[ \t\n\r\v\f]*(.*)$")
	local sign = 1
	local c = string.sub(rest, 1, 1)
	if c == "-" then sign = -1; rest = string.sub(rest, 2) elseif c == "+" then rest = string.sub(rest, 2) end
	if (base == 0 or base == 16) and string.match(rest, "^0[xX]%x") then rest = string.sub(rest, 3); base = 16
	elseif base == 0 and string.sub(rest, 1, 1) == "0" then base = 8
	elseif base == 0 then base = 10 end
	local digits = ""
	for i = 1, #rest do
		local d = tonumber(string.sub(rest, i, i), 36)
		if d == nil or d >= base then break end
		digits ..= string.sub(rest, i, i)
	end
	if digits == "" then return 0 end
	local v = sign * tonumber(digits, base)
	if v > 2147483647 then v = 2147483647 elseif v < -2147483648 then v = -2147483648 end
	return v
end
C.strtol = function(st, s, base) rec(st, "strtol", s, base); return cstrtol(s, base) end
C.axtoi = function(st, s) rec(st, "axtoi", s); return cstrtol(s, 16) end
C.distance = function(st, x0, y0, x1, y1) rec(st, "distance", x0, y0, x1, y1); return math.max(math.abs(x0 - x1), math.abs(y0 - y1)) end
C.isday = function(st) rec(st, "isday"); return st.env.night and 0 or 1 end
C.isnight = function(st) rec(st, "isnight"); return st.env.night and 1 or 0 end

C.sprintf = function(st, fmt, ...)
	rec(st, "sprintf", fmt, ...)
	local s, why = cfmt.sprintf(tostr(fmt), { ... })
	if s == nil then cmdFail(st); return "" end
	return s
end

-- sscanf: targets are variables (not 'r' in the arg spec), so the evaluated call node is available as st.argNodes
C.sscanf = function(st, str, fmt, ...)
	local nargs = select("#", ...)
	str, fmt = tostr(str), tostr(fmt)
	-- number of assigning conversions in the format (not %% and not %*)
	local nconv = 0
	do
		local i = 1
		while true do
			local p = string.find(fmt, "%", i, true)
			if not p then break end
			local c = string.sub(fmt, p + 1, p + 1)
			if c == "%" or c == "*" then i = p + 2 else nconv += 1; i = p + 1 end
		end
	end
	local nodes = st.argNodes
	local refs, kinds = {}, {}
	for k = 1, math.min(nconv, nargs) do
		local node = nodes[k + 2]
		local ok, r = pcall(I.mkref, st, node)
		if not ok then rec(st, "sscanf", str, fmt); cmdFail(st); return -1 end
		refs[k] = r
		kinds[k] = string.byte(r[2] or "", -1) == 36 and "s" or "n"
	end
	rec(st, "sscanf", str, fmt)
	if #fmt ~= 0 and #str == 0 then return -1 end
	if nconv > nargs then cmdFail(st); return -1 end
	local count, vals, ok = cfmt.sscanf(str, fmt, kinds)
	for k = 1, #vals do refset(st, refs[k], vals[k]) end
	if not ok then cmdFail(st); return -1 end
	return nconv
end

---------------------------------------------------------------------------------------------------------------------
-- Arrays (inarray / countinarray)
---------------------------------------------------------------------------------------------------------------------
C.inarray = function(st, ref, value)
	rec(st, "inarray", ref[2], value)
	local size = I.arraysize(st, ref[1], ref[2])
	if size == 0 then return -1 end
	for i = 0, size - 1 do
		if I.refget(st, { ref[1], ref[2], i, ref[4], ref[5] }) == value then return i end
	end
	return -1
end
C.countinarray = function(st, r1, r2)
	rec(st, "countinarray", r1[2], r2[2])
	local s1, s2 = I.arraysize(st, r1[1], r1[2]), I.arraysize(st, r2[1], r2[2])
	if s1 == 0 or s2 == 0 then return 0 end
	local i0, j0 = r1[3] or 0, r2[3] or 0
	if i0 > s1 - 1 or j0 > s2 - 1 then cmdFail(st); error(END, 0) end
	local n = 0
	for i = i0, s1 - 1 do
		local a = I.refget(st, { r1[1], r1[2], i, r1[4], r1[5] })
		for j = j0, s2 - 1 do
			if a == I.refget(st, { r2[1], r2[2], j, r2[4], r2[5] }) then n += 1 end
		end
	end
	return n
end

---------------------------------------------------------------------------------------------------------------------
-- Player model: equipment (empty unless a host fills player.equip[pos] = {id=, refine=, weaponlv=, identify=, enableref=, name=})
-- EQI positions: script.cpp equip_index_check() accepts 0 .. EQI_MAX-1 (EQI_MAX = 15 in this tree; the compound-on default is -1)
---------------------------------------------------------------------------------------------------------------------
local EQI_MAX = 15
local function equipAt(st, pos)
	if pos == nil or pos < 0 or pos >= EQI_MAX then return nil end
	local e = st.player.equip
	return e and e[pos]
end
C.getequipid = function(st, pos)
	rec(st, "getequipid", pos)
	if pos ~= nil and (pos < 0 or pos >= EQI_MAX) then cmdFail(st); return -1 end
	local it = equipAt(st, pos)
	return it and it.id or -1
end
C.getequipname = function(st, pos) rec(st, "getequipname", pos); local it = equipAt(st, pos); return it and it.name or "" end
C.getequipisequiped = function(st, pos) rec(st, "getequipisequiped", pos); return equipAt(st, pos) and 1 or 0 end
C.getequipisenableref = function(st, pos) rec(st, "getequipisenableref", pos); local it = equipAt(st, pos); return it and it.enableref and 1 or 0 end
C.getequipisidentify = function(st, pos) rec(st, "getequipisidentify", pos); local it = equipAt(st, pos); return it and (it.identify and 1 or 0) or 0 end
C.getequiprefinerycnt = function(st, pos) rec(st, "getequiprefinerycnt", pos); local it = equipAt(st, pos); return it and it.refine or 0 end
C.getequipweaponlv = function(st, pos) rec(st, "getequipweaponlv", pos); local it = equipAt(st, pos); return it and it.weaponlv or 0 end
C.getequipcardid = function(st, pos, slot) rec(st, "getequipcardid", pos, slot); local it = equipAt(st, pos); return it and it.cards and it.cards[slot] or 0 end
C.nude = function(st) rec(st, "nude"); st.player.equip = {}; return 0 end

-- look values of the player (all 0 for a fresh character); LOOK_SHOES (9) and unknown types are -1
C.getlook = function(st, t)
	rec(st, "getlook", t)
	local known = { [1] = true, [2] = true, [3] = true, [4] = true, [5] = true, [6] = true, [7] = true, [8] = true, [12] = true, [13] = true }
	if not known[t] then return -1 end
	return st.player.look and st.player.look[t] or 0
end
C.getpartnerid = function(st) rec(st, "getpartnerid"); return st.player.partnerid or 0 end -- sd->status.partner_id: unmarried = 0
C.getgdskilllv = function(st, gid, skill) rec(st, "getgdskilllv", gid, skill); return -1 end -- unknown guild id -> -1 (no guild model)
C.is_guild_leader = function(st, gid) rec(st, "is_guild_leader", gid); return 0 end -- no guild
C.isdead = function(st) rec(st, "isdead"); return 0 end

---------------------------------------------------------------------------------------------------------------------
-- Instances: the world model has no running instance, so (as in the legacy VM for a player/NPC outside an instance)
-- instance_id() is 0, instance variables read as 0/"" and cannot be written (interp.luau), instance_npcname/instance_mapname
-- fail and end the script.
---------------------------------------------------------------------------------------------------------------------
C.instance_id = function(st, ...) rec(st, "instance_id", ...); return st.env.instanceId or 0 end
local function noInstance(name)
	C[name] = function(st, ...)
		rec(st, name, ...)
		if (st.env.instanceId or 0) <= 0 then cmdFail(st); error(END, 0) end
		return 0
	end
end
noInstance("instance_npcname"); noInstance("instance_mapname")

---------------------------------------------------------------------------------------------------------------------
-- Data-provider backed lookups (world.luau W.defaultData): shops, castles, maps, battle config
---------------------------------------------------------------------------------------------------------------------
C.callshop = function(st, name, flag)
	rec(st, "callshop", name, flag)
	local d = st.env.data
	if d and d.shop and d.shop(name) then return 1 end
	cmdFail(st)
	return 0
end
C.getcastledata = function(st, map, ty)
	rec(st, "getcastledata", map, ty)
	local d = st.env.data
	if d and d.castle and d.castle(map) then return 0 end -- castle exists, no guild owns it in this world
	cmdFail(st)
	return 0
end
C.getbattleflag = function(st, name)
	rec(st, "getbattleflag", name)
	local d = st.env.data
	local v = d and d.battleflag and d.battleflag(string.lower(tostr(name)))
	return v or 0
end
C.getgmlevel = function(st, ...) rec(st, "getgmlevel", ...); return st.player.gmlevel or 0 end
C.mercenary_get_faith = function(st, g) rec(st, "mercenary_get_faith", g); return (st.player.mercfaith and st.player.mercfaith[g]) or 0 end
C.mercenary_set_faith = function(st, g, v)
	rec(st, "mercenary_set_faith", g, v)
	local p = st.player
	p.mercfaith = p.mercfaith or {}
	p.mercfaith[g] = v
	return 0
end
-- warp: the player moves when the map exists (pc_setpos); an unknown map is SCRIPT_CMD_FAILURE and the player stays
C.warp = function(st, map, x, y)
	rec(st, "warp", map, x, y)
	local d = st.env.data
	local p = st.player
	if map == "Random" or map == "SavePoint" or map == "Save" then return 0 end
	if d and d.mapexists and not d.mapexists(map) then cmdFail(st); return nil end
	p.map, p.x, p.y = map, x, y
	return 0
end
C.getusers = function(st, ...) rec(st, "getusers", ...); return 1 end -- exactly the one scripted player

return M
