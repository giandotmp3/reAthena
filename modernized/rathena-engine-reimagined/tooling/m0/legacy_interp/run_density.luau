--!nonstrict
-- control-flow density per file (AST based) over the union of both load sets
local C = require("./corpus")
local parser = require("./parser")
local K = parser.K
local uf = C.userfuncs()
local per = {}
local cur
local function walkE(e)
	if type(e) ~= "table" then return end
	local k = e[1]
	if k == K.E_CALL then
		local n = e[2]
		if n == "goto" or n == "callsub" or n == "callfunc" or n == "menu" or n == "freeloop" or n == "donpcevent" or n == "getd" or n == "setd" then cur[n] = (cur[n] or 0) + 1 end
		for _, a in e[3] do walkE(a) end
	elseif k == K.E_BIN then walkE(e[3]); walkE(e[4])
	elseif k == K.E_UN then walkE(e[3])
	elseif k == K.E_TERN then walkE(e[2]); walkE(e[3]); walkE(e[4])
	elseif k == K.E_ASSIGN then walkE(e[3]); walkE(e[4])
	elseif k == K.E_INCDEC then walkE(e[4])
	elseif k == K.E_VAR then walkE(e[4]) end
end
local function walkS(list)
	for _, s in list do
		local k = s[1]
		if k == K.S_EXPR then walkE(s[2])
		elseif k == K.S_LABEL then cur.labels = (cur.labels or 0) + 1
		elseif k == K.S_IF then walkE(s[2]); if s[3] then walkS({ s[3] }) end; if s[4] then walkS({ s[4] }) end
		elseif k == K.S_WHILE then walkE(s[2]); if s[3] then walkS({ s[3] }) end
		elseif k == K.S_DO then walkE(s[3]); if s[2] then walkS({ s[2] }) end
		elseif k == K.S_FOR then if s[2] then walkE(s[2]) end; if s[3] then walkE(s[3]) end; if s[4] then walkE(s[4]) end; if s[5] then walkS({ s[5] }) end
		elseif k == K.S_SWITCH then walkE(s[2]); walkS(s[3])
		elseif k == K.S_BLOCK then walkS(s[2])
		elseif k == K.S_FUNCDEF then walkS(s[3]) end
	end
end
local rows = {}
local tot = { goto = 0, callsub = 0, callfunc = 0, menu = 0, labels = 0, freeloop = 0, getd = 0, setd = 0, donpcevent = 0 }
for fi, f in C.man.files do
	if f.mod then
		cur = { lines = f.bytes }
		local text = C.text(fi)
		local r = C.ds.split(text)
		for _, d in r.decls do
			if d.kind == "script" or d.kind == "function" then
				local ast = parser.parse(d.body, C.builtins, uf)
				if ast then walkS(ast) end
			end
		end
		local _, nl = string.gsub(text, "\n", "")
		cur.lines = nl
		cur.path = f.path
		cur.score = (cur["goto"] or 0) + (cur.callsub or 0) + (cur.labels or 0)
		rows[#rows + 1] = cur
		for k in tot do tot[k] += cur[k] or 0 end
	end
end
table.sort(rows, function(a, b) return a.score > b.score end)
print(string.format("corpus totals (both sets, union): goto=%d callsub=%d callfunc=%d menu=%d labels=%d freeloop=%d getd=%d setd=%d donpcevent=%d", tot["goto"], tot.callsub, tot.callfunc, tot.menu, tot.labels, tot.freeloop, tot.getd, tot.setd, tot.donpcevent))
print("top 12 files by (goto + callsub + labels):")
for i = 1, 12 do
	local r = rows[i]
	print(string.format("%2d %-52s lines=%6d goto=%3d callsub=%4d callfunc=%4d labels=%4d menu=%4d freeloop=%d  score=%d", i, r.path, r.lines, r["goto"] or 0, r.callsub or 0, r.callfunc or 0, r.labels or 0, r.menu or 0, r.freeloop or 0, r.score))
end
local want = { ["npc/re/merchants/enchan_mal.txt"] = 1, ["npc/re/guild/invest_main.txt"] = 1, ["npc/re/quests/quests_16_1.txt"] = 1, ["npc/guild2/agit_main_se.txt"] = 1, ["npc/cities/prontera.txt"] = 1 }
print("requested files (rank among", #rows, "):")
for i, r in rows do
	if want[r.path] then print(string.format("rank %3d %-45s lines=%6d goto=%d callsub=%d callfunc=%d labels=%d menu=%d freeloop=%d", i, r.path, r.lines, r["goto"] or 0, r.callsub or 0, r.callfunc or 0, r.labels or 0, r.menu or 0, r.freeloop or 0)) end
end
table.sort(rows, function(a, b) return (a["goto"] or 0) > (b["goto"] or 0) end)
print("top files by goto count:")
for i = 1, 5 do local r = rows[i]; print(string.format("  %-52s goto=%d lines=%d", r.path, r["goto"] or 0, r.lines)) end
