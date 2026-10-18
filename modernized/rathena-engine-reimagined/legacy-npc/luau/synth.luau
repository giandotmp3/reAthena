--!nonstrict
-- Source text for an instruction list built by hand (the C++ facade's Program/Instr values that did not come from parse_file):
--   { {op="command", name="mes", args={"text"}}, {op="label", name="L_A"}, {op="goto", name="L_A"}, {op="end"}, {op="return"} }
-- A command with fewer arguments than its builtin spec requires is padded with neutral ones (0, "" or a scratch variable), so that the
-- permission and support checks of the runtime, not the parser's arity check, decide what happens to it.
local B = require("./builtins")

local M = {}

local function literal(v): string
	if type(v) == "number" then return string.format("%d", v) end
	return '"' .. string.gsub(tostring(v), '["\\]', "\\%0") .. '"'
end

local function pad(name: string, n: number): { string }
	local spec = B[string.lower(name)]
	local out = {}
	if not spec then return out end
	local i = 0
	for c in string.gmatch(spec, ".") do
		if c == "?" or c == "*" then break end
		i += 1
		if i > n then
			if c == "s" then out[#out + 1] = '""'
			elseif c == "r" then out[#out + 1] = ".@pad" .. i
			else out[#out + 1] = "0" end
		end
	end
	return out
end

function M.source(list): string
	local lines = {}
	for _, ins in list do
		local op = ins.op
		if op == "command" then
			local args = {}
			for _, a in ins.args or {} do args[#args + 1] = literal(a) end
			for _, a in pad(ins.name, #args) do args[#args + 1] = a end
			lines[#lines + 1] = ins.name .. (#args > 0 and (" " .. table.concat(args, ", ")) or "") .. ";"
		elseif op == "label" then lines[#lines + 1] = ins.name .. ":"
		elseif op == "goto" then lines[#lines + 1] = "goto " .. ins.name .. ";"
		elseif op == "callsub" then lines[#lines + 1] = "callsub " .. ins.name .. ";"
		elseif op == "return" then lines[#lines + 1] = "return;"
		else lines[#lines + 1] = "end;" end
	end
	return table.concat(lines, "\n")
end

return M
