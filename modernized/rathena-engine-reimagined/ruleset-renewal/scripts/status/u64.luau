--!nonstrict
-- uint64 arithmetic for status_calc_maxhp / maxsp / maxap (status.cpp:8733-8781): `uint64 maxhp += <int32 bonus>` wraps around modulo 2^64 when a
-- negative bonus exceeds the value, and `maxhp * rate / 100` is evaluated in uint64 too. Doubles hold 53 bits, so the value is kept in four
-- little-endian 16-bit limbs (a limb times an int32 stays below 2^53).
local M = {}

function M.from(n) -- n: integer, 0 <= n < 2^53
	local l1 = n % 65536 -- lint:wrap
	n = (n - l1) // 65536 -- lint:nonneg
	local l2 = n % 65536 -- lint:wrap
	n = (n - l2) // 65536 -- lint:nonneg
	local l3 = n % 65536 -- lint:wrap
	return { l1, l2, l3, (n - l3) // 65536 } -- lint:nonneg
end

-- v + (uint64)(int32)b
function M.add_i32(v, b)
	local out, carry = {}, 0
	if b >= 0 then
		local x = b
		for i = 1, 4 do
			local lb = x % 65536 -- lint:wrap
			x = (x - lb) // 65536 -- lint:nonneg
			local t = v[i] + lb + carry
			out[i] = t % 65536 -- lint:wrap
			carry = (t - out[i]) // 65536 -- lint:nonneg
		end
	else
		local x, borrow = -b, 0
		for i = 1, 4 do
			local lb = x % 65536 -- lint:wrap
			x = (x - lb) // 65536 -- lint:nonneg
			local t = v[i] - lb - borrow
			if t < 0 then t = t + 65536; borrow = 1 else borrow = 0 end
			out[i] = t
		end
	end
	return out
end

-- (v * r mod 2^64) / d with 0 < r < 2^31 and d > 0 (unsigned division)
function M.mul_div_small(v, r, d)
	local out, carry = {}, 0
	for i = 1, 4 do
		local t = v[i] * r + carry
		out[i] = t % 65536 -- lint:wrap
		carry = (t - out[i]) // 65536 -- lint:nonneg
	end
	local q, rem = {}, 0
	for i = 4, 1, -1 do
		local cur = rem * 65536 + out[i]
		q[i] = cur // d -- lint:nonneg
		rem = cur - q[i] * d
	end
	return q
end

-- cap_value(v, lo, hi) for 0 <= lo <= hi < 2^32, as a number
function M.cap(v, lo, hi)
	if v[3] ~= 0 or v[4] ~= 0 then return hi end
	local x = v[2] * 65536 + v[1]
	if x >= hi then return hi end
	if x <= lo then return lo end
	return x
end

return M
