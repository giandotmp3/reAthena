--!strict
-- Luau-hosted interpreter for the flat statement list (architecture section 7).
-- Heap frames; goto/callsub/switch fall-through are pc assignments. No Luau recursion.
-- Blocking commands are coroutine.yield. STUB: not implemented.

export type Frame = { pc: number, locals: { [string]: any }, returnPc: number? }

local Interpreter = {}

function Interpreter.run(_program: any, _host: any, _limits: { maxCommands: number, maxGotos: number }): string
	error("legacy-npc: interpreter not implemented")
end

return Interpreter
