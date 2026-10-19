--!nonstrict
-- Scripted status effects (BR-RULESET-075, status.cpp:4213-4221): a status record may carry a `Script:` that the legacy code runs, for players, during
-- the stat recalculation of every active status that has one (Speedup0/1: `bonus bSpeedRate, getstatus(SC_SPEEDUP0, 1)`, Atkpotion: `bonus bBaseAtk, ...`).
-- This module compiles that script text once into a list of statements and evaluates it into a LIST OF BONUS DELTAS (BR-RULESET-112: "scripts return a
-- compiled, cacheable list of bonus deltas, not live memory writes"). Applying a delta to the stats is the item-bonus vocabulary (`bonus.*` of
-- rathena.base, BR-RULESET-098..110), which this package does not implement: the deltas are returned as { name = "bBaseAtk", args = { 150 } }.
--
-- Supported subset (all 168 renewal scripts use it): `.@var = expr;`  `bonus[2..5] name, arg...;`  `if (cond) { ... }`  and in expressions
-- getstatus(SC_X, n) (the n-th val of status X), getstatus(SC_X) (is X active), getskilllv("SKILL"), max / min, + - * / unary - and !.
local expr = import("scripts/status/expr")
local status = import("scripts/status/status")

local M = {}

local function strip_comments(src)
	return (src:gsub("/%*.-%*/", ""))
end

-- reads one balanced (...) group starting at the '(' at position i; returns its inner text and the position after it
local function paren_group(src, i)
	local depth, j = 0, i
	while j <= #src do
		local c = src:sub(j, j)
		if c == '"' then
			j = src:find('"', j + 1, true) or #src
		elseif c == "(" then depth += 1
		elseif c == ")" then
			depth -= 1
			if depth == 0 then return src:sub(i + 1, j - 1), j + 1 end
		end
		j += 1
	end
	error("bonus_scripts: unbalanced parenthesis in: " .. src, 0)
end

