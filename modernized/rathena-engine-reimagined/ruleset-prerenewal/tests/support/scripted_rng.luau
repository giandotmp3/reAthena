--!nonstrict
-- RNG with draw counting. `scripted(draws)` replays recorded legacy draws in order: a number is a rnd() result,
-- {lo, hi, v} is a rnd_value(lo, hi) result. Asking for another kind of draw, other bounds, or a draw past the end
-- raises {code = "RNG_SEQUENCE"}: the sequence of draws is part of what is compared.
local M = {}

local function fail(msg) error({ code = "RNG_SEQUENCE", msg = msg }, 3) end

function M.scripted(draws)
	local rng = { used = 0, total = #draws }
	function rng.rnd()
		local i = rng.used + 1
		local d = draws[i]
		if d == nil then fail("draw " .. i .. ": script exhausted (asked rnd())") end
		if type(d) ~= "number" then fail("draw " .. i .. ": recorded rnd_value(" .. d[1] .. "," .. d[2] .. "), asked rnd()") end
		rng.used = i
		return d
	end
	function rng.value(lo, hi)
		local i = rng.used + 1
		local d = draws[i]
		if d == nil then fail("draw " .. i .. ": script exhausted (asked value(" .. lo .. "," .. hi .. "))") end
		if type(d) == "number" then fail("draw " .. i .. ": recorded rnd(), asked value(" .. lo .. "," .. hi .. ")") end
		if d[1] ~= lo or d[2] ~= hi then
			fail("draw " .. i .. ": recorded value(" .. d[1] .. "," .. d[2] .. "), asked value(" .. lo .. "," .. hi .. ")")
		end
		rng.used = i
		return d[3]
	end
	return rng
end

-- deterministic generator for unit tests that do not replay a recording (a 31-bit LCG, not the legacy stream)
function M.lcg(seed)
	local state = seed
	local rng = { used = 0 }
	local function nxt()
		state = (state * 1103515245 + 12345) % 2147483648
		rng.used = rng.used + 1
		return state
	end
	function rng.rnd() return nxt() end
	function rng.value(lo, hi) return lo + nxt() % (hi - lo + 1) end
	return rng
end

return M
