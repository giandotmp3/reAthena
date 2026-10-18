--!nonstrict
-- Per-op overhead of the flat-op interpreter on tight loops, with and without the per-op step counter
-- (the Luau CLI has no debug.setinterruptcallback, so the "counter" is the interpreter's own ops/limit check).
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local W = require("./world")
local Cmds = require("./commands")
local variants = { counted = require("./interp"), nocount = require("./interp_nocount") }

local function build(src)
	local ast = assert(parser.parse(src, C.builtins, {}))
	return assert(lower.lower(ast, { name = "bench", builtins = C.builtins, userfuncs = {} }))
end

local function runVariant(I, prog)
	local st = I.newState{ player = W.newPlayer("rich"), env = W.newEnv({}, 1), commands = Cmds.commands, limit = math.huge, glimit = math.huge }
	local co = I.spawn(st, prog, 1, {})
	local t0 = os.clock()
	local ok, kind, err = coroutine.resume(co)
	local dt = os.clock() - t0
	assert(ok and kind == "end", tostring(kind) .. " " .. tostring(err and err.type))
	return dt
end

local N = 2000000
local cases = {
	{ "for-loop, local arith   (4 ops/iter: JZ, EXPR body, EXPR i++, JMP)", ([[ for (.@i = 0; .@i < %d; .@i++) { .@s += .@i & 7; } ]]):format(N), 4 * N },
	{ "while-loop, 3 stmts     (JZ + 3 EXPR + JMP = 5 ops/iter)", ([[ .@i = 0; while (.@i < %d) { .@i++; .@a = .@i * 3 + 1; .@b = (.@a %% 5 == 0) ? .@a : .@i; } ]]):format(N), 5 * N },
	{ "global-var writes       ($x scope, 4 ops/iter)", ([[ for (.@i = 0; .@i < %d; .@i++) { $x = $x + 1; } ]]):format(N), 4 * N },
	{ "callsub/return          (per iter: JZ, CALLSUB, RETURN, EXPR i++, JMP = 5 ops)", ([[ for (.@i = 0; .@i < %d; .@i++) { callsub L_f, .@i; } end; L_f: .@t = getarg(0) + 1; return; ]]):format(N / 4), 6 * (N / 4) },
	{ "switch dispatch         (JZ, SWITCH, EXPR, JMP(break), EXPR i++, JMP = 6 ops)", ([[ for (.@i = 0; .@i < %d; .@i++) { switch (.@i & 3) { case 0: .@a++; break; case 1: .@b++; break; default: .@c++; } } ]]):format(N / 2), 6 * (N / 2) },
}
local out = {}
print("ns per executed flat op (best of 5):")
for _, c in cases do
	local prog = build(c[2])
	local best = {}
	for name, I in variants do
		local b = math.huge
		for _ = 1, 5 do
			local dt = runVariant(I, prog)
			if dt < b then b = dt end
		end
		best[name] = b
	end
	print(string.format("  %-72s counted: %6.1f ns/op   no-counter: %6.1f ns/op   counter overhead: %+.1f%%", c[1], best.counted / c[3] * 1e9, best.nocount / c[3] * 1e9, 100 * (best.counted / best.nocount - 1)))
end
-- raw reference: a plain Luau loop doing comparable work
local t0 = os.clock()
local s = 0
for i = 0, N - 1 do s += bit32.band(i, 7) end
print(string.format("  reference: native Luau for-loop with the same body: %.2f ns/iteration (s=%d)", (os.clock() - t0) / N * 1e9, s))
