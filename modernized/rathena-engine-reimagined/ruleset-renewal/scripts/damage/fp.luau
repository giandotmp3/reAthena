--!nonstrict
-- IEEE floating point helpers for the places where the legacy renewal code computes in `float` / `double` (BR-RULESET-034, 010,
-- 025, M0 numerics note). Luau numbers are IEEE doubles, so double arithmetic is the legacy `double` arithmetic; `float` arithmetic
-- is a double operation rounded to binary32 (exact for + - * / and sqrt because a double has more than 2 * 24 + 2 bits).
--
-- The `/` operator is allowed in this file only: every line that uses it is a FLOATING POINT division the legacy code also does in
-- floating point (marked `lint:float`). Signed integer division never appears here; it goes through ctx.idiv / ctx.mul_div.
--
-- Candidate for rathena.base / script-host (BR-RULESET-115 `num`): there is no ctx.f32() in the host API today, only the lane step
-- `f32_step(factor)`; this module is the package-local stand-in (see docs/ARCHITECTURE_PROOF.md).
local M = {}

local TWO24 = 16777216

-- round a double to the nearest binary32 value (ties to even); normal range only (|x| < 2^127, no denormals needed here)
function M.f32(x)
	if x == 0 or x ~= x then return x end
	local neg = x < 0
	if neg then x = -x end
	local m, e = math.frexp(x) -- x = m * 2^e, 0.5 <= m < 1
	local scaled = m * TWO24 -- exact
	local fl = math.floor(scaled)
	local diff = scaled - fl
	if diff > 0.5 or (diff == 0.5 and fl % 2 == 1) then fl = fl + 1 end
	local r = math.ldexp(fl, e - 24)
	return neg and -r or r
end

local f32 = M.f32
function M.mul(a, b) return f32(a * b) end
function M.add(a, b) return f32(a + b) end
function M.sub(a, b) return f32(a - b) end
function M.div(a, b) return f32(a / b) end -- lint:float

-- double division (legacy `double` operands)
function M.ddiv(a, b) return a / b end -- lint:float

-- C++ (int64)x for a finite double: truncation toward zero
function M.trunc(x)
	if x >= 0 then return math.floor(x) end
	return math.ceil(x)
end

-- C++ (int64)floor((float)v) for an integer v (what `(int64)floor((float)(damage * rate / 100))` does after the integer division)
function M.floor_f32_of_int(v)
	return math.floor(f32(v))
end

return M
