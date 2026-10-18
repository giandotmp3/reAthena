--!nonstrict
-- Which constructs stress the "flat list + pc + heap frames" design? Scans the lowered corpus (both sets, union of files).
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local K = parser.K
local OP = lower.OP
local uf = C.userfuncs()
local files = {}
for _, set in { "re", "pre" } do for _, fi in C.setfiles(set) do files[fi] = true end end

local spanProgs, spanNames, progs, multiLabelProgs, intraSpanProgs = 0, 0, 0, 0, 0
local spanSamples = {}
local stats = { switchOps = 0, switchCases = 0, caseSymbolic = 0, dupLabelFail = 0, lowerFail = 0 }
local nestedLabelByKind = {}
local function collectLocals(e, into)
	if type(e) ~= "table" then return end
	local k = e[1]
	if k == K.E_VAR then
		if e[2] == ".@" then into[e[3]] = true end
		collectLocals(e[4], into)
	elseif k == K.E_BIN then collectLocals(e[3], into); collectLocals(e[4], into)
	elseif k == K.E_UN then collectLocals(e[3], into)
	elseif k == K.E_TERN then collectLocals(e[2], into); collectLocals(e[3], into); collectLocals(e[4], into)
	elseif k == K.E_ASSIGN then collectLocals(e[3], into); collectLocals(e[4], into)
	elseif k == K.E_INCDEC then collectLocals(e[4], into)
	elseif k == K.E_CALL then for _, a in e[3] do collectLocals(a, into) end end
end

local function abs(ctx, line)
	if not ctx then return "?" end
	return ctx.file .. ":" .. (C.ds.lineof(ctx.text, ctx.pos) + (line or 1) - 1)
end

for fi in files do
	local f = C.man.files[fi]
	if f.mod then
		local text = C.text(fi)
		local r = C.ds.split(text)
		for _, d in r.decls do
			if d.kind == "script" or d.kind == "function" then
				local ast = parser.parse(d.body, C.builtins, uf)
				if ast then
					lower.ctx = { file = f.path, text = text, pos = d.pos }
					local prog, lerr = lower.lower(ast, { name = d.name, file = f.path, builtins = C.builtins, userfuncs = uf })
					if prog then
						progs += 1
						local code = prog.code
						-- label regions
						local pcs, nl = {}, 0
						for name, pc in prog.labels do nl += 1; pcs[#pcs + 1] = pc end
						table.sort(pcs)
						if nl >= 2 then multiLabelProgs += 1 end
						local boundary = {} -- pcs that start a fresh frame family: callsub targets / event labels / function defs
						for name, pc in prog.labels do if string.sub(name, 1, 2) == "on" then boundary[pc] = true end end
						for _, ins in code do
							if ins[1] == OP.CALLSUB and ins[2][1] == K.E_NUM then boundary[ins[2][2]] = true end
						end
						local function region(pc, onlyIntra)
							local n = 0
							for _, lp in pcs do
								if lp <= pc and not (onlyIntra and boundary[lp]) then n += 1 elseif lp > pc then break end
							end
							return n
						end
						local namesAny, namesIntra = {}, {}
						for pc, ins in code do
							local op = ins[1]
							local ex = {}
							if op == OP.EXPR or op == OP.JZ or op == OP.JNZ or op == OP.SWITCH or op == OP.GOTO or op == OP.RETURN then collectLocals(ins[2], ex)
							elseif op == OP.CALLSUB or op == OP.CALLFUNC then for _, a in ins[3] do collectLocals(a, ex) end
							elseif op == OP.MENU then for _, a in ins[2] do collectLocals(a, ex) end end
							if op == OP.SWITCH then
								stats.switchOps += 1
								for _ in ins[3] do stats.switchCases += 1 end
							end
							for n in ex do
								namesAny[n] = namesAny[n] or {}
								namesAny[n][region(pc, false)] = true
								namesIntra[n] = namesIntra[n] or {}
								namesIntra[n][region(pc, true)] = true
							end
						end
						local spanAny, spanIntra = 0, 0
						for n, regs in namesAny do
							local c = 0; for _ in regs do c += 1 end
							if c >= 2 then spanAny += 1 end
						end
						for n, regs in namesIntra do
							local c = 0; for _ in regs do c += 1 end
							if c >= 2 then
								spanIntra += 1
								if #spanSamples < 8 then spanSamples[#spanSamples + 1] = string.format("%s [%s] .@%s", f.path, d.name, n) end
							end
						end
						if spanAny > 0 then spanProgs += 1; spanNames += spanAny end
						if spanIntra > 0 then intraSpanProgs += 1 end
					else
						stats.lowerFail += 1
					end
				end
			end
		end
	end
end
print(string.format("lowered programs=%d (lower failures=%d); programs with >=2 labels=%d", progs, stats.lowerFail, multiLabelProgs))
print(string.format("programs whose `.@` locals are used in >=2 label regions: %d (%d names) - in a compile-to-Luau-locals design these need liveness analysis across labels; here they are fields of one frame table", spanProgs, spanNames))
print(string.format("  ...excluding regions that start at callsub targets / On* event labels (intra-frame labels only): %d programs", intraSpanProgs))
for _, s in spanSamples do print("    e.g. " .. s) end
print(string.format("switch statements=%d, case entries=%d", stats.switchOps, stats.switchCases))
local function list(label, arr, n)
	print(string.format("%s: %d", label, #arr))
	local byName = {}
	for _, s in arr do byName[s.name] = (byName[s.name] or 0) + 1 end
	local parts = {}
	for k, v in byName do parts[#parts + 1] = k .. "=" .. v end
	if #parts > 1 or (#parts == 1 and arr[1].depth == nil and false) then print("   by name: " .. table.concat(parts, " ")) end
	for i = 1, math.min(n, #arr) do local s = arr[i]; print("   " .. abs(s.ctx, s.line) .. (s.name and (" (" .. s.name .. (s.depth and (" depth " .. s.depth) or "") .. ")") or "")) end
end
list("labels defined INSIDE a nested block (if/loop/switch/{}) - jumping into the middle of a block", lower.nestedLabels, 12)
list("callsub/callfunc/user-function calls that cannot be hoisted (always empty since the legacy VM evaluates every operand of && || ?:; kept as a tripwire)", lower.nestedCallSites, 12)
list("goto/callsub whose target is not a statically known label (dynamic)", lower.dynamicTargets, 12)
list("inline `function name { }` definitions", lower.funcDefs, 6)
local q = {}
for k, v in parser.quirks do q[#q + 1] = k .. "=" .. v end
print("parser: rAthena statement-terminator quirks accepted: " .. table.concat(q, " "))
local le = {}
for k, v in parser.lenient do le[#le + 1] = k .. "=" .. v end
print("parser: lenient paren-less calls in expression position: " .. (#le == 0 and "0" or table.concat(le, " ")))
