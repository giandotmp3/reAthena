--!nonstrict
-- command usage histogram over the AST of both load sets (union of files)
local C = require("./corpus")
local parser = require("./parser")
local K = parser.K
local uf = C.userfuncs()
local counts, total = {}, 0
local bodyNames, bodies = nil, {}
local files = {}
for _, set in { "re", "pre" } do for _, fi in C.setfiles(set) do files[fi] = true end end
local function walkE(e)
	if type(e) ~= "table" then return end
	local k = e[1]
	if k == K.E_CALL then
		counts[e[2]] = (counts[e[2]] or 0) + 1; total += 1
		bodyNames[e[2]] = true
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
		elseif k == K.S_IF then walkE(s[2]); if s[3] then walkS({ s[3] }) end; if s[4] then walkS({ s[4] }) end
		elseif k == K.S_WHILE then walkE(s[2]); if s[3] then walkS({ s[3] }) end
		elseif k == K.S_DO then walkE(s[3]); if s[2] then walkS({ s[2] }) end
		elseif k == K.S_FOR then if s[2] then walkE(s[2]) end; if s[3] then walkE(s[3]) end; if s[4] then walkE(s[4]) end; if s[5] then walkS({ s[5] }) end
		elseif k == K.S_SWITCH then walkE(s[2]); walkS(s[3])
		elseif k == K.S_BLOCK then walkS(s[2])
		elseif k == K.S_FUNCDEF then walkS(s[3]) end
	end
end
for fi in files do
	local r = C.ds.split(C.text(fi))
	for _, d in r.decls do
		if d.kind == "script" or d.kind == "function" then
			local ast = parser.parse(d.body, C.builtins, uf)
			bodyNames = {}
			if ast then walkS(ast) end
			bodies[#bodies + 1] = bodyNames
		end
	end
end
local arr = {}
for k, v in counts do arr[#arr + 1] = { k, v } end
table.sort(arr, function(a, b) return a[2] > b[2] end)
print("distinct command names used:", #arr, "total call sites:", total)
for i = 1, math.min(#arr, 80) do io_ = nil; print(i, arr[i][1], arr[i][2]) end

-- static support coverage of the command tables
local Cmds = require("./commands")
local Ext = require("./commands_ext")
local native = { ["goto"] = true, callsub = true, callfunc = true, ["return"] = true, ["end"] = true, menu = true, ["function"] = true }
local function coverage(label, tbl)
	local unsup, calls, nUnsupNames = {}, 0, 0
	for name, n in counts do
		if not native[name] and tbl[name] == nil and not uf[name] then
			unsup[name] = n; calls += n; nUnsupNames += 1
		end
	end
	local bodiesHit = 0
	for _, names in bodies do
		for name in names do if unsup[name] then bodiesHit += 1; break end end
	end
	local implemented = 0
	for name in counts do if native[name] or tbl[name] ~= nil or uf[name] then implemented += 1 end end
	print(string.format("%s: %d/%d distinct used names handled; unsupported call sites=%d/%d (%.2f%%); script bodies containing >=1 unsupported name=%d/%d (%.1f%%)",
		label, implemented, #arr, calls, total, 100 * calls / total, bodiesHit, #bodies, 100 * bodiesHit / #bodies))
	return unsup
end
local u1 = coverage("BASE command set", Cmds.commands)
local u2 = coverage("BASE+EXT command set", Ext.commands)
local arr2 = {}
for k, v in u1 do arr2[#arr2 + 1] = { k, v } end
table.sort(arr2, function(a, b) return a[2] > b[2] end)
local parts = {}
for i = 1, math.min(25, #arr2) do parts[#parts + 1] = arr2[i][1] .. "=" .. arr2[i][2] end
print("top unsupported (base) by call sites: " .. table.concat(parts, " "))
do
	local nb = 0
	for name in counts do if C.builtins[name] then nb += 1 end end
	print(string.format("of the %d distinct called names, %d are builtins (BUILDIN_DEF) and %d are user/function-script/inline-function names", #arr, nb, #arr - nb))
end
