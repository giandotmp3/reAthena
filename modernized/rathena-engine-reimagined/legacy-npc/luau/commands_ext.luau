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

-- String commands: ports of script.cpp BUILDIN_FUNC(...), byte-exact incl. their quirks
local function byteAt(s, i) return string.byte(s, i + 1) or 0 end -- 0-based, NUL past the end
C.substr = function(st, s, a, b)
	rec(st, "substr", s, a, b)
	s = tostr(s)
	if a >= 0 and b < #s and a <= b then return string.sub(s, a + 1, b + 1) end
	return ""
end
C.charat = function(st, s, i)
	rec(st, "charat", s, i)
	s = tostr(s)
	if i >= 0 and i < #s then return string.sub(s, i + 1, i + 1) end
	return ""
end
C.setchar = function(st, s, c, i)
	rec(st, "setchar", s, c, i)
	s, c = tostr(s), tostr(c)
	if i >= 0 and i < #s then return string.sub(s, 1, i) .. string.sub(c, 1, 1) .. string.sub(s, i + 2) end
	return s
end
C.insertchar = function(st, s, c, i)
	rec(st, "insertchar", s, c, i)
	s, c = tostr(s), tostr(c)
	if i < 0 then i = 0 elseif i > #s then i = #s end -- size_t index: a negative value wraps to huge and is then capped to the length
	if i < 0 then i = 0 end
	return string.sub(s, 1, i) .. string.sub(c, 1, 1) .. string.sub(s, i + 1)
end
C.delchar = function(st, s, i)
	rec(st, "delchar", s, i)
	s = tostr(s)
	if i < 0 or i > #s then return s end
	return string.sub(s, 1, i) .. string.sub(s, i + 2)
end
C.strpos = function(st, hay, needle, from)
	rec(st, "strpos", hay, needle, from)
	hay, needle = tostr(hay), tostr(needle)
	local i = from or 0
	if needle == "" then return -1 end
	if i < 0 then return -1 end -- (uint32 comparison in the legacy loop)
	local p = string.find(hay, needle, i + 1, true)
	return p and p - 1 or -1
end
local function foldEq(a, b)
	return a == b or string.upper(string.char(a)) == string.upper(string.char(b))
