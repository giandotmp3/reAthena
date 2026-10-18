--!nonstrict
-- Package entry (`legacy.npc`): what a mod, a test or the engine gets from require("init") / the bundled global LEGACY.
local Runtime = require("./runtime")

local M = {
	version = "0.1.0",
	new = Runtime.new,
	-- layers, for tests and tools
	parser = require("./parser"),
	lower = require("./lower"),
	interp = require("./interp"),
	vars = require("./vars"),
	storage_mem = require("./storage_mem"),
	loader = require("./loader"),
	events = require("./events"),
	blocking = require("./blocking"),
	report = require("./report"),
	world = require("./world"),
	commands = require("./commands_rt"),
	builtins = require("./builtins"),
	dump = require("./dump"),
	synth = require("./synth"),
}

-- sorted names of every command of the runtime's table (the C++ CommandRegistry is generated from and checked against this list)
function M.command_names()
	local names, seen = {}, {}
	local function collect(t)
		for k in t do if not seen[k] then seen[k] = true; names[#names + 1] = k end end
		local mt = getmetatable(t)
		if mt and type(mt.__index) == "table" then collect(mt.__index) end
	end
	collect(M.commands.commands)
	table.sort(names)
	return names
end

-- One status line the host (or a test) can read from the log: @run <status> <npc> <error type> <command> <line>
function M.emit_result(o)
	local e = o.err or {}
	print(string.format("@run\t%s\t%s\t%s\t%s\t%s", tostring(o.status), tostring(o.npc or ""), tostring(e.type or ""), tostring(e.cmd or ""),
		tostring(e.line or "")))
end

return M
