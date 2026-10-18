--!nonstrict
-- Line protocol between the Luau package and the C++ facade (the sandbox's only output channel is print()):
--   @npc  kind name map x y dir template line      one declaration (kind: script function duplicate warp shop monster mapflag)
--   @op   opname name target line                   one flat op of the NPC announced last
--   @label name pc
--   @used command                                   every command the NPC calls (statically)
--   @body text                                      the script body of a script NPC (escaped, in chunks: print() cuts lines at 1024 bytes)
--   @diag line message
--   @end
-- Fields are tab separated; text fields escape backslash, tab, newline, carriage return as \\ \t \n \r.
local Declsplit = require("./declsplit")
local Loader = require("./loader")
local Lower = require("./lower")
local Report = require("./report")
local P = require("./parser")

local M = {}
local OP = Lower.OP
local K = P.K

local function esc(s): string
	return (string.gsub(tostring(s), "[\\\t\n\r]", function(c)
		if c == "\\" then return "\\\\" elseif c == "\t" then return "\\t" elseif c == "\n" then return "\\n" else return "\\r" end
	end))
end
M.esc = esc

local OPNAME = {
	[OP.EXPR] = "expr", [OP.JZ] = "jz", [OP.JMP] = "jmp", [OP.JNZ] = "jnz", [OP.SWITCH] = "switch", [OP.GOTO] = "goto",
	[OP.CALLSUB] = "callsub", [OP.CALLFUNC] = "callfunc", [OP.RETURN] = "return", [OP.END] = "end", [OP.MENU] = "menu",
}

-- classification of an EXPR op by its top node: a command call, an assignment, or something else
local function exprInfo(e)
	local k = e[1]
	if k == K.E_CALL then return "command", e[2] end
	if k == K.E_ASSIGN or k == K.E_INCDEC then return "assign", "" end
	if k == Lower.E_TMPSET then return exprInfo(e[3]) end
	return "nop", ""
end

local function dumpProgram(prog)
	for _, ins in prog.code do
		local op = ins[1]
		local kind, name, target = OPNAME[op], "", ""
		if op == OP.EXPR then kind, name = exprInfo(ins[2])
		elseif op == OP.JZ or op == OP.JNZ then target = ins[3]
		elseif op == OP.JMP then target = ins[2]
		elseif op == OP.MENU then name = "menu"
		end
		print(string.format("@op\t%s\t%s\t%s\t%s", kind, esc(name), tostring(target), tostring(ins[5] or 0)))
	end
	for name, pc in prog.labels do print(string.format("@label\t%s\t%d", esc(name), pc)) end
	local seen = {}
	Report.eachCommand(prog, function(name)
		if not seen[name] then seen[name] = true; print("@used\t" .. esc(name)) end
	end)
end

-- Parses `text` as one NPC file and prints every declaration (+ flat ops of script NPCs and functions).
function M.file(text: string, fileName: string)
	local split = Declsplit.split(text, { noBody = true, all = true })
	local loader = Loader.new{}
	loader:addFile(fileName, text)
	for _, e in split.errors do print(string.format("@diag\t%d\t%s", Declsplit.lineof(text, e[2]), esc(e[1]))) end
	for _, d in split.decls do
		local map, x, y, dir = string.match(d.w1 or "", "^([^,]+),(%-?%d+),(%-?%d+),?(%-?%d*)")
		if not map then map, x, y, dir = (d.w1 == "-" and "" or d.w1 or ""), 0, 0, 0 end
		print(string.format("@npc\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d", d.kind, esc(d.name or ""), esc(map), tostring(x), tostring(y), tostring(tonumber(dir) or 0),
			esc(d.template or ""), d.line or 0))
		if d.kind == "script" or d.kind == "function" then
			-- print() output is cut at 1024 bytes per line: the body travels in raw chunks of 400 bytes (<= 800 escaped)
			local body = string.sub(text, d.pos + 1, d.endpos - 1)
			for i = 1, math.max(#body, 1), 400 do print("@body\t" .. esc(string.sub(body, i, i + 399))) end
			local exname = d.name
			local entry
			if d.kind == "script" then
				local _, e = string.find(d.name, "::", 1, true)
				if e then exname = string.sub(d.name, e + 1) end
				entry = loader.npcs[exname]
			else
				entry = loader.funcs[string.lower(d.name)]
			end
			local prog, err = entry and loader:program(entry)
			if prog then dumpProgram(prog)
			else print(string.format("@diag\t%d\t%s", err and err.line or 0, esc(err and err.cause or "unknown"))) end
		end
		print("@end")
	end
end

-- `trace` sink of the runtime for the C++ facade: one @cmd line per recorded command; arguments as i:<integer> | s:<text> | r:<variable ref>
function M.trace(name, ...)
	local parts = { "@cmd", esc(name) }
	for i = 1, select("#", ...) do
		local v = select(i, ...)
		local t = type(v)
		if t == "number" then parts[#parts + 1] = string.format("i:%d", math.floor(v))
		elseif t == "table" then parts[#parts + 1] = "r:" .. esc(tostring(v[2]))
		else parts[#parts + 1] = "s:" .. esc(tostring(v)) end
	end
	print(table.concat(parts, "\t"))
end

return M
