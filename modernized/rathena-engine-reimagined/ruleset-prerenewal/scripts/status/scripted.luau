--!nonstrict
-- BR-RULESET-075: scripted statuses. A status record may carry a `Script:` (rAthena script text); during the recalculation of a player the
-- script of every active status runs and its `bonus` commands feed the bonus table (`bonus bSpeedRate, getstatus(SC_SPEEDUP0, 1);`). This
-- is the precedent for "a status contributes through a small program": here the program is interpreted, the contributions it makes are
-- returned as data. Supported language: the subset the 68 scripts of db/pre-re/status.yml use -- `bonus` .. `bonus5`, `.@local = expr;`,
-- `if (cond) stmt [else stmt]`, blocks, integers, constants, + - * / unary - and !, comparisons, && ||, getstatus(SC_X) / getstatus(SC_X, n).
--
--   run(ctx, names, active, K) -> { {n, type, arg2, ..., value}, ... }   one record per bonus command, the numeric constants resolved
--     names   the statuses to run (SC_ suffixes); they run in ascending sc_type order (the legacy iterates an unordered map; bonuses add up)
--     active  { NAME = {val1..val4} }
local define = import("scripts/status/define")

local M = {}

local function tokenize(src)
	local toks, i, n = {}, 1, #src
	while i <= n do
		local c = src:sub(i, i)
		if c:match("%s") then i = i + 1
		elseif c:match("%d") then
			local j = i
			while j <= n and src:sub(j, j):match("[%dxXa-fA-F]") do j = j + 1 end
			toks[#toks + 1] = { "num", tonumber(src:sub(i, j - 1)) }
			i = j
		elseif c:match("[%a_%.@]") then
			local j = i + 1
			while j <= n and src:sub(j, j):match("[%w_%.@]") do j = j + 1 end
			toks[#toks + 1] = { "id", src:sub(i, j - 1) }
			i = j
		else
			local two = src:sub(i, i + 1)
			if two == "&&" or two == "||" or two == "==" or two == "!=" or two == "<=" or two == ">=" then
				toks[#toks + 1] = { "op", two }; i = i + 2
			else
				toks[#toks + 1] = { "op", c }; i = i + 1
			end
		end
	end
	return toks
end

local PREC = { ["||"] = 1, ["&&"] = 2, ["=="] = 3, ["!="] = 3, ["<"] = 4, [">"] = 4, ["<="] = 4, [">="] = 4, ["+"] = 5, ["-"] = 5, ["*"] = 6, ["/"] = 6 }

-- runs one script; `emit(record)` receives every bonus command
local function exec(src, env)
	local toks, p = tokenize(src), 1
	local function peek() return toks[p] end
	local function take() local t = toks[p]; p = p + 1; return t end
	local function expect(v)
		local t = take()
		if t == nil or t[2] ~= v then error("script: expected '" .. v .. "' near token " .. p .. " in: " .. src) end
	end
	local expr
	local function primary(run)
		local t = take()
		if t[1] == "num" then return t[2] end
		if t[1] == "op" and t[2] == "(" then local v = expr(run, 0); expect(")"); return v end
		if t[1] == "op" and t[2] == "-" then return -primary(run) end
		if t[1] == "op" and t[2] == "!" then return (primary(run) == 0) and 1 or 0 end
		if t[1] == "id" then
			if t[2] == "getstatus" then
				expect("(")
				local sc = take()[2]
				local arg = nil
				if peek() and peek()[2] == "," then take(); arg = expr(run, 0) end
				expect(")")
				local name = sc:sub(4) -- SC_X -> X
				local st = env.active[name]
				if st == nil then return 0 end
				if arg == nil then return 1 end
				return st["val" .. arg] or 0 -- (type 5, the remaining time, is not modelled)
			end
			if t[2]:sub(1, 2) == ".@" then return env.locals[t[2]] or 0 end
			local v = env.K.SCRIPT[t[2]]
			if v == nil then error("script: unknown constant " .. t[2]) end
			return v
		end
		error("script: unexpected token " .. tostring(t[2]))
	end
	function expr(run, minp)
		local lhs = primary(run)
		while true do
			local t = peek()
			local pr = t and t[1] == "op" and PREC[t[2]]
			if not pr or pr <= minp then break end
			take()
			local rhs = expr(run, pr)
			local op = t[2]
			if op == "+" then lhs = lhs + rhs elseif op == "-" then lhs = lhs - rhs elseif op == "*" then lhs = lhs * rhs
			elseif op == "/" then lhs = env.idiv(lhs, rhs)
			elseif op == "==" then lhs = lhs == rhs and 1 or 0 elseif op == "!=" then lhs = lhs ~= rhs and 1 or 0
			elseif op == "<" then lhs = lhs < rhs and 1 or 0 elseif op == ">" then lhs = lhs > rhs and 1 or 0
			elseif op == "<=" then lhs = lhs <= rhs and 1 or 0 elseif op == ">=" then lhs = lhs >= rhs and 1 or 0
			elseif op == "&&" then lhs = (lhs ~= 0 and rhs ~= 0) and 1 or 0 elseif op == "||" then lhs = (lhs ~= 0 or rhs ~= 0) and 1 or 0 end
		end
		return lhs
	end
	local stmt
	local function skip_stmt() -- skips one statement without executing it (the untaken branch)
		local t = peek()
		if t[2] == "{" then
			local depth = 0
			repeat
				local x = take()
				if x[2] == "{" then depth = depth + 1 elseif x[2] == "}" then depth = depth - 1 end
			until depth == 0
		elseif t[2] == "if" then
			take(); expect("("); local d = 1
			while d > 0 do local x = take(); if x[2] == "(" then d = d + 1 elseif x[2] == ")" then d = d - 1 end end
			skip_stmt()
			if peek() and peek()[2] == "else" then take(); skip_stmt() end
		else
			while take()[2] ~= ";" do end
		end
	end
	function stmt()
		local t = peek()
		if t[2] == ";" then take(); return end
		if t[2] == "{" then
			take()
			while peek()[2] ~= "}" do stmt() end
			take(); return
		end
		if t[2] == "if" then
			take(); expect("(")
			local c = expr(true, 0)
			expect(")")
			if c ~= 0 then
				stmt()
				if peek() and peek()[2] == "else" then take(); skip_stmt() end
			else
				skip_stmt()
				if peek() and peek()[2] == "else" then take(); stmt() end
			end
			return
		end
		if t[1] == "id" and t[2]:sub(1, 2) == ".@" then
			take(); expect("=")
			env.locals[t[2]] = expr(true, 0)
			expect(";")
			return
		end
		local cmd = t[2]
		local nargs = ({ bonus = 1, bonus2 = 2, bonus3 = 3, bonus4 = 4, bonus5 = 5 })[cmd]
		if nargs == nil then error("script: unsupported command " .. tostring(cmd)) end
		take()
		local rec = { nargs }
		repeat
			rec[#rec + 1] = expr(true, 0)
			local more = peek() and peek()[2] == ","
			if more then take() end
		until not more
		expect(";")
		if #rec ~= nargs + 2 then error("script: " .. cmd .. " takes " .. (nargs + 1) .. " arguments") end
		env.emit(rec)
	end
	while peek() do stmt() end
end
M.exec = exec

function M.run(ctx, names, active, K)
	local order = {}
	for _, name in ipairs(names) do order[#order + 1] = name end
	table.sort(order, function(a, b) return K.SC[a] < K.SC[b] end)
	local out = {}
	local named = {}
	for name, raw in pairs(active) do named[name] = raw end
	for _, name in ipairs(order) do
		local d = define.get(name)
		local script = d and d.record and d.record.script
		if script then
			exec(script, { active = named, locals = {}, K = K, idiv = ctx.idiv, emit = function(r) out[#out + 1] = r end })
		end
	end
	return out
end

return M
