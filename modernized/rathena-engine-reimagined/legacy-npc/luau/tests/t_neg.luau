--!nonstrict
-- Parser accept/reject cases. Ported from tooling/m0/legacy_interp/t_neg.luau, which only printed OK/FAIL; the outcomes recorded
-- there (and the failure causes) are asserted here. Tag LNPC-PARSE-005.
local H = require("./harness")
local B = require("../builtins")
local parser = require("../parser")

local rejects = {
	{ 'mes;', "too_few_args" }, { 'foo "x";', "unknown_command" }, { 'close "x";', "too_many_args" }, { 'mes "a"', "expected_;" },
	{ 'if (1) { mes "a"; ', "unexpected_eof_in_block" }, { 'x = ;', "unexpected_token" }, { 'case 1: mes "a";', "unexpected_case" },
	{ 'mes "a;', "lex_unterminated_string" }, { 'switch (1) { case x y: }', "expected_:" },
}
local accepts = {
	'break;', -- accepted by the parser, rejected when lowering (no enclosing loop)
	'mes "ok"; close;', 'if (a) && (b) mes "x";', 'for (.@i = 0; .@i < 3; .@i++) { mes "x"; }', '.@a = 1 + 2 * 3 << 1;',
	'menu "a",-,"b",L_x; L_x:', 'while (1) { break; } do { continue; } while (0);', 'set .@a, getarg(0, 1) ? 1 : 2;',
	'switch (select("a:b")) { case 1: mes "a"; break; default: mes "d"; }',
}
for _, r in rejects do
	H.case("LNPC-PARSE-005", "parser rejects: " .. r[1], function()
		local ast, info = parser.parse(r[1], B, {})
		H.eq(ast, nil, "expected a parse error")
		H.eq(info.cause, r[2])
	end)
end
for _, src in accepts do
	H.case("LNPC-PARSE-005", "parser accepts: " .. src, function()
		local ast, info = parser.parse(src, B, {})
		H.truthy(ast, info and (tostring(info.cause) .. " " .. tostring(info.detail)))
	end)
end
return true
