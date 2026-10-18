--!nonstrict
-- IEEE floating point helpers for the places where the legacy renewal code computes in `float` / `double` (BR-RULESET-034, 010,
-- 025, M0 numerics note). Luau numbers are IEEE doubles, so double arithmetic is the legacy `double` arithmetic; `float` arithmetic
-- is a double operation rounded to binary32 (exact for + - * / and sqrt because a double has more than 2 * 24 + 2 bits), and the
-- rounding is the host's native `num.f32` (script-host numeric API, `(double)(float)x`): register(engine) binds it here.
--
-- The `/` operator is allowed in this file only: every line that uses it is a FLOATING POINT division the legacy code also does in
-- floating point (marked `lint:float`). Signed integer division never appears here; it goes through ctx.idiv / ctx.mul_div.
local M = {}

local f32 = nil -- engine.num.f32
local bound = nil -- the engine.num table it came from

function M.bind(num) bound, f32 = num, num.f32 end
function M.binding() return bound end
function M.f32(x) return f32(x) end

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
