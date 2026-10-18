--!nonstrict
-- Variable scopes mapped onto script-host storage (LNPC-VAR-*), through the in-memory storage adapter.
local H = require("./harness")
local U = require("./util")
local Vars = require("../vars")
local Mem = require("../storage_mem")

-- scope table exercised through a runtime: one NPC that runs `body` as player `rid`
local function exec(rt, name, rid)
	local o = U.play(rt, name, {}, rid)
	H.eq(o.status, "end", "run status " .. tostring(o.err and o.err.type))
	return o
end

H.case("LNPC-VAR-001", "sigils map to their scopes (character/world persist, temp and NPC/frame/instance do not)", function()
	local rt = U.rt1(U.npc("Setter", [[
		name = 11; @tmp = 12; $w = 13; $@wt = 14; #acc = 15; ##glob = 16; .npc = 17; .@frame = 18; 'inst = 19;
		str$ = "c"; $ws$ = "w";
	]]))
	exec(rt, "Setter", 7)
	H.eq(U.charGet(rt, 7, "name"), 11)
	H.eq(U.charGet(rt, 7, "str$"), "c")
	H.eq(U.worldGet(rt, "w"), 13)
	H.eq(U.worldGet(rt, "ws$"), "w")
	H.eq(rt.backend.storage.account(2000007):get("legacy:account/acc"), 15)
	H.eq(rt.backend.storage.account(2000007):get("legacy:account_global/glob"), 16)
	-- temp scopes never reach storage
	H.eq(U.charGet(rt, 7, "tmp"), nil)
	H.eq(U.worldGet(rt, "wt"), nil)
	H.eq(rt.mem.rows.world.world["legacy:world/wt"], nil)
	-- the temp table of the player and of the world hold them
	H.eq(rt.players[7].temp.tmp, 12)
	H.eq(rt.env.gtemp.wt, 14)
	-- `.` lives on the NPC object, `.@` on the frame (gone), `'` only exists in instances (write refused outside)
	H.eq(rt.loader.npcs["Setter"].vars.npc, 17)
	H.eq(rt.loader.npcs["Setter"].vars.frame, nil)
	H.eq(rt.env.instance.inst, nil)
end)

H.case("LNPC-VAR-004", "storage key spelling equals the C++ storage_key(): legacy:<scope>/<name>", function()
	local rt = U.rt1(U.npc("K", [[ a$ = "x"; ]]))
	exec(rt, "K", 1)
	H.eq(rt.mem.rows.character["1"]["legacy:char/a$"], "x")
end)

H.case("LNPC-VAR-010", "character variables persist across runs, are private per character, and survive a logout", function()
	local rt = U.rt1(U.npc("Counter", [[ visits += 1; mes "v" + visits; ]]))
	exec(rt, "Counter", 1); exec(rt, "Counter", 1); exec(rt, "Counter", 2)
	H.eq(U.charGet(rt, 1, "visits"), 2)
	H.eq(U.charGet(rt, 2, "visits"), 1)
	rt:dropPlayer(1)
	exec(rt, "Counter", 1)
	H.eq(U.charGet(rt, 1, "visits"), 3)
end)

H.case("LNPC-VAR-011", "world variables are shared by every character; world temp is not persisted", function()
	local rt = U.rt1(U.npc("W", [[ $hits += 1; $@seen += 1; ]]))
	exec(rt, "W", 1); exec(rt, "W", 2)
	H.eq(U.worldGet(rt, "hits"), 2)
	H.eq(U.worldGet(rt, "seen"), nil)
	H.eq(rt.env.gtemp.seen, 2)
end)

