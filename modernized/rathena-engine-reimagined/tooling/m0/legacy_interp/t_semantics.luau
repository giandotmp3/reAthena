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
]], "3|6:20:0|b=99 a2=31|60|xz3")

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

-- expression-position (non-hoistable) recursion depth on the host stack
I.MAX_DEPTH = 1000000
for _, depth in { 100, 190, 250, 1000, 1500, 2000, 2500, 3000, 4000, 5000 } do
	local src = ([[
	mes "" + (1 && callsub(L_R, 0));
	end;
L_R:
	if (getarg(0) >= %d) return 1;
	return (1 && callsub(L_R, getarg(0) + 1));
]]):format(depth)
	local r, err = run(src, { limit = 1e9, glimit = 1e9 })
	local res = r and (mes(r) .. (r.status == "error" and (" ERR " .. tostring(r.err.type) .. " " .. tostring(r.err.msg)) or "")) or err
	print(string.format("host-recursive nested callsub (under &&) depth=%d -> %s", depth, string.sub(res, 1, 100)))
end

print(string.format("semantics tests: %d passed, %d failed", pass, fail))
