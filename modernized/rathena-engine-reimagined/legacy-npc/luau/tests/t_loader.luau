--!nonstrict
-- Lazy loading and duplicate sharing (LNPC-LAZY-*, LNPC-VAR-008 / Q-H).
local H = require("./harness")
local U = require("./util")

local FILE = U.npc("Guide", [[
	.visits += 1;
	mes "visit " + .visits;
	close;
OnInit:
	.ready = 1;
	end;
OnTimer5000:
	end;
]], "prontera,150,150,4") .. "prontera,152,150,4\tduplicate(Guide)\tGuide2\t111\n" ..
	"prontera,154,150,4\tduplicate(Guide)\tGuide3::G3\t111\n" ..
	U.npc("Other", [[ mes "other"; close; ]], "geffen,10,10,0") ..
	"function\tscript\tF_Double\t{ return getarg(0) * 2; }\n" ..
	"prontera,1,1,0\twarp\tw1\t2,2,geffen,10,10\n"

H.case("LNPC-LAZY-001", "indexing a file parses nothing: entries, header fields and event labels only", function()
	local rt = U.rt1(FILE)
	local L = rt.loader
	H.eq(L.stats.scripts, 2)
	H.eq(L.stats.duplicates, 2)
	H.eq(L.stats.functions, 1)
	H.eq(L.stats.warps, 1)
	H.eq(L.stats.parsed, 0)
	local g = L.npcs["Guide"]
	H.eq(g.map, "prontera"); H.eq(g.x, 150); H.eq(g.y, 150); H.eq(g.dir, 4); H.eq(g.line, 1)
	H.eq(g.prog, nil)
	H.eq(table.concat(g.events, ","), "OnInit,OnTimer5000")
	H.eq(L.npcs["G3"].name, "Guide3::G3", "exname is the unique name after ::")
	H.eq(L.npcs["Guide3"], nil)
	H.eq(L.npcs["Other"].events, nil)
end)

H.case("LNPC-LAZY-002", "a program is parsed on first use, once, and the statement AST is not kept", function()
	local rt = U.rt1(FILE)
	local L = rt.loader
	U.play(rt, "Other", {}, 1)
	H.eq(L.stats.parsed, 1)
	U.play(rt, "Other", {}, 1)
	H.eq(L.stats.parsed, 1)
	local prog = L.npcs["Other"].prog
	H.truthy(prog and prog.code and prog.labels)
	local keys = 0
	for k in prog do keys += 1 end
	H.truthy(keys <= 6, "prog holds only code/labels/name/file/baseLine, got " .. keys .. " fields")
	H.eq(L.npcs["Guide"].prog, nil, "other NPCs stay unparsed")
	L:evict(L.npcs["Other"])
	H.eq(L.npcs["Other"].prog, nil)
end)

H.case("LNPC-LAZY-003", "duplicate clones share one body (same program table, one parse)", function()
	local rt = U.rt1(FILE)
	local L = rt.loader
	U.play(rt, "Guide2", {}, 1)
	H.eq(L.stats.parsed, 1)
	U.play(rt, "Guide", {}, 1)
	U.play(rt, "G3", {}, 1)
	H.eq(L.stats.parsed, 1, "template + 2 clones = 1 parse")
	H.truthy(L.npcs["Guide"].prog == L.npcs["Guide2"].prog and L.npcs["Guide"].prog == L.npcs["G3"].prog)
end)

H.case("LNPC-VAR-008", "Q-H: `.` NPC variables are shared by the template and its duplicate clones (legacy script_code::local, npc.cpp:4602)", function()
	local rt = U.rt1(FILE)
	U.play(rt, "Guide", {}, 1)
	U.play(rt, "Guide2", {}, 2)
	local _, api = U.play(rt, "G3", {}, 3)
	H.eq(rt.loader.npcs["Guide"].vars.visits, 3)
	H.truthy(rt.loader.npcs["Guide"].vars == rt.loader.npcs["Guide2"].vars)
	H.eq(rt.loader.npcs["Other"].vars == nil or rt.loader.npcs["Other"].vars.visits == nil, true, "a different script does not share")
	local o, api2 = U.play(rt, "Guide2", {}, 1)
	H.eq(api2.log[1][2], "visit 4")
end)

H.case("LNPC-LAZY-004", "getvariableofnpc reads another NPC's `.` table, clones included", function()
	local rt = U.rt1(FILE .. U.npc("Peek", [[ mes "" + getvariableofnpc(.visits, "G3"); close; ]]))
	U.play(rt, "Guide", {}, 1)
	U.play(rt, "Guide2", {}, 1)
	local _, api = U.play(rt, "Peek", {}, 1)
	H.eq(api.log[1][2], "2")
end)