H.case("LNPC-VAR-012", "account and global account variables are shared by the characters of one account", function()
	local rt = U.rt1(U.npc("A", [[ #n += 1; ##g += 10; ]]))
	local a = rt:player(1, { accountid = 500 })
	local b = rt:player(2, { accountid = 500 })
	local c = rt:player(3, { accountid = 501 })
	for _, p in { a, b, c } do exec(rt, "A", p.rid) end
	H.eq(rt.backend.storage.account(500):get("legacy:account/n"), 2)
	H.eq(rt.backend.storage.account(500):get("legacy:account_global/g"), 20)
	H.eq(rt.backend.storage.account(501):get("legacy:account/n"), 1)
end)

H.case("LNPC-VAR-005", "sparse arrays: storing 0 or \"\" deletes the element and the key when nothing is left", function()
	local rt = U.rt1(U.npc("S", [[
		arr[3] = 7; arr[5] = 9; str$[2] = "x";
		.@n = getarraysize(arr);
		arr[3] = 0;
		str$[2] = "";
		$out = .@n;
	]]))
	exec(rt, "S", 1)
	H.eq(U.worldGet(rt, "out"), 6)
	local arr = U.charGet(rt, 1, "arr")
	H.eq(arr[5], 9)
	H.eq(arr[3], nil)
	H.eq(U.charGet(rt, 1, "str$"), nil)
end)

H.case("LNPC-VAR-003", "a variable key of 33 or more characters (sigil and $ included) is refused, 32 is fine", function()
	local k32 = string.rep("a", 32)
	local k31 = string.rep("b", 31) -- + `$` suffix: 32
	local rt = U.rt1(U.npc("Long", string.format([[ %s = 1; %s = 2; %s$ = "s"; $%s = 3; ]], k32, k32 .. "c", k31, string.rep("w", 32))))
	local o = exec(rt, "Long", 1)
	H.eq(U.charGet(rt, 1, k32), 1)
	H.eq(U.charGet(rt, 1, k32 .. "c"), nil)
	H.eq(U.charGet(rt, 1, k31 .. "$"), "s")
	H.eq(U.worldGet(rt, string.rep("w", 32)), nil, "`$` + 32 characters is 33")
end)

H.case("LNPC-VAR-007", "string values: 255+ characters are refused for character/account variables and cut to 254 for world variables", function()
	local long = string.rep("z", 300)
	local rt = U.rt1(U.npc("Str", string.format([[ c$ = "ok"; c$ = "%s"; $w$ = "%s"; #a$ = "%s"; @t$ = "%s"; ]], long, long, long, long)))
	exec(rt, "Str", 1)
	H.eq(U.charGet(rt, 1, "c$"), "ok", "refused: the old value stays")
	H.eq(#U.worldGet(rt, "w$"), 254)
	H.eq(rt.backend.storage.account(2000001):get("legacy:account/a$"), nil)
	H.eq(#rt.players[1].temp["t$"], 300, "temporary scopes have no limit")
end)

H.case("LNPC-VAR-013", "# variables without the storage.account permission are a typed PERMISSION_DENIED, everything else keeps working", function()
	local rt = U.rt1(U.npc("NoAcc", [[ x = 1; #y = 2; x = 3; ]]), { storage = { account = false } })
	local o = U.play(rt, "NoAcc", {}, 1)
	H.eq(o.status, "error")
	H.eq(o.err.type, "PERMISSION_DENIED")
	H.eq(o.err.permission, "storage.account")
	H.eq(U.charGet(rt, 1, "x"), 1)
end)

H.case("LNPC-VAR-014", "writes are batched: a slice writes each dirty key once, reads hit the cache", function()
	local rt = U.rt1(U.npc("Batch", [[ for (.@i = 0; .@i < 200; .@i++) { total += .@i; arr[.@i % 5] = .@i; } ]]))
	local before = rt.mem.writes
	exec(rt, "Batch", 1)
	local writes = rt.mem.writes - before
	H.truthy(writes <= 4, "expected <= 4 storage writes, got " .. writes)
	H.eq(U.charGet(rt, 1, "total"), 19900)
end)

H.case("LNPC-VAR-015", "a write made before a blocking command is persisted even if the player disconnects while blocked", function()
	local rt = U.rt1(U.npc("Block", [[ progress = 1; mes "a"; next; progress = 2; close; ]]))
	local o = U.play(rt, "Block", { }, 1)
	H.eq(o.status, "end")
	-- a second NPC cancelled at its first `next`: the answer list raises CANCELLED from the scripted client
	local rt2 = U.rt1(U.npc("Block", [[ progress = 1; mes "a"; select("x:y"); progress = 2; ]]))
	local o2 = U.play(rt2, "Block", { "cancel" }, 1)
	H.eq(o2.status, "cancelled")
	H.eq(U.charGet(rt2, 1, "progress"), 1)
end)

H.case("LNPC-VAR-016", "Vars.writeOk: the legacy rules in one table", function()
	H.eq(select(1, Vars.writeOk("", string.rep("a", 33), 1)), false)
	H.eq(select(1, Vars.writeOk("$@", string.rep("a", 31), 1)), false)
	H.eq(select(1, Vars.writeOk("$@", string.rep("a", 30), 1)), true)
	H.eq(select(1, Vars.writeOk("", "a$", string.rep("x", 255))), false)
	H.eq(select(1, Vars.writeOk("#", "a$", string.rep("x", 254))), true)
	local ok, v = Vars.writeOk("$", "a$", string.rep("x", 400))
	H.eq(ok, true); H.eq(#v, 254)
	H.eq(select(1, Vars.writeOk("@", "a$", string.rep("x", 400))), true)
end)

H.case("LNPC-VAR-017", "storage_mem behaves like the host storage: copies in and out, nil erases, nested tables, quota", function()
	local m = Mem.new({ quota_bytes = 120 })
	local s = m.api.character(1)
	local t = { 1, 2, { a = "x" } }
	s:set("k", t)
	t[1] = 99
	H.eq(s:get("k")[1], 1, "stored value is a copy")
	local got = s:get("k"); got[2] = 77
	H.eq(s:get("k")[2], 2, "returned value is a copy")
	s:set("k", nil)
	H.eq(s:get("k"), nil)
	local ok = pcall(function() s:set("big", string.rep("x", 400)) end)
	H.eq(ok, false, "quota")
	local ok2 = pcall(function() s:set("fn", function() end) end)
	H.eq(ok2, false, "functions cannot be stored")
end)

return true