end
C.replacestr = function(st, input, find, replace, usecase, count)
	rec(st, "replacestr", input, find, replace, usecase, count)
	input, find, replace = tostr(input), tostr(find), tostr(replace)
	local inputlen, findlen = #input, #find
	if findlen == 0 then error(END, 0) end
	if usecase ~= nil and type(usecase) ~= "number" then error(END, 0) end
	local useCase = usecase == nil or usecase ~= 0
	if count == 0 then error(END, 0) end -- explicit count 0 is rejected by the legacy command
	count = count or 0
	local out, numFinds, i = {}, 0, 0
	while i < inputlen do
		if count ~= 0 and count == numFinds then break end
		local f = 0
		while f <= findlen do
			if f == findlen then
				numFinds += 1
				out[#out + 1] = replace
				i += findlen - 1
				break
			else
				local ic, fc = byteAt(input, i + f), byteAt(find, f)
				local mismatch = (i + f) > inputlen or ic ~= fc
				if useCase then
					if mismatch then out[#out + 1] = string.sub(input, i + 1, i + 1); break end
				else
					if mismatch and not foldEq(ic, fc) then out[#out + 1] = string.sub(input, i + 1, i + 1); break end
				end
			end
			f += 1
		end
		i += 1
	end
	if i < inputlen then out[#out + 1] = string.sub(input, i + 1) end
	return table.concat(out)
end
C.countstr = function(st, input, find, usecase)
	rec(st, "countstr", input, find, usecase)
	input, find = tostr(input), tostr(find)
	local inputlen, findlen = #input, #find
	if findlen == 0 then error(END, 0) end
	if usecase ~= nil and type(usecase) ~= "number" then error(END, 0) end
	local useCase = usecase == nil or usecase ~= 0
	local n, i = 0, 0
	while i < inputlen do
		local f = 0
		while f <= findlen do
			if f == findlen then n += 1; i += findlen - 1; break end
			local ic, fc = byteAt(input, i + f), byteAt(find, f)
			local mismatch = (i + f) > inputlen or ic ~= fc
			if useCase then
				if mismatch then break end
			else
				if mismatch and not foldEq(ic, fc) then break end
			end
			f += 1
		end
		i += 1
	end
	return n
end
C.explode = function(st, ref, str, delim)
	rec(st, "explode", ref[2], str, delim)
	-- legacy: a SINGLE delimiter character (first char of the 3rd argument; "" = NUL = never splits), elements are written from the
	-- reference's own index upward, nothing is returned, existing elements beyond the last piece are left alone
	if string.byte(ref[2] or "", -1) ~= 36 then error(END, 0) end
	str, delim = tostr(str), string.sub(tostr(delim), 1, 1)
	local start = ref[3] or 0
	local i, buf = 1, {}
	for k = 1, #str do
		local ch = string.sub(str, k, k)
		if delim ~= "" and ch == delim then
			refset(st, { ref[1], ref[2], start, ref[4], ref[5] }, table.concat(buf)); start += 1; buf = {}
		else
			buf[#buf + 1] = ch
		end
	end
	refset(st, { ref[1], ref[2], start, ref[4], ref[5] }, table.concat(buf))
	return nil
end
C.implode = function(st, ref, glue)
	rec(st, "implode", ref[2], glue)
	if string.byte(ref[2] or "", -1) ~= 36 then error(END, 0) end
	local size = I.arraysize(st, ref[1], ref[2])
	if size == 0 then return "NULL" end
	local parts = {}
	for i = 0, size - 1 do parts[#parts + 1] = tostr(I.refget(st, { ref[1], ref[2], i, ref[4], ref[5] })) end
	return table.concat(parts, glue and tostr(glue) or "")
end
-- getmapxy(<map$>,<x>,<y>{,<type>{,<search>}}): types BL_PC (the attached player) and BL_NPC (this NPC) are modelled; pets, homunculi,
-- mercenaries and elementals do not exist in this world (-1). Argument-kind violations are -1 + failure, as in the legacy command.
C.getmapxy = function(st, mval, xval, yval, kind, search)
	rec(st, "getmapxy")
	local nodes = st.argNodes
	local function isVar(i) return nodes[i] and nodes[i][1] == require("./parser").K.E_VAR end
	local function isStr(i) return string.byte(nodes[i][3], -1) == 36 end
	if not (isVar(1) and isVar(2) and isVar(3)) or not isStr(1) or isStr(2) or isStr(3) then cmdFail(st); return -1 end
	local K = require("./consts").consts
	local ty = kind or K.bl_pc
	local map, x, y
	if ty == K.bl_pc then
		local p = st.player
		map, x, y = p.map or "prontera", p.x or 150, p.y or 150
	elseif ty == K.bl_npc then
		if search ~= nil then return -1 end -- another NPC by name/id: not modelled
		map, x, y = st.npc.map or "", st.npc.x or 0, st.npc.y or 0
	elseif ty == K.bl_pet or ty == K.bl_hom or ty == K.bl_mer or ty == K.bl_elem then
		return -1
	else
		cmdFail(st); return -1
	end
	local refs = {}
	for k = 1, 3 do refs[k] = I.mkref(st, nodes[k]) end
	refset(st, refs[1], map); refset(st, refs[2], x); refset(st, refs[3], y)
	return 0
end
C.eaclass = function(st) return st.player.params.class end

for _, n in { "logmes", "achievementupdate", "npcskill", "readbook", "openstorage", "unitwarp", "awake", "guardian", "setcastledata",
	"setwall", "delwall", "setunitdata", "maprespawnguildid", "delequip", "repairall", "disable_items_unused", "bonus",
	"announce_unused", "addtimer", "deltimer", "attachnpctimer", "detachnpctimer", "mapflag", "setmapflag", "removemapflag", "setcell", "pcblockmove",
	"hateffect", "setquestinfo_unused", "unitskilluseid", "unitattack", "unitkill", "unitstop", "unitstopattack", "getunitdata", "maprespawn" } do
	stub(n, 0)
end
for _, n in { "agitcheck", "agitcheck2", "achievementinfo", "checkwall", "getd_unused" } do stub(n, 0) end
stub("basicskillcheck", 1)
stub("getskilllv", 0)
stub("readparam", 0)
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
local function charTest(name, pat, truth)
	C[name] = function(st, s, pos)
		rec(st, name, s, pos)
		s = tostr(s)
		if pos < 0 or pos >= #s then return 0 end
		return string.find(string.sub(s, pos + 1, pos + 1), pat) and truth or 0
	end
end
charTest("charisalpha", "^%a$", 1)
-- charislower / charisupper return the raw libc islower()/isupper() value (glibc: the ctype class bit 512 / 256), not 0/1
charTest("charislower", "^%l$", 512); charTest("charisupper", "^%u$", 256)
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
	if select("#", ...) == 0 then return tostr(fmt) end -- no arguments: the format is returned unparsed (even %%)
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
-- getitem2/3/4, getitembound*: the end state only holds item id -> amount, attributes (identify/refine/cards/bound) are not tracked
for _, name in { "getitem2", "getitem3", "getitem4", "getitembound", "getitembound2", "getitembound3", "getitembound4" } do
	C[name] = function(st, key, amount, ...)
		rec(st, name, key, amount, ...)
		return Base.commands.getitem(st, key, amount)
	end
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

---------------------------------------------------------------------------------------------------------------------
-- Static game-data lookups through the generic data-provider hook data.lookup(cmd, ...) -> value | nil (world.luau). A host that has
-- the database answers; without it the command answers like the legacy VM does for an unknown key (the fallbacks below).
---------------------------------------------------------------------------------------------------------------------
local function lookup(st, cmd, ...)
	local d = st.env.data
	if d and d.lookup then return d.lookup(cmd, ...) end
	return nil
end
C.getiteminfo = function(st, id, ty)
	rec(st, "getiteminfo", id, ty)
	local v = lookup(st, "getiteminfo", id, ty)
	if v ~= nil then return v end
	return -1 -- unknown item (legacy: -1, or "" for the aegis-name type)
end
C.getmonsterinfo = function(st, id, ty)
	rec(st, "getmonsterinfo", id, ty)
	local v = lookup(st, "getmonsterinfo", id, ty)
	if v ~= nil then return v end
	return ty == 0 and "null" or -1
end
C.getitemslots = function(st, id) rec(st, "getitemslots", id); local v = lookup(st, "getitemslots", id); if v ~= nil then return v end return -1 end
C.mesitemlink = function(st, id, ...)
	rec(st, "mesitemlink", id, ...)
	local v = lookup(st, "mesitemlink", id, ...)
	if v ~= nil then return v end
	local d = st.env.data
	local n = d and d.itemname and d.itemname(id)
	if n == nil then cmdFail(st); return "" end
	local brackets, name = ...
	if brackets == nil then brackets = 1 end
	if name ~= nil then n = name end
	local iid = d.itemid and d.itemid(id) or id
	return "<ITEM>" .. (brackets ~= 0 and "[" or "") .. n .. (brackets ~= 0 and "]" or "") .. "<INFO>" .. tostr(iid) .. "</INFO></ITEM>"
end
C.mesitemicon = function(st, id, ...)
	local name = ...
	rec(st, "mesitemicon", id, ...)
	local v = lookup(st, "mesitemicon", id, ...)
	if v ~= nil then return v end
	local d = st.env.data
	local n = d and d.itemname and d.itemname(id)
	if n == nil then cmdFail(st); return "" end
	return name or n -- feature disabled (needs a newer PACKETVER): the plain name
end
C.checkre = function(st, t) rec(st, "checkre", t); local v = lookup(st, "checkre", t); if v ~= nil then return v end return 0 end -- server build flags (RENEWAL, RENEWAL_CAST, ...)
C.getcastlename = function(st, map) rec(st, "getcastlename", map); local v = lookup(st, "getcastlename", map); if v ~= nil then return v end return "" end
C.jobname = function(st, c) rec(st, "jobname", c); local v = lookup(st, "jobname", c); if v ~= nil then return v end return "" end
C.getmapflag = function(st, map, f) rec(st, "getmapflag", map, f); local v = lookup(st, "getmapflag", map, f); if v ~= nil then return v end return 0 end
C.checkcell = function(st, map, x, y, t) rec(st, "checkcell", map, x, y, t); local v = lookup(st, "checkcell", map, x, y, t); if v ~= nil then return v end return 0 end
C.getusers = function(st, t) rec(st, "getusers", t); local v = lookup(st, "getusers", t); if v ~= nil then return v end return 0 end
-- players on a map / in an area: the scripted player is not counted by the map's user counters; unknown map = -1
C.getmapusers = function(st, map)
	rec(st, "getmapusers", map)
	local d = st.env.data
	if d and d.mapexists and not d.mapexists(map) then return -1 end
	return 0
end
C.getareausers = function(st, map, ...)
	rec(st, "getareausers", map, ...)
	local d = st.env.data
	if d and d.mapexists and not d.mapexists(map) then return -1 end
	return 0
end

-- no guilds / parties in this world (the scripted player has none): unknown ids answer "null" / 0 like the legacy VM
C.getguildname = function(st, gid) rec(st, "getguildname", gid); return "null" end
C.getguildmaster = function(st, gid) rec(st, "getguildmaster", gid); return "null" end
C.getguildmasterid = function(st, gid) rec(st, "getguildmasterid", gid); return 0 end
C.getgdid = function(st, ...) rec(st, "getgdid", ...); return st.player.guildid or 0 end
C.getpartyname = function(st, pid) rec(st, "getpartyname", pid); return "null" end
C.getpartyleader = function(st, pid, ty) rec(st, "getpartyleader", pid, ty); if ty and ty ~= 0 then return -1 end return "null" end
C.getstatus = function(st, id, ty) rec(st, "getstatus", id, ty); return (st.player.status and st.player.status[id]) and 1 or 0 end

-- client-effect / world-effect commands without a script-visible result or player state in this world (no return value, the
-- legacy command pushes nothing): recorded, otherwise no-ops. Argument validation failures of the legacy commands are not modelled.
for _, n in { "flagemblem", "soundeffectall", "viewpointmap", "npcspeed", "specialpopup", "removespecialeffect", "areapercentheal",
	"refineui", "killmonsterall", "questinfo_refresh", "classchange", "setunittitle", "unitskillusepos", "unitwalk", "areawarp" } do
	C[n] = function(st, ...) rec(st, n, ...); return nil end
end
-- status flags of the scripted player (cart/riding/mount/option bits): none are set in this world
for _, n in { "checkcart", "checkriding", "ismounting", "checkfalcon", "checkwug", "checkmadogear", "checkdragon" } do
	C[n] = function(st, ...) rec(st, n, ...); return 0 end
end
C.isequippedcnt = function(st, ...) rec(st, "isequippedcnt", ...); return 0 end
-- makeitem <item>,<amount>,"<map>",<x>,<y>: drops an item on the floor (negative id = unidentified); fails only for an unknown item
C.makeitem = function(st, key, amount, map, x, y, ...)
	rec(st, "makeitem", key, amount, map, x, y, ...)
	local d = st.env.data
	local k = (type(key) == "number" and key < 0) and -key or key
	local id = d and d.itemid and d.itemid(k)
	if id == nil then cmdFail(st) end
	return nil
end

return M
