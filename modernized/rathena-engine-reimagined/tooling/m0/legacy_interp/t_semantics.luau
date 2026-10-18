--!nonstrict
-- Semantic regression tests for the interpreter design (locals/labels, goto, switch, callsub/getarg, refs, menus, depth).
local C = require("./corpus")
local parser = require("./parser")
local lower = require("./lower")
local D = require("./driver")
local I = require("./interp")

local pass, fail = 0, 0
local function run(src, opts)
	opts = opts or {}
	local ast, info = parser.parse(src, C.builtins, {})
	if not ast then return nil, "PARSE " .. info.cause .. " " .. tostring(info.detail) end
	local prog, lerr = lower.lower(ast, { name = "t", builtins = C.builtins, userfuncs = {} })
	if not prog then return nil, "LOWER " .. tostring(lerr.cause) end
	local r = D.runOnce(prog, 1, { profile = "rich", picks = opts.picks, wantLog = true, ext = true, limit = opts.limit, glimit = opts.glimit })
	return r, nil
end
local function mes(r)
	local out = {}
	for _, l in r.log do
		local m = string.match(l, "^mes%((.*)%)$")
		if m then out[#out + 1] = m end
	end
	return table.concat(out, "|")
end
local function check(name, src, expect, opts)
	local r, err = run(src, opts)
	local got = r and mes(r) or err
	if r and r.status == "error" then got = got .. " <ERR " .. tostring(r.err.type) .. ">" end
	if got == expect then pass += 1 else fail += 1; print("FAIL " .. name .. "\n   expected: " .. expect .. "\n   got:      " .. tostring(got)) end
end

check("locals shared across labels via goto", [[
	.@a = 5;
	goto L_X;
	mes "skipped";
L_X:
	.@a += 1;
	goto L_Y;
L_Z:
	mes "z" + .@a;
	end;
L_Y:
	.@a *= 2;
	goto L_Z;
]], "z12")

check("callsub gets fresh .@ frame, caller's untouched", [[
	.@a = 1;
	callsub L_S, 7;
	mes "caller a=" + .@a;
	mes "ret=" + callsub(L_R, 3, 4);
	end;
L_S:
	.@a = getarg(0) * 100;
	mes "callee a=" + .@a;
	return;
L_R:
	return getarg(0) + getarg(1);
]], "callee a=700|caller a=1|ret=7")

check("operator precedence", [[
	mes "" + (1 + 2 * 3 << 1);
	mes "" + (10 - 2 - 3);
	mes "" + (-7 / 2);
	mes "" + (-7 % 3);
	mes "" + (1 ? 2 : 0 ? 4 : 5);
	mes "" + (0 ? 2 : 0 ? 4 : 5);
	mes "" + (1 == 1 && 2 > 1 || 0);
	mes "" + (6 & 3 | 8 ^ 1);
	mes "a" + 1 + 2 + "b";
	mes "" + (1 + 2 + "x");
	mes "" + (!0 + 1);
	mes "" + (3 > 2 == 1);
]], "14|5|-3|-1|2|5|1|11|a12b|3x|2|1")

check("switch fallthrough/default/break + loops", [[
	for (.@i = 0; .@i < 5; .@i++) {
		switch (.@i) {
		case 0: mes "zero";
		case 1: mes "one"; break;
		default: mes "dflt";
		case 3: mes "three"; break;
		case 4: continue;
		}
		mes "after" + .@i;
	}
	.@n = 0;
	do { .@n++; if (.@n == 2) continue; if (.@n > 3) break; mes "n" + .@n; } while (1);
	while (.@n < 6) .@n += 1;
	mes "end" + .@n;
]], "zero|one|after0|one|after1|dflt|three|after2|three|after3|n1|n3|end6")

check("arrays, sigils, by-reference getarg", [[
	setarray .@a[0], 10, 20, 30;
	mes "" + getarraysize(.@a);
	.@a[5] = 7;
	mes "" + getarraysize(.@a) + ":" + .@a[1] + ":" + .@a[4];
	callsub L_W, .@a[2], .@b;
	mes "b=" + .@b + " a2=" + .@a[2];
	$g = 4; $@t = 5; #acc = 6; ##acc2 = 7; 'inst = 8; .npc = 9; @tmp = 10; perm = 11;
	mes "" + ($g + $@t + #acc + ##acc2 + 'inst + .npc + @tmp + perm);
	.@s$ = "x"; .@s$[2] = "z";
	mes .@s$ + .@s$[2] + getarraysize(.@s$);
	end;
L_W:
	set getarg(1), 99;
	set getarg(0), getarg(0) + 1;
	return;
]], "3|6:20:0|b=99 a2=31|52|xz3") -- 52: 'inst cannot be written outside an instance (legacy), so 60 - 8

check("menu with '-' and select, @menu numbering with empties", [[
	menu "A::B", L_AB, "", L_NO, "C", -;
	mes "fell through " + @menu;
	end;
L_AB:
	mes "ab " + @menu;
	end;
L_NO:
	mes "never";
]], "ab 1", { picks = { 1 } })
check("menu choice 3 (B) numbering consistent with empties", [[
	menu "A::B", L_AB, "", L_NO, "C", -;
	mes "fell through " + @menu;
	end;
L_AB:
	mes "ab " + @menu;
	end;
]], "ab 3", { picks = { 2 } })
check("menu '-' continues after the menu", [[
	menu "A::B", L_AB, "", L_NO, "C", -;
	mes "fell through " + @menu;
	end;
L_AB:
	mes "ab " + @menu;
	end;
]], "fell through 5", { picks = { 3 } })
check("select/prompt", [[
	.@r = select("one:two:three");
	mes "r=" + .@r + " m=" + @menu;
	if (select("yes:no") == 2) mes "no"; else mes "yes";
]], "r=2 m=2|no", { picks = { 2, 2 } })

check("goto budget (infinite loop) raises typed error", [[
L: goto L;
]], " <ERR BUDGET_EXCEEDED>")
check("freeloop lifts budget", [[
	freeloop(1);
	for (.@i = 0; .@i < 30000; .@i++) { .@s += 2; }
	mes "" + .@s;
]], "60000", { limit = 10000, glimit = 2048 })
check("unsupported command is a typed error naming the command", [[
	mes "before";
	bg_warp 1, "x", 1, 1;
	mes "after";
]], "before <ERR UNSUPPORTED>")

check("callsub depth 2000 on heap frames (no host recursion)", [[
	mes "" + callsub(L_D, 0);
	end;
L_D:
	if (getarg(0) >= 2000) return getarg(0);
	callsub L_D, getarg(0) + 1;
	return getarg(0);
	]], "0", { limit = 1e9, glimit = 1e9 })
I.MAX_DEPTH = 2048
check("callsub depth beyond MAX_DEPTH raises CALL_DEPTH", [[
	callsub L_D, 0;
	end;
L_D:
	callsub L_D, getarg(0) + 1;
	return;
]], " <ERR CALL_DEPTH>", { limit = 1e9, glimit = 1e9 })

-- callsub under && / || / ?: is hoisted and recursion stays on heap frames (no host-stack recursion path any more)
I.MAX_DEPTH = 1000000
for _, depth in { 100, 1000, 2500, 5000, 20000 } do
	local src = ([[
	mes "" + (1 && callsub(L_R, 0));
	end;
L_R:
	if (getarg(0) >= %d) return 1;
	return (1 && callsub(L_R, getarg(0) + 1));
]]):format(depth)
	check("callsub under && recursion depth " .. depth .. " (heap frames)", src, "1", { limit = 1e9, glimit = 1e9 })
end
I.MAX_DEPTH = 2048

-- legacy has no short-circuit: every operand of && || ?: is evaluated (script.cpp C_LAND/C_LOR/C_OP3 are stack ops)
check("no short-circuit: && || ?: evaluate all operands (incl. hoisted callsub)", [[
	.@a = (0 && callsub(L_S));
	.@b = (1 || callsub(L_S));
	.@c = (1 ? callsub(L_S) : callsub(L_S));
	.@d = (0 && (.@x = 5));
	mes "cnt=" + $cnt + " a=" + .@a + " b=" + .@b + " c=" + .@c + " x=" + .@x + " d=" + .@d;
	end;
L_S:
	$cnt += 1;
	return 7;
]], "cnt=4 a=0 b=1 c=7 x=5 d=0")

check("callsub returning a string keeps the type when hoisted", [[
	mes "x" + callsub(L_S, 1);
	.@a$ = callsub(L_S, 2);
	mes .@a$;
	end;
L_S:
	return "str" + getarg(0);
]], "xstr1|str2")
do
	local r = run([[
	if (countitem(501) != 0 && callsub(L_S) == 7) mes "yes";
	.@x = countitem(502) + callsub(L_S) + countitem(503);
	end;
L_S:
	mes "in-sub";
	return 7;
]])
	local got = table.concat(r.log, ",")
	local want = "countitem(501),mes(in-sub),mes(yes),countitem(502),mes(in-sub),countitem(503)"
	if got == want then pass += 1 else fail += 1; print("FAIL effect order\n   expected: " .. want .. "\n   got:      " .. got) end
end

-- statements between 'switch (...) {' and the first 'case' execute (legacy compiles to a compare chain)
check("switch: statements before the first case execute", [[
	switch (2) {
		mes "pre";
		case 1: mes "one"; break;
		case 2: mes "two"; break;
	}
	mes "done";
]], "pre|two|done")
check("switch compat: $@__swN_val left behind (non-zero when no case matched, 0 when matched)", [[
	switch (2) { case 1: break; case 2: break; }
	switch (7) { case 1: mes "no"; break; }
	switch (9) { case 1: break; default: mes "dflt"; break; }
	mes "v0=" + $@__sw0_val + " v1=" + $@__sw1_val + " v2=" + $@__sw2_val;
]], "dflt|v0=0 v1=7 v2=9")
check("switch compat: index counter shared with if/while/for/do/function{}", [[
	if (1) { .@a = 1; }
	for (.@i = 0; .@i < 1; .@i++) { .@a++; }
	switch (5) { case 1: break; }
	mes "" + $@__sw2_val;
]], "5")
check("switch: body falling into mid-switch default jumps on to the next compare with VAL=0 (case 0 matches, default body skipped)", [[
	switch (1) {
		case 1: mes "one";
		default: mes "dflt";
		case 0: mes "zero"; break;
	}
]], "one|zero")
check("bit operators are int64-correct (bit32 is not)", [[
	mes "" + (1 << 40) + "|" + (-1 & 255) + "|" + (4294967296 | 1) + "|" + (-8 >> 1) + "|" + (~5) + "|" + (6 ^ 3) + "|" + (-1 ^ 1);
]], "1099511627776|255|4294967297|-4|-6|5|-2")
check("strcmp/strtol/sprintf/sscanf/inarray", [[
	mes "" + strcmp("a", "c") + "|" + strcmp("b", "a") + "|" + strcmp("x", "x");
	mes strtoupper("aBc") + strtolower("AbC") + "|" + strtol("ff", 16) + "|" + strtol("-0x10", 0) + "|" + axtoi("1F");
	mes sprintf("%05d|%-4s|%x|%.2s|%%|%c", 42, "ab", 255, "xyz", 65);
	.@n = sscanf("Yellow Mage Hat", "%s Mage Hat", .@c$);
	mes .@n + .@c$;
	.@m = sscanf("12 abc 7", "%d %s %d", .@a, .@s$, .@b);
	mes .@m + ":" + .@a + .@s$ + .@b;
	setarray .@arr$[0], "x", "y", "z";
	mes inarray(.@arr$, "z") + "," + inarray(.@arr$, "q");
]], "-2|1|0|ABCabc|255|-16|31|00042|ab  |ff|xy|%|A|1Yellow|3:12abc7|2,-1")
lower.switchCompat = false
check("switch compat OFF: table dispatch, pre-case statements run, no global left behind", [[
	switch (2) { mes "pre"; case 1: mes "one"; break; case 2: mes "two"; default: mes "d"; }
	switch (7) { case 1: break; }
	mes "[" + $@__sw0_val + "][" + $@__sw1_val + "]";
]], "pre|two|d|[0][0]")
lower.switchCompat = true

print(string.format("semantics tests: %d passed, %d failed", pass, fail))