local function split_args(text)
	local out, depth, cur, inq = {}, 0, {}, false
	for k = 1, #text do
		local c = text:sub(k, k)
		if c == '"' then inq = not inq end
		if not inq and c == "(" then depth += 1 end
		if not inq and c == ")" then depth -= 1 end
		if not inq and depth == 0 and c == "," then
			out[#out + 1] = table.concat(cur)
			cur = {}
		else
			cur[#cur + 1] = c
		end
	end
	out[#out + 1] = table.concat(cur)
	for i, a in ipairs(out) do out[i] = (a:gsub("^%s+", ""):gsub("%s+$", "")) end
	return out
end

-- statements: { kind = "assign" | "bonus" | "if", ... }
local function parse(src)
	local pos, stmts = 1, {}
	local function skip_ws()
		pos = src:find("%S", pos) or #src + 1
	end
	local function parse_statement()
		skip_ws()
		if pos > #src then return nil end
		if src:sub(pos, pos + 1) == "if" and not src:sub(pos + 2, pos + 2):match("[%w_]") then
			pos = src:find("%(", pos)
			local cond, after = paren_group(src, pos)
			pos = after
			skip_ws()
			local body = {}
			if src:sub(pos, pos) == "{" then
				pos += 1
				while true do
					skip_ws()
					if src:sub(pos, pos) == "}" then pos += 1 break end
					local s = parse_statement()
					if not s then error("bonus_scripts: missing } in: " .. src, 0) end
					body[#body + 1] = s
				end
			else
				body[1] = parse_statement()
			end
			return { kind = "if", cond = cond, body = body }
		end
		local semi = nil
		local inq, k = false, pos
		while k <= #src do
			local c = src:sub(k, k)
			if c == '"' then inq = not inq end
			if not inq and c == ";" then semi = k break end
			k += 1
		end
		if not semi then error("bonus_scripts: missing ; in: " .. src:sub(pos), 0) end
		local text = src:sub(pos, semi - 1)
		pos = semi + 1
		local var, rhs = text:match("^%s*%.@([%w_]+)%s*=%s*(.-)%s*$")
		if var then return { kind = "assign", var = var, rhs = rhs } end
		local cmd, rest = text:match("^%s*(bonus%d?)%s+(.-)%s*$")
		if cmd then
			local args = split_args(rest)
			return { kind = "bonus", cmd = cmd, name = table.remove(args, 1), args = args }
		end
		error("bonus_scripts: unsupported statement: " .. text, 0)
	end
	while true do
		local s = parse_statement()
		if not s then break end
		stmts[#stmts + 1] = s
	end
	return stmts
end

-- getstatus(SC_X, n) -> u.val.<key>.v<n>; getstatus(SC_X) -> u.sc.<key>; getskilllv("NAME") -> u.skill.NAME; .@var -> var_<name>
local function rewrite(text)
	text = text:gsub("getstatus%s*%(%s*(SC_[%w_]+)%s*,%s*(%d)%s*%)", function(sc, n)
		local key = status.by_sc(sc)
		if not key then error("bonus_scripts: unknown status " .. sc, 0) end
		return "u.val." .. key .. ".v" .. n
	end)
	text = text:gsub("getstatus%s*%(%s*(SC_[%w_]+)%s*%)", function(sc)
		local key = status.by_sc(sc)
		if not key then error("bonus_scripts: unknown status " .. sc, 0) end
		return "u.sc." .. key
	end)
	text = text:gsub('getskilllv%s*%(%s*"([%w_]+)"%s*%)', "u.skill.%1")
	text = text:gsub("%.@([%w_]+)", "var_%1")
	return text
end

-- compiles the script text of a record into a function(vals, F) -> { { name, args } ... } (F = facts with F.sc / F.val)
function M.compile(src)
	local stmts = parse(strip_comments(src))
	local slots, nslots = {}, 4
	local function slot(var)
		if not slots[var] then nslots += 1; slots[var] = nslots end
		return slots[var]
	end
	local function comp(text)
		return expr.compile(rewrite(text), slots, {})
	end
	local function build(list)
		local out = {}
		for _, s in ipairs(list) do
			if s.kind == "assign" then
				out[#out + 1] = { kind = "assign", slot = slot("var_" .. s.var), fn = (function() local r = rewrite(s.rhs); return expr.compile(r, slots, {}) end)() }
			elseif s.kind == "if" then
				out[#out + 1] = { kind = "if", cond = comp(s.cond), body = build(s.body) }
			else
				local args = {}
				for i, a in ipairs(s.args) do
					local lit = a:match('^"(.*)"$')
					if lit then
						args[i] = { sym = lit }
					elseif a:match("^[%a_][%w_]*$") and not slots["var_" .. a] then
						args[i] = { sym = a } -- a constant of the item script language (Ele_Water, RC_Undead, Eff_Stun, ...)
					else
						args[i] = { fn = comp(a) }
					end
				end
				out[#out + 1] = { kind = "bonus", name = s.name, cmd = s.cmd, args = args }
			end
		end
		return out
	end
	local prog = build(stmts)
	return function(vals, F)
		local v = { vals[1] or 0, vals[2] or 0, vals[3] or 0, vals[4] or 0 }
		for i = 5, nslots do v[i] = 0 end
		local out = {}
		local function run(list)
			for _, s in ipairs(list) do
				if s.kind == "assign" then
					v[s.slot] = s.fn(v, F)
				elseif s.kind == "if" then
					if s.cond(v, F) ~= 0 then run(s.body) end
				else
					local args = {}
					for i, a in ipairs(s.args) do args[i] = a.sym ~= nil and a.sym or a.fn(v, F) end
					out[#out + 1] = { name = s.name, args = args }
				end
			end
		end
		run(prog)
		return out
	end
end

local cache = {}
-- the bonus list of the status `name` for its val1..val4 and the unit facts F (aggregate.facts_for); nil when the record has no script
function M.bonuses(name, vals, F)
	local rec = status.record(name)
	if not rec or not rec.script then return nil end
	local fn = cache[name]
	if not fn then fn = M.compile(rec.script); cache[name] = fn end
	return fn(vals, F)
end

return M
