--!nonstrict
-- Pure-Luau fake of the engine `ctx` (interface: scripts/damage/weapon.luau header) with C++ int64 truncating
-- semantics on integer-safe arithmetic: every product and result must stay below 2^53 in magnitude (asserted),
-- so doubles (Luau) and 64-bit integers (Lua 5.4) give identical results. Never uses `//` or `/` on signed values.
local M = {}

local LIMIT = 9007199254740992 -- 2^53

local function check(v, what)
	if v ~= v or v >= LIMIT or v <= -LIMIT then
		error({ code = "FAKE_CTX_OVERFLOW", msg = what .. " leaves the integer-safe range: " .. tostring(v) })
	end
	return v
end

-- C++ a / b for integers: truncate toward zero. math.fmod keeps the dividend's sign and is exact, so (a - r) is an
-- exact multiple of b and the quotient below is exact.
local function idiv(a, b)
	if b == 0 then error({ code = "DIV_BY_ZERO" }) end
	local r = math.fmod(a, b)
	return math.floor((a - r) / b + 0.5)
end

local function mul_div(v, m, d)
	return idiv(check(v * m, "product"), d)
end

local Acc = {}
Acc.__index = Acc
function Acc:get() return self.v end
function Acc:set(v) self.v = check(v, "set") end
function Acc:add(v) self.v = check(self.v + v, "add") end
function Acc:mul_pct(p) self.v = mul_div(self.v, p, 100) end
function Acc:add_pct(p) self.v = check(self.v + mul_div(self.v, p, 100), "add_pct") end
function Acc:clamp_min(lo) if self.v < lo then self.v = lo end end

-- rng: { rnd = function() end, value = function(lo, hi) end }
function M.new(rng)
	return {
		rng = rng,
		idiv = idiv,
		mul_div = mul_div,
		acc = function(v) return setmetatable({ v = check(v, "acc") }, Acc) end,
	}
end

M.idiv, M.mul_div = idiv, mul_div
return M
