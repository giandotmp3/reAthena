--!nonstrict
-- Blocking commands as yields (LNPC-YIELD-*), safety (LNPC-SAFE-*) and graceful degradation (LNPC-DEGRADE-*) through the runtime.
local H = require("./harness")
local U = require("./util")
local Report = require("../report")

H.case("LNPC-YIELD-001", "blocking commands map onto the dialogue requests: mes+next -> say, menu/select/prompt -> choose, input -> input, sleep -> wait", function()
	local rt = U.rt1(U.npc("Talk", [[
		mes "line1"; mes "line2", "line3";
		next;
		mes "pick";
		.@a = select("one:two:three");
		.@b = input(.@n);
		mes "n=" + .@n;
		input .@s$;
		prompt("yes:no");
		sleep 250;
		progressbar "0xff0000", 3;
		progressbar_npc "0x00ff00", 2;
		menu "A", L_a, "B", L_b;
	L_a: mes "a"; close;
	L_b: mes "b"; close;
	]]))
	local o, api = U.play(rt, "Talk", { 2, 42, "txt", 1, 2 }, 1)
	H.eq(o.status, "end")
	local seq = {}
	for _, r in api.log do seq[#seq + 1] = r[1] .. ":" .. tostring(r[2]) end
	H.eq(table.concat(seq, ","), "say:line1\nline2\nline3,say:pick,choose:one|two|three,input:,input:n=42,choose:yes|no,wait:250,wait:3000,wait:2000,choose:A|B,say:b")
	H.eq(rt.players[1].temp["menu"], 2)
end)

H.case("LNPC-YIELD-002", "a cancelled client ends the script at the blocking command; earlier writes are kept", function()
	local rt = U.rt1(U.npc("C", [[ $before = 1; mes "x"; .@r = select("a:b"); $after = 1; ]]))
	local o = U.play(rt, "C", { "cancel" }, 1)
	H.eq(o.status, "cancelled")
	H.eq(U.worldGet(rt, "before"), 1); H.eq(U.worldGet(rt, "after"), nil)
end)

H.case("LNPC-YIELD-003", "close delivers the buffered text and ends; close2 delivers it and the script goes on", function()
	local rt = U.rt1(U.npc("C", [[ mes "bye"; close2; mes "still here"; close; $never = 1; ]]))
	local o, api = U.play(rt, "C", {}, 1)
	H.eq(o.status, "end")
	H.eq(api.log[1][2], "bye"); H.eq(api.log[2][2], "still here"); H.eq(#api.log, 2)
	H.eq(U.worldGet(rt, "never"), nil)
end)

H.case("LNPC-SAFE-003", "the command budget (65535 / 2048 gotos by default) aborts the script with a typed error, not the host", function()
	local rt = U.rt1(U.npc("Loop", [[ L: goto L; ]]) .. U.npc("Count", [[ while (1) { .@i++; } ]]) .. U.npc("Fine", [[ $x = 1; ]]))
	local o = U.play(rt, "Loop", {}, 1)
	H.eq(o.status, "error"); H.eq(o.err.type, "BUDGET_EXCEEDED"); H.eq(o.err.what, "gotos")
	local o2 = U.play(rt, "Count", {}, 1)
	H.eq(o2.err.type, "BUDGET_EXCEEDED")
	local o3 = U.play(rt, "Fine", {}, 1)
	H.eq(o3.status, "end")
	local small = U.rt1(U.npc("Loop", [[ L: goto L; ]]), { limits = { max_commands = 100, max_gotos = 10 } })
	local o4 = U.play(small, "Loop", {}, 1)
	H.eq(o4.err.what, "gotos")
end)

H.case("LNPC-SAFE-004", "freeloop needs the unbounded_loop permission", function()
	local body = [[ freeloop(1); for (.@i = 0; .@i < 70000; .@i++) { .@s += 1; } $s = .@s; ]]
	local denied = U.rt1(U.npc("F", body))
	local o = U.play(denied, "F", {}, 1)
	H.eq(o.err.type, "PERMISSION_DENIED"); H.eq(o.err.cmd, "freeloop"); H.eq(o.err.permission, "unbounded_loop")
	local granted = U.rt1(U.npc("F", body), { granted = { "unbounded_loop" } })
	local o2 = U.play(granted, "F", {}, 1)
	H.eq(o2.status, "end")
	H.eq(U.worldGet(granted, "s"), 70000)
end)

H.case("LNPC-SAFE-005", "atcommand, query_sql and setunitdata are denied unless granted; a grant runs the host hook", function()
	local forms = { atcommand = '"@heal"', query_sql = '"select 1"', setunitdata = "1, 2, 3" }
	for _, cmd in { "atcommand", "query_sql", "setunitdata" } do
		local rt = U.rt1(U.npc("G", string.format('%s %s; $after = 1;', cmd, forms[cmd])))
		local o = U.play(rt, "G", {}, 1)
		H.eq(o.err.type, "PERMISSION_DENIED", cmd); H.eq(o.err.cmd, cmd); H.eq(U.worldGet(rt, "after"), nil)
	end
	local seen = {}
	local rt = U.rt1(U.npc("G", [[ atcommand "@heal"; query_sql "select 1"; setunitdata 1, 2, 3; $after = 1; ]]),
		{ granted = { "atcommand", "query_sql", "setunitdata" }, host = { hooks = {
			atcommand = function(st, c) seen[#seen + 1] = "at:" .. c; return 1 end,
			query_sql = function(st, c) seen[#seen + 1] = "sql:" .. c; return 0 end,
			setunitdata = function(st, c) seen[#seen + 1] = "unit:" .. c; return 0 end } } })
	local o = U.play(rt, "G", {}, 1)
	H.eq(o.status, "end")
	H.eq(table.concat(seen, ","), "at:@heal,sql:select 1,unit:1")
end)

H.case("LNPC-DEGRADE-001", "an unknown command is a typed UNSUPPORTED naming the command (interpreter level)", function()
	local rt = U.rt1(U.npc("U", [[ mes "x"; bg_warp 1, "x", 1, 1; ]]), { disable_unsupported = false })
	local o = U.play(rt, "U", {}, 1)
	H.eq(o.err.type, "UNSUPPORTED"); H.eq(o.err.cmd, "bg_warp")
end)

H.case("LNPC-DEGRADE-002", "an NPC using an unsupported command is disabled before it starts; other NPCs of the file are unaffected", function()
	local rt = U.rt1(U.npc("Good", [[ mes "ok"; close; ]]) .. U.npc("Bad", [[ $side = 1; if (0) bg_warp 1, "x", 1, 1; ]]) ..
		"-\tduplicate(Bad)\tBadClone\t-1\n" .. U.npc("Via", [[ callfunc "UsesBg"; ]]) ..
		"function\tscript\tUsesBg\t{ bg_warp 1, \"x\", 1, 1; return; }\n")
	local ok = U.play(rt, "Good", {}, 1)
	H.eq(ok.status, "end")
	local bad = U.play(rt, "Bad", {}, 1)
	H.eq(bad.status, "unsupported"); H.eq(bad.err.cmd, "bg_warp")
	H.eq(U.worldGet(rt, "side"), nil, "no side effect of an NPC that cannot run")
	H.eq(U.play(rt, "BadClone", {}, 1).status, "unsupported")
	H.eq(U.play(rt, "Via", {}, 1).status, "unsupported", "commands of called function scripts count")
	local rep = rt:report()
	local names = {}
	for _, r in rep.rows do names[r.npc] = table.concat(r.commands, ",") end
	H.eq(names.Bad, "bg_warp"); H.eq(names.BadClone, "bg_warp"); H.eq(names.Via, "bg_warp"); H.eq(names.Good, nil)
	H.eq(rep.npcs, 4); H.eq(rep.disabled, 3)
end)

H.case("LNPC-DEGRADE-003", "the report lists file, NPC and commands; gated commands are listed but do not disable", function()
	local rt = U.rt({
		{ "a.txt", U.npc("A1", [[ bg_warp 1, "x", 1, 1; bg_desert; ]]) .. U.npc("A2", [[ mes "fine"; ]]) },
		{ "b.txt", U.npc("B1", [[ atcommand "@x"; ]]) },
	})
	local rep = Report.run(rt.loader)
	H.eq(#rep.rows, 2)
	H.eq(rep.rows[1].file, "a.txt"); H.eq(rep.rows[1].npc, "A1"); H.eq(table.concat(rep.rows[1].commands, ","), "bg_desert,bg_warp")
	H.eq(rep.rows[2].file, "b.txt"); H.eq(#rep.rows[2].commands, 0); H.eq(table.concat(rep.rows[2].gated, ","), "atcommand")
	H.eq(rep.disabled, 1)
	local text = Report.format(rep)
	H.truthy(string.find(text, "a.txt\t1\tA1\tbg_desert,bg_warp", 1, true), text)
	H.truthy(string.find(text, "# 3 NPCs, 1 disabled, 2 rows", 1, true), text)
	local by = Report.byCommand(rep)
	H.eq(by[1].name, "bg_desert")
	local only = Report.run(rt.loader, { file = "b.txt" })
	H.eq(#only.rows, 1)
end)

H.case("LNPC-FLOW-004", "call depth: callsub on heap frames reaches 2048 frames, beyond that CALL_DEPTH (typed)", function()
	local rt = U.rt1(U.npc("D", [[ callsub L_R, 0; end; L_R: if (getarg(0) < 2000) callsub L_R, getarg(0) + 1; return; ]]) ..
		U.npc("E", [[ callsub L_R, 0; end; L_R: callsub L_R, getarg(0) + 1; return; ]]), { limits = { max_commands = 10000000, max_gotos = 10000000 } })
	H.eq(U.play(rt, "D", {}, 1).status, "end")
	H.eq(U.play(rt, "E", {}, 1).err.type, "CALL_DEPTH")
end)

H.case("LNPC-PARAM-002", "param variables (Zeny, BaseLevel) read and write the engine attributes, they are not stored as variables", function()
	local attrs = { ["legacy:param/zeny"] = 1000, ["legacy:param/baselevel"] = 42 }
	local params = setmetatable({}, {
		__index = function(_, name) return attrs["legacy:param/" .. name] or 0 end,
		__newindex = function(_, name, v) attrs["legacy:param/" .. name] = v end,
	})
	local rt = U.rt1(U.npc("P", [[ Zeny -= 300; .@lv = BaseLevel; $lv = .@lv; Zeny += 1; ]]))
	rt:player(1, { attributes = params })
	U.play(rt, "P", {}, 1)
	H.eq(attrs["legacy:param/zeny"], 701)
	H.eq(U.worldGet(rt, "lv"), 42)
	H.eq(U.charGet(rt, 1, "zeny"), nil, "a param is never a stored variable")
end)

return true
