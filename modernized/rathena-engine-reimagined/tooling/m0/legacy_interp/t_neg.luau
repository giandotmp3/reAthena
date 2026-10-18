local C = require("./corpus")
local parser = require("./parser")
local cases = {
 'mes;', 'foo "x";', 'close "x";', 'mes "a"', 'if (1) { mes "a"; ', 'x = ;', 'case 1: mes "a";', 'break;', 'mes "a;', 'switch (1) { case x y: }',
 'mes "ok"; close;', 'if (a) && (b) mes "x";', 'for (.@i = 0; .@i < 3; .@i++) { mes "x"; }', '.@a = 1 + 2 * 3 << 1;', 'menu "a",-,"b",L_x; L_x:', 'while (1) { break; } do { continue; } while (0);',
 'set .@a, getarg(0, 1) ? 1 : 2;', 'switch (select("a:b")) { case 1: mes "a"; break; default: mes "d"; }',
}
for _, c in cases do
  local ast, info = parser.parse(c, C.builtins, {})
  print(ast and "OK  " or "FAIL", c, ast and "" or (info.cause .. " " .. tostring(info.detail)))
end