H.case("LNPC-LAZY-005", "function scripts are lowered on first callfunc; undefined ones are a typed error", function()
	local rt = U.rt1(FILE .. U.npc("UseF", [[ mes "" + callfunc("F_Double", 21); close; ]]) .. U.npc("NoF", [[ callfunc "Missing"; ]]))
	local o, api = U.play(rt, "UseF", {}, 1)
	H.eq(api.log[1][2], "42")
	H.eq(rt.loader.stats.parsed, 2, "UseF + F_Double")
	local o2 = U.play(rt, "NoF", {}, 1)
	H.eq(o2.err.type, "UNDEFINED_FUNCTION")
end)

H.case("LNPC-LAZY-006", "a parse error disables that NPC only (typed load_error), others still run", function()
	local rt = U.rt1(U.npc("Bad", [[ mes "x" ]]) .. U.npc("Good", [[ mes "ok"; close; ]]))
	local o = U.play(rt, "Bad", {}, 1)
	H.eq(o.status, "load_error")
	local o2, api = U.play(rt, "Good", {}, 1)
	H.eq(o2.status, "end")
	H.eq(api.log[1][2], "ok")
end)

H.case("LNPC-LAZY-007", "source can be re-read from a provider instead of retained", function()
	local reads = 0
	local rt = U.rt({}, { readText = function(path) reads += 1; return FILE end })
	rt.loader:addFile("provided.txt")
	H.eq(rt.loader.files["provided.txt"].text, nil)
	local readsAfterIndex = reads
	U.play(rt, "Other", {}, 1)
	H.truthy(reads > readsAfterIndex, "body text comes from the provider on first use")
end)

H.case("LNPC-LAZY-009", "labels inside block comments are not events", function()
	local rt = U.rt1(U.npc("C", [[ mes "x"; end;
/*
OnDead:
	end;
*/
OnReal:
	end;
]]))
	H.eq(table.concat(rt.loader.npcs.C.events, ","), "OnReal")
end)

-- Size test on a synthetic corpus (the renewal set is measured by tests/memory_test.py): the lazy index holds a small fraction of what the
-- eager prototype pipeline keeps. collectgarbage("count") is available in sandboxes and in the CLI.
H.case("LNPC-LAZY-008", "memory: the lazy index of a corpus is a small fraction of the eagerly lowered programs", function()
	-- sandboxes expose collectgarbage("count") only; the measurement needs full collections (the Luau CLI run does it, and tests/memory_test.py
	-- does the renewal set)
	if not pcall(collectgarbage, "collect") then H.skip("collectgarbage(\"collect\") is not available in a script-host sandbox") end
	local Declsplit = require("../declsplit")
	local Parser = require("../parser")
	local Lower = require("../lower")
	local Loader = require("../loader")
	local B = require("../builtins")
	local parts = {}
	for i = 1, 300 do
		parts[#parts + 1] = U.npc("Npc" .. i, string.format([[
	mes "[Npc %d]";
	mes "Hello there, traveller, welcome to town number %d.";
	next;
	switch (select("Buy:Sell:Quests:Leave")) {
	case 1: callshop "shop%d", 1; end;
	case 2: callshop "shop%d", 2; end;
	case 3:
		if (BaseLevel < 10) { mes "Come back later."; close; }
		.@n = 0;
		for (.@i = 0; .@i < 5; .@i++) { .@n += .@i * %d; }
		mes "Total " + .@n;
		close;
	default: close;
	}
OnInit:
	.ready = 1;
	end;
]], i, i, i, i, i), "prontera,10,10,0")
	end
	local text = table.concat(parts)
	local function mem() collectgarbage("collect"); collectgarbage("collect"); return collectgarbage("count") end
	require("../consts"); Lower.lower(Parser.parse("mes \"x\";", B, {}), { name = "w", builtins = B, userfuncs = {} })
	local base = mem()
	local progs = {}
	local r = Declsplit.split(text)
	for _, d in r.decls do
		local ast = Parser.parse(d.body, B, {})
		progs[#progs + 1] = Lower.lower(ast, { name = d.name, builtins = B, userfuncs = {} })
	end
	r = nil
	local eager = mem() - base
	progs = nil
	base = mem()
	local L = Loader.new{ builtins = B }
	L:addFile("big.txt", text)
	local lazy = mem() - base
	H.eq(#L.npcList, 300)
	H.truthy(eager > 0 and lazy < 0.25 * eager, string.format("lazy %.0f KB vs eager %.0f KB", lazy, eager))
end)

return true
