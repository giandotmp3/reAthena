--!nonstrict
-- Integer helpers of the status calculation that need no ctx: 32-bit bit operations with C++ int32 results, narrowing casts and cap_value.
-- Division is NOT here: every division goes through ctx.idiv / ctx.mul_div (truncating, see scripts/damage/weapon.luau header); Luau `//`
-- and `/` floor / are doubles and legacy C++ truncates signed integers.
local M = {}

local TWO32 = 4294967296

-- two's complement int32 view of any integer-valued number (wraps like a C++ int32 conversion)
local function to_i32(x)
	x = x % TWO32 -- x may be negative: Lua's % is floored, so the result is in [0, 2^32)
	if x >= 2147483648 then return x - TWO32 end
	return x
end
M.to_i32 = to_i32

local b32 = rawget(_G, "bit32")
local band, bor, bxor, lshift, arshift
if b32 then
	band, bor, bxor, lshift, arshift = b32.band, b32.bor, b32.bxor, b32.lshift, b32.arshift
else -- plain Lua 5.4 (lupa): the host operators work on 64-bit integers
	band = load("return function(a, b) return a & b end")()
	bor = load("return function(a, b) return a | b end")()
	bxor = load("return function(a, b) return a ~ b end")()
	lshift = load("return function(a, n) return a << n end")()
	arshift = load("return function(a, n) return a >> n end")()
end

function M.band(a, b) return to_i32(band(to_i32(a), to_i32(b))) end
function M.bor(a, b) return to_i32(bor(to_i32(a), to_i32(b))) end
function M.bxor(a, b) return to_i32(bxor(to_i32(a), to_i32(b))) end
function M.bnot(a) return -to_i32(a) - 1 end
function M.shl(a, n) return to_i32(lshift(to_i32(a) % TWO32, n)) end
-- arithmetic shift right of an int32: floor(a / 2^n) without a division operator
function M.sar(a, n)
	a = to_i32(a)
	if a >= 0 then return to_i32(arshift(a, n)) end
	return M.bnot(to_i32(arshift(M.bnot(a) % TWO32, n)))
end

-- C++ narrowing conversion of an integer to an 8 / 16 / 32-bit integer type
local MOD = { [8] = 256, [16] = 65536, [32] = 4294967296 }
local HALF = { [8] = 128, [16] = 32768, [32] = 2147483648 }
function M.narrow(x, bits, signed)
	x = x % MOD[bits]
	if signed and x >= HALF[bits] then x = x - MOD[bits] end
	return x
end

-- cap_value(x, lo, hi)
function M.cap(x, lo, hi)
	if x < lo then return lo end
	if x > hi then return hi end
	return x
end

return M
