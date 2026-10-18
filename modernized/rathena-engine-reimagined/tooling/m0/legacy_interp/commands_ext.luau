--!nonstrict
-- Extension command set: strings + the extra stubs needed to drive deeper into the worst-case files.
-- Base set (commands.luau) is what was asked for; this is the "on demand" layer.
local I = require("./interp")
local Base = require("./commands")
local tostr, getvar, setvar, refset = I.tostr, I.getvar, I.setvar, I.refset
local rec = Base.rec

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
C.sscanf = function(st, ...) rec(st, "sscanf"); return 0 end
C.getmapxy = function(st, mref, xref, yref, kind)
	rec(st, "getmapxy")
	refset(st, mref, "prontera"); refset(st, xref, 150); refset(st, yref, 150)
	return 0
end
C.mesitemlink = function(st, id) return "<ITEM>" .. tostr(id) .. "</ITEM>" end
C.getguildname = function(st, id) return "Guild" end
C.getcastlename = function(st, m) return "Castle" end
C.eaclass = function(st) return st.player.params.class end

for _, n in { "logmes", "achievementupdate", "npcskill", "readbook", "openstorage", "unitwarp", "awake", "guardian", "setcastledata",
	"setwall", "delwall", "setunitdata", "maprespawnguildid", "getequipname", "delequip", "repairall", "disable_items_unused", "bonus",
	"announce_unused", "addtimer", "deltimer", "attachnpctimer", "detachnpctimer", "mapflag", "setmapflag", "removemapflag", "setcell", "pcblockmove",
	"hateffect", "setquestinfo_unused", "unitskilluseid", "unitattack", "unitkill", "unitstop", "unitstopattack", "getunitdata", "maprespawn" } do
	stub(n, 0)
end
for _, n in { "getgmlevel", "agitcheck", "agitcheck2", "achievementinfo", "checkwall", "getpartyleader", "getd_unused" } do stub(n, 0) end
stub("basicskillcheck", 1)
stub("getequipid", function(st) return st.player.profile == "rich" and 1224 or 0 end)
stub("getequipisequiped", function(st) return st.player.profile == "rich" and 1 or 0 end)
stub("getequipisenableref", 0)
stub("getequipisidentify", 0)
stub("getequiprefinerycnt", 0)
stub("getequipweaponlv", 0)
stub("getiteminfo", function(st) return st.player.profile == "rich" and 1 or 0 end)
stub("getskilllv", 0)
stub("readparam", 0)
stub("getpartyname", "Party")
stub("getmapname_unused", "prontera")
stub("getnpcdir", 0)
stub("rid2name", "Tester")
stub("isloggedin", 1)
stub("gettimetick", function(st) return st.env.clock end)
stub("gettimestr", "00:00:00")
stub("callshop", 0)
C.getstrlen = Base.commands.strlen
C.copyarray = function(st, dst, src, n)
	rec(st, "copyarray")
	for i = 0, n - 1 do I.refset(st, { dst[1], dst[2], (dst[3] or 0) + i, dst[4], dst[5] }, I.refget(st, { src[1], src[2], (src[3] or 0) + i, src[4], src[5] })) end
	return 0
end
stub("getusers", 1)
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

return M
