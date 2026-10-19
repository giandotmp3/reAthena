--!nonstrict
-- The expression language of status definitions (`value = "40 + 30 * level"`, `when = "u.is_pc == 0 && target == BCT_ENEMY"`). A small C-like
-- integer language with the legacy semantics: int32 arithmetic (overflow is an error, it is undefined behaviour in the legacy C++), `/`
-- truncates toward zero, `>>` is arithmetic, comparisons and `!` give 0/1.
--
--   operators (C precedence): || && | & == != < <= > >= << >> + - * / % unary - ! ~     calls: max(a,b) min(a,b)
--   identifiers: a field name of the status (the names in `fields`), a constant of scripts/status/consts, or `u.<fact>` (the unit's facts:
--   u.is_pc u.is_hom u.is_mob u.vit u.id u.skill.<NAME> ...). A fact the unit does not have reads 0.
--
-- compile(src, fieldslots, consts) -> function(vals, u) -> number;   fieldslots maps a field name to its slot index 1..4 in `vals`.
local num = import("scripts/status/num")

local M = {}

local function tokenize(src)
	local toks, i, n = {}, 1, #src
	while i <= n do
		local c = src:sub(i, i)
		if c:match("%s") then
			i += 1
		elseif c:match("%d") then
			local hex = src:match("^0[xX]%x+", i)
			local lit = hex or src:match("^%d+", i)
			toks[#toks + 1] = { t = "num", v = tonumber(lit) }
			i += #lit
		elseif c:match("[%a_]") then
			local id = src:match("^[%a_][%w_]*", i)
			local j = i + #id
			while src:sub(j, j) == "." and src:sub(j + 1, j + 1):match("[%a_]") do
				local part = src:match("^[%a_][%w_]*", j + 1)
				id = id .. "." .. part
				j = j + 1 + #part
			end
			toks[#toks + 1] = { t = "id", v = id }
			i = j
		else
			local two = src:sub(i, i + 1)
			if two == "||" or two == "&&" or two == "==" or two == "!=" or two == "<=" or two == ">=" or two == "<<" or two == ">>" then
				toks[#toks + 1] = { t = "op", v = two }
				i += 2
			elseif c:match("[%+%-%*/%%&|<>!~%(%),]") then
				toks[#toks + 1] = { t = "op", v = c }
				i += 1
			else
				error("expr: unexpected character '" .. c .. "' in: " .. src, 0)
			end
		end
	end
	return toks
end

local BINARY = {
	{ ["||"] = true }, { ["&&"] = true }, { ["|"] = true }, { ["&"] = true }, { ["=="] = true, ["!="] = true },
	{ ["<"] = true, ["<="] = true, [">"] = true, [">="] = true }, { ["<<"] = true, [">>"] = true }, { ["+"] = true, ["-"] = true },
	{ ["*"] = true, ["/"] = true, ["%"] = true },
}

local function binop(op, a, b)
	if op == "||" then return function(v, u) if a(v, u) ~= 0 or b(v, u) ~= 0 then return 1 end return 0 end end
	if op == "&&" then return function(v, u) if a(v, u) ~= 0 and b(v, u) ~= 0 then return 1 end return 0 end end
	if op == "|" then return function(v, u) return num.bor(a(v, u), b(v, u)) end end
	if op == "&" then return function(v, u) return num.band(a(v, u), b(v, u)) end end
	if op == "==" then return function(v, u) return num.b2i(a(v, u) == b(v, u)) end end
	if op == "!=" then return function(v, u) return num.b2i(a(v, u) ~= b(v, u)) end end
	if op == "<" then return function(v, u) return num.b2i(a(v, u) < b(v, u)) end end
	if op == "<=" then return function(v, u) return num.b2i(a(v, u) <= b(v, u)) end end
	if op == ">" then return function(v, u) return num.b2i(a(v, u) > b(v, u)) end end
	if op == ">=" then return function(v, u) return num.b2i(a(v, u) >= b(v, u)) end end
	if op == "<<" then return function(v, u) return num.shl(a(v, u), b(v, u)) end end
	if op == ">>" then return function(v, u) return num.shr(a(v, u), b(v, u)) end end
	if op == "+" then return function(v, u) return num.i32(a(v, u) + b(v, u)) end end
	if op == "-" then return function(v, u) return num.i32(a(v, u) - b(v, u)) end end
	if op == "*" then return function(v, u) return num.i32(a(v, u) * b(v, u)) end end
	if op == "/" then return function(v, u) return num.tdiv(a(v, u), b(v, u)) end end
	if op == "%" then return function(v, u) return math.fmod(a(v, u), b(v, u)) end end -- C++ remainder: the sign of the dividend
	error("expr: operator " .. op, 0)
end

local function fact(path)
	local parts = {}
	for p in path:gmatch("[^%.]+") do parts[#parts + 1] = p end
	return function(_, u)
		local cur = u
		for i = 2, #parts do
			if type(cur) ~= "table" then return 0 end
			cur = cur[parts[i]]
		end
		if cur == nil then return 0 end
		if cur == true then return 1 end
		if cur == false then return 0 end
		return cur
	end
end

function M.compile(src, fieldslots, consts)
	local toks, pos = tokenize(src), 1
	local function peek() return toks[pos] end
	local function take() pos += 1; return toks[pos - 1] end
	local function expect(op)
		local t = take()
		if not t or t.t ~= "op" or t.v ~= op then error("expr: expected '" .. op .. "' in: " .. src, 0) end
	end

	local parse_level
	local function primary()
		local t = take()
		if not t then error("expr: unexpected end in: " .. src, 0) end
		if t.t == "num" then
			local k = t.v
			return function() return k end
		elseif t.t == "id" then
			local nt = peek()
			if nt and nt.t == "op" and nt.v == "(" and (t.v == "max" or t.v == "min") then
				take()
				local a = parse_level(1)
				expect(",")
				local b = parse_level(1)
				expect(")")
				local f = t.v == "max" and num.max or num.min
				return function(v, u) return f(a(v, u), b(v, u)) end
			end
			if t.v:sub(1, 2) == "u." then return fact(t.v) end
			local slot = fieldslots[t.v]
			if slot then return function(v) return v[slot] end end
			local k = consts[t.v]
			if k == nil then error("expr: unknown identifier '" .. t.v .. "' in: " .. src, 0) end
			return function() return k end
		elseif t.t == "op" and t.v == "(" then
			local e = parse_level(1)
			expect(")")
			return e
		elseif t.t == "op" and (t.v == "-" or t.v == "!" or t.v == "~") then
			local e = primary()
			if t.v == "-" then return function(v, u) return -e(v, u) end end
			if t.v == "!" then return function(v, u) return num.b2i(e(v, u) == 0) end end
			return function(v, u) return num.bnot(e(v, u)) end
		end
		error("expr: unexpected token in: " .. src, 0)
	end

	function parse_level(level)
		if level > #BINARY then return primary() end
		local left = parse_level(level + 1)
		while true do
			local t = peek()
			if t and t.t == "op" and BINARY[level][t.v] then
				take()
				left = binop(t.v, left, parse_level(level + 1))
			else
				return left
			end
		end
	end

	local e = parse_level(1)
	if pos <= #toks then error("expr: trailing tokens in: " .. src, 0) end
	return e
end

return M
