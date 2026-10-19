--!nonstrict
-- C++ integer semantics for the status layer (pure Luau, no host call). Luau numbers are doubles; every value here is an int32 (or an int64
-- below 2^53) held exactly. The numerics lint (tools/lint_numeric.py) forbids `/` and `//` on signed values: the one division below runs on
-- absolute values and carries the `lint:nonneg` marker.
local M = {}

local INT32_MAX, INT32_MIN = 2147483647, -2147483648
M.INT32_MAX, M.INT32_MIN, M.USHRT_MAX, M.SHRT_MAX, M.SHRT_MIN, M.UINT_MAX = INT32_MAX, INT32_MIN, 65535, 32767, -32768, 4294967295

-- a signed int32 overflow is undefined behaviour in the legacy C++; the conformance runner counts a case that reaches it instead of comparing
function M.i32(v)
	if v > INT32_MAX or v < INT32_MIN then error({ code = "INT32_OVERFLOW", msg = tostring(v) }, 0) end
	return v
end

-- a / b for C++ integers: truncation toward zero
function M.tdiv(a, b)
	if b == 0 then error({ code = "DIV_BY_ZERO" }, 0) end
	local q = math.abs(a) // math.abs(b) -- lint:nonneg
	if (a < 0) ~= (b < 0) and q ~= 0 then return -q end
	return q
end

-- a * b / c in int32 (the product must fit, like the legacy expression `a * b / c`)
function M.mul_div(a, b, c) return M.tdiv(M.i32(a * b), c) end

-- cap_value(v, lo, hi)
function M.cap(v, lo, hi)
	if v >= hi then return hi end
	if v <= lo then return lo end
	return v
end

-- narrowing conversions of a return statement: two's complement wrap
function M.wrap_i16(v) return (v + 32768) % 65536 - 32768 end -- lint:wrap
function M.wrap_u16(v) return v % 65536 end -- lint:wrap
function M.wrap_u8(v) return v % 256 end -- lint:wrap
function M.wrap_i32(v) return (v + 2147483648) % 4294967296 - 2147483648 end -- lint:wrap

-- bit operations on int32 (arithmetic right shift, like the gcc build)
local function to_signed(u) if u >= 2147483648 then return u - 4294967296 end return u end
function M.band(a, b) return to_signed(bit32.band(a, b)) end
function M.bor(a, b) return to_signed(bit32.bor(a, b)) end
function M.bnot(a) return to_signed(bit32.bnot(a)) end
function M.shr(a, n) return to_signed(bit32.arshift(a, n)) end
function M.shl(a, n) return to_signed(bit32.lshift(a, n)) end

function M.max(a, b) if a > b then return a end return b end
function M.min(a, b) if a < b then return a end return b end
function M.b2i(b) if b then return 1 end return 0 end

return M
