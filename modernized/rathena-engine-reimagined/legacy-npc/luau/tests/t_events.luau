--!nonstrict
-- Event labels, the 2-slot player queue, timers and the NPC/event commands (LNPC-EVT-*, LNPC-TIMER-*), against the runtime's own
-- scheduler (Rt:advance) and a fake engine event bus.
local H = require("./harness")
local U = require("./util")
local Events = require("../events")

local function fakeBus()
	local bus = { hooks = {} }
	bus.api = { on = function(event, spec, fn) bus.hooks[#bus.hooks + 1] = { event = event, spec = spec, fn = fn } end }
	-- the engine side of a dispatch: every hook of the event runs; ctx:get() = attached player id
	function bus.dispatch(event, rid)
		local n = 0
		for _, h in bus.hooks do
			if h.event == event then h.fn({ get = function() return rid or 0 end }); n += 1 end
		end
		return n
	end
	return bus
end

H.case("LNPC-EVT-001", "label classification covers the legacy table (script.cpp script_config)", function()
	local function kind(l) local c = Events.classify(l); return c and c.kind end
	H.eq(kind("OnInit"), "init")
	H.eq(kind("OnPCLoginEvent"), "player"); H.eq(kind("OnPCLogoutEvent"), "player"); H.eq(kind("OnPCDieEvent"), "player")
	H.eq(kind("OnPCKillEvent"), "player"); H.eq(kind("OnNPCKillEvent"), "player"); H.eq(kind("OnPCLoadMapEvent"), "player")
	H.eq(kind("OnPCBaseLvUpEvent"), "player"); H.eq(kind("OnPCJobLvUpEvent"), "player"); H.eq(kind("OnPCIdentifyEvent"), "player")
	H.eq(kind("OnTouch"), "touch"); H.eq(kind("OnTouch_"), "touch"); H.eq(kind("OnTouchNPC"), "touchnpc")
	H.eq(kind("OnWhisperGlobal"), "whisper"); H.eq(kind("OnCommand"), "command"); H.eq(kind("OnBuyItem"), "shop")
	H.eq(kind("OnAgitStart"), "agit"); H.eq(kind("OnAgitEnd3"), "agit"); H.eq(kind("OnInterIfInit"), "interif")
	H.eq(kind("OnInstanceInit"), "instance"); H.eq(kind("OnTimerQuit"), "timerquit")
	H.eq(kind("OnTimer5000"), "timer"); H.eq(Events.classify("OnTimer5000").arg, 5000)
	H.eq(kind("OnClock0830"), "clock"); H.eq(Events.classify("OnClock0830").arg, 830)
	H.eq(kind("OnMinute30"), "minute"); H.eq(kind("OnHour07"), "hour"); H.eq(kind("OnDay1225"), "day")
	H.eq(kind("OnSun2359"), "weekday"); H.eq(kind("OnSat0000"), "weekday")
	H.eq(kind("OnMyMobDead"), "user")
	H.eq(kind("OnClock83"), "user", "OnClock needs hhmm")
	H.eq(Events.classify("L_Start"), nil)
	H.eq(Events.busName("OnInit"), "legacy:OnInit")
end)

H.case("LNPC-EVT-002", "the queue keeps two events in order", function()
	local q = Events.newQueue(true)
	H.truthy(q:push("a") and q:push("b"))
	H.eq(q:pop(), "a"); H.eq(q:pop(), "b"); H.eq(q:pop(), nil)
end)

H.case("LNPC-EVT-003", "Q-J: with both slots taken the NEW (third) event is the one dropped; flag off keeps everything", function()
	local q = Events.newQueue(true)
	q:push("A"); q:push("B")
	H.eq(q:push("C"), false)
	H.eq(q:size(), 2)
	H.eq(q:pop(), "A"); H.eq(q:pop(), "B")
	local free = Events.newQueue(false)
	for i = 1, 5 do H.truthy(free:push("e" .. i)) end
	H.eq(free:size(), 5)
end)

H.case("LNPC-EVT-004", "On* labels register bus hooks under their legacy names; clones register under their own names", function()
	local text = U.npc("Hooks", [[
OnInit: $log$ = $log$ + "I"; end;
OnTouch: $log$ = $log$ + "T"; end;
OnTimer5000: end;
OnClock0830: end;
OnMinute30: end;
OnPCLoginEvent: $log$ = $log$ + "L"; end;
OnMyMobDead: end;
mes "body"; close;
]], "prontera,1,1,0") .. "prontera,2,2,0\tduplicate(Hooks)\tHooks2\t111\n"
	local rt = U.rt1(text)
	local bus = fakeBus()
	local n = rt:registerEvents(bus.api)
	H.eq(n, 14, "7 labels x (template + clone)")
	local ids, names = {}, {}
	for _, h in bus.hooks do ids[h.spec.id] = h.event; names[h.event] = true end
	H.eq(ids["Hooks::OnInit"], "legacy:OnInit")
	H.eq(ids["Hooks2::OnInit"], "legacy:OnInit")
	H.eq(ids["Hooks::OnTimer5000"], "legacy:OnTimer5000")
	H.eq(ids["Hooks::OnClock0830"], "legacy:OnClock0830")
	H.eq(ids["Hooks::OnMinute30"], "legacy:OnMinute30")
	H.eq(ids["Hooks::OnPCLoginEvent"], "legacy:OnPCLoginEvent")
	H.eq(ids["Hooks::OnMyMobDead"], "legacy:OnMyMobDead")
	-- dispatching the bus event runs the label of every NPC that has it
	H.eq(bus.dispatch("legacy:OnInit"), 2)
	H.eq(U.worldGet(rt, "log$"), "II")
	rt:player(9, {})
	bus.dispatch("legacy:OnPCLoginEvent", 9)
	H.eq(U.worldGet(rt, "log$"), "IILL")
end)

H.case("LNPC-EVT-005", "fireAll runs OnInit in load order; clockTick fires the clock family as npc_event_do_clock does", function()
	local rt = U.rt1(
		U.npc("A", [[ OnInit: $o$ = $o$ + "a"; end; OnMinute05: $o$ = $o$ + "m"; end; OnClock1405: $o$ = $o$ + "c"; end;
OnHour14: $o$ = $o$ + "h"; end; OnSun1405: $o$ = $o$ + "s"; end; OnDay1018: $o$ = $o$ + "d"; end; ]]) ..
		U.npc("B", [[ OnInit: $o$ = $o$ + "b"; end; ]]) .. U.npc("C", [[ OnInit: $o$ = $o$ + "c"; end; ]]))
	H.eq(rt:init(), 3)
	H.eq(U.worldGet(rt, "o$"), "abc")
	rt.env.global["o$"] = nil
	local prev = { min = 4, hour = 14, mday = 18, month = 10, wday = 0 }
	local now = { min = 5, hour = 14, mday = 18, month = 10, wday = 0 }
	H.eq(rt:clockTick(now, prev), 3, "minute + clock + weekday; hour and day unchanged")
	H.truthy(string.find(U.worldGet(rt, "o$"), "m", 1, true) and string.find(U.worldGet(rt, "o$"), "c", 1, true) and string.find(U.worldGet(rt, "o$"), "s", 1, true))
	H.eq(string.find(U.worldGet(rt, "o$"), "h", 1, true), nil)
	rt.env.global["o$"] = nil
	H.eq(rt:clockTick({ min = 5, hour = 15, mday = 19, month = 10, wday = 1 }, now), 0, "nothing registered for these labels")
	rt.env.global["o$"] = nil
	H.eq(rt:clockTick(now, { min = 5, hour = 13, mday = 17, month = 10, wday = 6 }), 2, "hour and day changed, minute equal")
end)

H.case("LNPC-EVT-006", "an event aimed at a busy player is queued; the third is dropped; after the script ends the queue drains by 100 ms timers", function()
	local text = U.npc("Talk", [[ mes "hello"; next; mes "bye"; close; ]]) ..
		U.npc("L", [[ OnE1: $o$ = $o$ + "1"; end; OnE2: $o$ = $o$ + "2"; end; OnE3: $o$ = $o$ + "3"; end; ]])
	local rt = U.rt1(text)
	local p = rt:player(1, {})
	local api = require("../blocking").scripted({})
	rt.dialogue = api
	api.onRequest = function(kind)
		if kind == "say" and #api.log == 1 then
			-- the player is inside Talk: three events arrive
			H.truthy(rt:fireEvent("L::OnE1", p)); rt:fireEvent("L::OnE2", p); rt:fireEvent("L::OnE3", p)
			H.eq(p.queue:size(), 2)
			H.eq(U.worldGet(rt, "o$"), nil, "nothing ran while the player was busy")
		end
	end
	local o = rt:run("Talk", 1)
	H.eq(o.status, "end")
	H.eq(rt.stats.queued, 2); H.eq(rt.stats.dropped, 1)
	-- the script ended: slot 0 became a 100 ms player timer, slot 1 is still queued
	H.eq(U.worldGet(rt, "o$"), nil)
	H.eq(#p.timers, 1)
	rt:advance(99)
	H.eq(U.worldGet(rt, "o$"), nil)
	rt:advance(1)
	H.eq(U.worldGet(rt, "o$"), "1")
	-- E1's own run ended, which dequeues E2 the same way (another 100 ms timer)
	H.eq(p.queue:size(), 0)
	rt:advance(99); H.eq(U.worldGet(rt, "o$"), "1")
	rt:advance(1); H.eq(U.worldGet(rt, "o$"), "12")
	rt:advance(1000); H.eq(U.worldGet(rt, "o$"), "12", "E3 was dropped for good")
end)

H.case("LNPC-EVT-012", "a cancelled dialogue leaves the player busy until the host reports it (endDialogue), then the queue drains", function()
	local rt = U.rt1(U.npc("Talk", [[ mes "x"; next; mes "y"; close; ]]) .. U.npc("L", [[ OnE: $o$ = $o$ + "e"; end; ]]))
	local p = rt:player(1, {})
	-- the dialogue coroutine of script-host: say() yields to the host, which here simply never resumes it (= cancelled)
	rt.dialogue = { say = function(_) coroutine.yield() end, choose = function() return 1 end, input = function() return "" end, wait = function() end }
	local co = coroutine.create(function() rt:run("Talk", 1) end)
	coroutine.resume(co)
	H.eq(coroutine.status(co), "suspended")
	H.eq(p.busy, 1, "the cancelled run never reaches its own cleanup")
	rt:fireEvent("L::OnE", p)
	H.eq(p.queue:size(), 1)
	rt:endDialogue(1)
	H.eq(p.busy, 0)
	rt:advance(100)
	H.eq(U.worldGet(rt, "o$"), "e")
	-- abandoned dialogue runs do not eat the nesting budget of event runs
	for _ = 1, 40 do
		local c = coroutine.create(function() rt:run("Talk", 2) end)
		coroutine.resume(c)
	end
	H.eq(rt:runLabel(rt.loader.npcs.L, "OnE", nil).status, "end")
end)

H.case("LNPC-EVT-007", "with the compat flag off the queue is unbounded", function()
	local rt = U.rt1(U.npc("Talk", [[ mes "x"; next; close; ]]) .. U.npc("L", [[ OnE: end; ]]), { compat = { two_slot_event_queue = false } })
	local p = rt:player(1, {})
	local api = require("../blocking").scripted({})
	rt.dialogue = api
	local fired = false
	api.onRequest = function() if not fired then fired = true; for _ = 1, 5 do rt:fireEvent("L::OnE", p) end end end
	rt:run("Talk", 1)
	H.eq(rt.stats.dropped, 0)
	H.eq(rt.stats.queued, 5)
end)

H.case("LNPC-TIMER-001", "initnpctimer fires OnTimer<ms> labels in order at their times; stopnpctimer halts; startnpctimer resumes", function()
	local rt = U.rt1(U.npc("Tm", [[
		initnpctimer;
		end;
	OnTimer1000: $o$ = $o$ + "a"; end;
	OnTimer3000: $o$ = $o$ + "b"; end;
	OnTimer6000: $o$ = $o$ + "c"; end;
	]]) .. U.npc("Ctl", [[ stopnpctimer("Tm"); ]]) .. U.npc("Go", [[ startnpctimer("Tm"); ]]))
	U.play(rt, "Tm", {}, 1)
	rt:advance(999); H.eq(U.worldGet(rt, "o$"), nil)
	rt:advance(1); H.eq(U.worldGet(rt, "o$"), "a")
	rt:advance(2000); H.eq(U.worldGet(rt, "o$"), "ab")
	H.eq(rt:timerGet(rt.loader.npcs.Tm, 0), 3000)
	H.eq(rt:timerGet(rt.loader.npcs.Tm, 2), 3)
	U.play(rt, "Ctl", {}, 1)
	rt:advance(10000); H.eq(U.worldGet(rt, "o$"), "ab", "stopped")
	H.eq(rt:timerGet(rt.loader.npcs.Tm, 0), 3000, "stopped timers report their stopped time")
	U.play(rt, "Go", {}, 1)
	rt:advance(2999); H.eq(U.worldGet(rt, "o$"), "ab")
	rt:advance(1); H.eq(U.worldGet(rt, "o$"), "abc")
end)

H.case("LNPC-TIMER-002", "setnpctimer moves a running timer; getnpctimer(1) says whether an event is pending", function()
	local rt = U.rt1(U.npc("Tm", [[ initnpctimer; end;
	OnTimer1000: $o$ = $o$ + "a"; end;
	OnTimer9000: $o$ = $o$ + "z"; end; ]]) .. U.npc("Set", [[ setnpctimer 8000, "Tm"; ]]))
	U.play(rt, "Tm", {}, 1)
	rt:advance(1000); H.eq(U.worldGet(rt, "o$"), "a")
	H.eq(rt:timerGet(rt.loader.npcs.Tm, 1), 1)
	U.play(rt, "Set", {}, 1)
	rt:advance(999); H.eq(U.worldGet(rt, "o$"), "a")
	rt:advance(1); H.eq(U.worldGet(rt, "o$"), "az")
	H.eq(rt:timerGet(rt.loader.npcs.Tm, 1), 0, "no event left")
end)

H.case("LNPC-TIMER-003", "addtimer schedules a player event, deltimer cancels it, addtimercount shifts it", function()
	local rt = U.rt1(U.npc("Arm", [[ addtimer 1000, "L::OnA"; addtimer 2000, "L::OnB"; addtimer 3000, "L::OnC"; deltimer "L::OnB"; addtimercount 1500, "L::OnC"; end; ]]) ..
		U.npc("L", [[ OnA: $o$ = $o$ + "A"; end; OnB: $o$ = $o$ + "B"; end; OnC: $o$ = $o$ + "C"; end; ]]))
	U.play(rt, "Arm", {}, 1)
	rt:advance(1000); H.eq(U.worldGet(rt, "o$"), "A")
	rt:advance(3000); H.eq(U.worldGet(rt, "o$"), "A", "OnB was deleted, OnC moved to 4500")
	rt:advance(500); H.eq(U.worldGet(rt, "o$"), "AC")
end)

H.case("LNPC-TIMER-005", "OnTimerQuit runs once when the player an NPC timer is attached to logs out while the timer is pending", function()
	local rt = U.rt1(U.npc("Tm", [[ initnpctimer 1; end;
	OnTimer5000: $o$ = $o$ + "t"; end;
	OnTimerQuit: $o$ = $o$ + "q" + getcharid(0); end; ]]))
	rt:player(1, {})
	U.play(rt, "Tm", {}, 1)
	rt:advance(1000)
	rt:dropPlayer(1)
	H.eq(U.worldGet(rt, "o$"), "q1")
	rt:advance(10000)
	H.eq(U.worldGet(rt, "o$"), "q1", "the timer is gone")
	-- an expired timer has nothing to abort
	local rt2 = U.rt1(U.npc("Tm", [[ initnpctimer 1; end; OnTimer1000: $o$ = $o$ + "t"; end; OnTimerQuit: $o$ = $o$ + "q"; end; ]]))
	U.play(rt2, "Tm", {}, 1)
	rt2:advance(1000)
	rt2:dropPlayer(1)
	H.eq(U.worldGet(rt2, "o$"), "t")
end)

H.case("LNPC-TIMER-004", "a player may hold at most 32 event timers", function()
	local rt = U.rt1(U.npc("Many", [[ for (.@i = 0; .@i < 40; .@i++) addtimer 1000 + .@i, "L::OnA"; ]]) .. U.npc("L", [[ OnA: $n += 1; end; ]]))
	U.play(rt, "Many", {}, 1)
	rt:advance(5000)
	H.eq(U.worldGet(rt, "n"), 32)
end)

H.case("LNPC-EVT-008", "donpcevent runs the label at once with no player; returns 1 when the event exists, else 0", function()
	local rt = U.rt1(U.npc("Caller", [[ $r1 = donpcevent("L::OnGo"); $r2 = donpcevent("L::OnNone"); $r3 = donpcevent("Nobody::OnGo"); mes "x"; close; ]]) ..
		U.npc("L", [[ OnGo: $ran += 1; $who = getcharid(3); end; ]]))
	U.play(rt, "Caller", {}, 5)
	H.eq(U.worldGet(rt, "r1"), 1); H.eq(U.worldGet(rt, "r2"), nil); H.eq(U.worldGet(rt, "r3"), nil)
	H.eq(U.worldGet(rt, "ran"), 1)
	H.eq(U.worldGet(rt, "who"), nil, "no player attached to the event")
end)

H.case("LNPC-EVT-009", "doevent from a script queues for the attached player and runs after the script ends (100 ms)", function()
	local rt = U.rt1(U.npc("Caller", [[ doevent "L::OnGo"; mes "after"; close; ]]) .. U.npc("L", [[ OnGo: $ran += 1; $who = getcharid(3); end; ]]))
	U.play(rt, "Caller", {}, 5)
	H.eq(U.worldGet(rt, "ran"), nil)
	rt:advance(100)
	H.eq(U.worldGet(rt, "ran"), 1)
	H.eq(U.worldGet(rt, "who"), 2000005, "the event runs with the player attached")
end)

H.case("LNPC-EVT-010", "disablenpc stops player events from triggering, enablenpc restores them", function()
	local rt = U.rt1(U.npc("Sw", [[ disablenpc "L"; ]]) .. U.npc("Sw2", [[ enablenpc "L"; ]]) .. U.npc("L", [[ OnGo: $ran += 1; end; ]]))
	local p = rt:player(1, {})
	rt:fireEvent("L::OnGo", p); H.eq(U.worldGet(rt, "ran"), 1)
	U.play(rt, "Sw", {}, 1)
	rt:fireEvent("L::OnGo", p); H.eq(U.worldGet(rt, "ran"), 1, "hidden NPC: no trigger")
	U.play(rt, "Sw2", {}, 1)
	rt:fireEvent("L::OnGo", p); H.eq(U.worldGet(rt, "ran"), 2)
end)

H.case("LNPC-EVT-011", "an event handler that needs a dialogue ends with a typed NEEDS_DIALOGUE, sleeps continue on the scheduler", function()
	local rt = U.rt1(U.npc("L", [[ OnTalk: $a = 1; mes "x"; next; $b = 1; end; OnSleep: $s = 1; sleep 500; $s = 2; end; ]]))
	local o = rt:runLabel(rt.loader.npcs.L, "OnTalk", rt:player(1, {}))
	H.eq(o.status, "needs_dialogue")
	H.eq(U.worldGet(rt, "a"), 1); H.eq(U.worldGet(rt, "b"), nil)
	local o2 = rt:runLabel(rt.loader.npcs.L, "OnSleep", nil)
	H.eq(o2.status, "pending")
	H.eq(U.worldGet(rt, "s"), 1)
	rt:advance(499); H.eq(U.worldGet(rt, "s"), 1)
	rt:advance(1); H.eq(U.worldGet(rt, "s"), 2)
end)

return true
