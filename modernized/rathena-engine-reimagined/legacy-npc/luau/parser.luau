--!nonstrict
--!optimize 2
-- Step 2a: tokenizer + recursive-descent parser for the rAthena script dialect -> AST.
-- AST nodes are small positional arrays ({kind, ...}); see K table. All identifiers are lower-cased
-- (rAthena's symbol table is case-insensitive).
local find, sub, byte, lower, char, gsub, tonumber = string.find, string.sub, string.byte, string.lower, string.char, string.gsub, tonumber

local M = {}
M.quirks = {}
M.lenient = {}

-- expression kinds
local E_NUM, E_STR, E_VAR, E_CALL, E_BIN, E_UN, E_TERN, E_ASSIGN, E_INCDEC, E_NEXTLINE = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
-- statement kinds
local S_EXPR, S_IF, S_WHILE, S_DO, S_FOR, S_SWITCH, S_BLOCK, S_LABEL, S_CASE, S_DEFAULT, S_BREAK, S_CONTINUE, S_FUNCDEF, S_FUNCDECL =
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
M.K = { E_NUM = E_NUM, E_STR = E_STR, E_VAR = E_VAR, E_CALL = E_CALL, E_BIN = E_BIN, E_UN = E_UN, E_TERN = E_TERN, E_ASSIGN = E_ASSIGN,
	E_INCDEC = E_INCDEC, E_NEXTLINE = E_NEXTLINE, S_EXPR = S_EXPR, S_IF = S_IF, S_WHILE = S_WHILE, S_DO = S_DO, S_FOR = S_FOR,
	S_SWITCH = S_SWITCH, S_BLOCK = S_BLOCK, S_LABEL = S_LABEL, S_CASE = S_CASE, S_DEFAULT = S_DEFAULT, S_BREAK = S_BREAK,
	S_CONTINUE = S_CONTINUE, S_FUNCDEF = S_FUNCDEF, S_FUNCDECL = S_FUNCDECL }

-- token types
local T_NUM, T_STR, T_WORD, T_PUNCT, T_EOF = 1, 2, 3, 4, 5

-------------------------------------------------------------------------------------------------
-- Lexer
-------------------------------------------------------------------------------------------------
local P3 = { ["<<="] = true, [">>="] = true }
local P2 = { ["++"] = true, ["--"] = true, ["+="] = true, ["-="] = true, ["*="] = true, ["/="] = true, ["%="] = true,
	["&="] = true, ["|="] = true, ["^="] = true, ["~="] = true, ["<<"] = true, [">>"] = true, ["<="] = true,
	[">="] = true, ["=="] = true, ["!="] = true, ["&&"] = true, ["||"] = true }

local ESC = { n = "\n", t = "\t", r = "\r", ["0"] = "\0", a = "\a", b = "\b", f = "\f", v = "\v" }
local T: { number }, V: { any }, L: { number } = {}, {}, {}
local NT = 0

-- returns nil on success or an error table
local function lex(src: string): any
	local n = #src
	local i = 1
	local line = 1
	local nt = 0
	local tt, tv, tl = T, V, L
	while true do
		-- whitespace / comments
		while true do
			local c = byte(src, i)
			if c == 32 or c == 9 or c == 13 then
				local _, e = find(src, "^[ \t\r]+", i)
				i = (e :: number) + 1
			elseif c == 10 then
				line += 1; i += 1
			elseif c == 47 then
				local d = byte(src, i + 1)
				if d == 47 then
					local nl = find(src, "\n", i, true)
					i = nl and nl or n + 1
				elseif d == 42 then
					local _, e = find(src, "*/", i + 2, true)
					local stop = e and e + 1 or n + 1
					local k = i
					while true do
						local nl = find(src, "\n", k, true)
						if not nl or nl >= stop then break end
						line += 1; k = nl + 1
					end
					i = stop
				else
					break
				end
			else
				break
			end
		end
		if i > n then break end
		local c = byte(src, i)
		nt += 1
		tl[nt] = line
		if c == 34 then -- string
			local j = i + 1
			local hasEsc = false
			while true do
				local k = find(src, '["\\\n]', j) :: any
				if not k then return { cause = "lex_unterminated_string", line = line } end
				local d = byte(src, k)
				if d == 34 then
					local s = sub(src, i + 1, k - 1)
					if hasEsc then
						s = gsub(s, "\\(.)", function(ch) return ESC[ch] or ch end)
					end
					tt[nt] = T_STR; tv[nt] = s; i = k + 1
					break
				elseif d == 92 then
					hasEsc = true; j = k + 2
				else
					return { cause = "lex_newline_in_string", line = line }
				end
			end
		else
			-- word with optional sigil prefix?
			local pre = 0
			if c == 46 or c == 36 then -- . $
				local d = byte(src, i + 1)
				pre = (d == 64) and 2 or 1
			elseif c == 64 or c == 39 then -- @ '
				pre = 1
			elseif c == 35 then -- #
				pre = (byte(src, i + 1) == 35) and 2 or 1
			end
			local _, e = find(src, "^[%w_]*", i + pre)
			e = e :: number
			local isword = (pre > 0 and e >= i + pre) or (pre == 0 and e >= i and (c >= 48 and c <= 57 or c >= 65 and c <= 90 or c == 95 or c >= 97 and c <= 122))
			if isword then
				if pre == 0 and c >= 48 and c <= 57 then
					-- number or digit-leading word
					local w = sub(src, i, e)
					local num
					if find(w, "^%d+$") then num = tonumber(w)
					elseif find(w, "^0[xX]%x+$") then num = tonumber(w, 16) end
					if num then
						tt[nt] = T_NUM; tv[nt] = num; i = e + 1
					else
						tt[nt] = T_WORD; tv[nt] = lower(w); i = e + 1
					end
				else
					if byte(src, e + 1) == 36 then e += 1 end
					tt[nt] = T_WORD; tv[nt] = lower(sub(src, i, e)); i = e + 1
				end
			else
				local s3 = sub(src, i, i + 2)
				if P3[s3] then
					tt[nt] = T_PUNCT; tv[nt] = s3; i += 3
				else
					local s2 = sub(s3, 1, 2)
					if P2[s2] then
						tt[nt] = T_PUNCT; tv[nt] = s2; i += 2
					else
						local s1 = sub(s3, 1, 1)
						if find(s1, "^[%(%)%[%]{};,%?:%+%-%*/%%&|%^!~=<>]$") then
							tt[nt] = T_PUNCT; tv[nt] = s1; i += 1
						else
							return { cause = "lex_bad_char", line = line, detail = s1 }
						end
					end
				end
			end
		end
	end
	nt += 1
	tt[nt] = T_EOF; tv[nt] = "<eof>"; tl[nt] = line
	NT = nt
	return nil
end
M.lex = lex

-------------------------------------------------------------------------------------------------
-- Parser
-------------------------------------------------------------------------------------------------
local ti = 1
local BUILTINS: { [string]: string } = {}
local USERFUNCS: { [string]: boolean } = {}
local DECLFUNCS: { [string]: boolean } = {}

local function perr(cause: string, detail: any?): never
	error({ cause = cause, detail = detail, line = L[ti] }, 0)
end

local function isp(s: string): boolean
	return T[ti] == T_PUNCT and V[ti] == s
end
local function expectp(s: string)
	if T[ti] == T_PUNCT and V[ti] == s then ti += 1 else perr("expected_" .. s, V[ti]) end
end

local BINPREC = { ["||"] = 1, ["&&"] = 2, ["|"] = 3, ["^"] = 4, ["&"] = 5, ["=="] = 6, ["!="] = 6, ["<"] = 7, ["<="] = 7,
	[">"] = 7, [">="] = 7, ["<<"] = 8, [">>"] = 8, ["+"] = 9, ["-"] = 9, ["*"] = 10, ["/"] = 10, ["%"] = 10 }
local ASSIGNOPS = { ["="] = true, ["+="] = true, ["-="] = true, ["*="] = true, ["/="] = true, ["%="] = true, ["&="] = true,
	["|="] = true, ["^="] = true, ["~="] = true, ["<<="] = true, [">>="] = true }

local subexpr: (limit: number) -> any

local function isSigil(c: number): boolean
	return c == 46 or c == 36 or c == 64 or c == 35 or c == 39
end

-- split a word token into (scope, name)
local function splitvar(w: string): (string, string)
	local c = byte(w, 1)
	if c == 46 then
		if byte(w, 2) == 64 then return ".@", sub(w, 3) end
		return ".", sub(w, 2)
	elseif c == 36 then
		if byte(w, 2) == 64 then return "$@", sub(w, 3) end
		return "$", sub(w, 2)
	elseif c == 64 then return "@", sub(w, 2)
	elseif c == 35 then
		if byte(w, 2) == 35 then return "##", sub(w, 3) end
		return "#", sub(w, 2)
	elseif c == 39 then return "'", sub(w, 2) end
	return "", w
end

local function isFunc(w: string): boolean
	if isSigil(byte(w, 1) :: number) then return false end
	return BUILTINS[w] ~= nil or USERFUNCS[w] == true or DECLFUNCS[w] == true
end

local function arglist(closer: string?): { any }
	local args = {}
	local n = 0
	repeat
		n += 1
		args[n] = subexpr(-1)
		local more = isp(",")
		if more then ti += 1 end
	until not more
	return args
end

-- arity check against the BUILDIN_DEF argument-spec string (rA reports both too few and too many as errors)
local function checkArity(name: string, nargs: number)
	local spec = BUILTINS[name]
	if not spec then return end
	local req = #spec
	local unlimited = false
	local q = find(spec, "[%?%*]")
	if q then req = q - 1 end
	if find(spec, "*", 1, true) then unlimited = true end
	if nargs < req then perr("too_few_args", name .. "/" .. nargs .. " spec=" .. spec) end
	if not unlimited and nargs > #spec then perr("too_many_args", name .. "/" .. nargs .. " spec=" .. spec) end
end

-- function call in expression position (require_paren)
local function callExpr(name: string): any
	ti += 1
	local line = L[ti - 1]
	if isp("(") then
		ti += 1
		local args
		if isp(")") then args = {} else args = arglist() end
		expectp(")")
		checkArity(name, #args)
		return { E_CALL, name, args, line }
	end
	-- lenient: no parens -> zero-arg call (real parser requires '(' unless followed by ';')
	if not isp(";") then M.lenient[name] = (M.lenient[name] or 0) + 1 end
	checkArity(name, 0)
	return { E_CALL, name, {}, line, true }
end

-- variable reference (with optional array index)
local function varRef(): any
	local w = V[ti] :: string
	ti += 1
	local scope, name = splitvar(w)
	local idx
	if isp("[") then
		ti += 1
		idx = subexpr(-1)
		expectp("]")
	end
	return { E_VAR, scope, name, idx }
end

local function primary(): any
	local t = T[ti]
	if t == T_NUM then
		local v = V[ti]; ti += 1
		return { E_NUM, v }
	elseif t == T_STR then
		local v = V[ti]; ti += 1
		return { E_STR, v }
	elseif t == T_PUNCT then
		local p = V[ti]
		if p == "(" then
			ti += 1
			local e = subexpr(-1)
			expectp(")")
			return e
		elseif p == "+" and T[ti + 1] == T_NUM then
			ti += 1
			local v = V[ti]; ti += 1
			return { E_NUM, v }
		elseif p == "++" or p == "--" then
			ti += 1
			if T[ti] ~= T_WORD then perr("expected_variable", V[ti]) end
			local v = varRef()
			return { E_INCDEC, p == "++" and 1 or -1, false, v }
		end
		perr("unexpected_token", p)
	elseif t == T_WORD then
		local w = V[ti] :: string
		if isFunc(w) then return callExpr(w) end
		local v = varRef()
		-- assignment / post inc-dec (parse_variable)
		if T[ti] == T_PUNCT then
			local p = V[ti] :: string
			if ASSIGNOPS[p] then
				ti += 1
				local rhs = subexpr(-1)
				return { E_ASSIGN, p, v, rhs }
			elseif p == "++" or p == "--" then
				ti += 1
				return { E_INCDEC, p == "++" and 1 or -1, true, v }
			end
		end
		return v
	end
	perr("unexpected_token", V[ti])
end

function subexpr(limit: number): any
	local lhs
	local t, p = T[ti], V[ti]
	if t == T_PUNCT and p == "-" and T[ti + 1] == T_PUNCT and (V[ti + 1] == "," or V[ti + 1] == ";") then
		ti += 1
		return { E_NEXTLINE } -- the '-' pseudo label meaning "next line"
	end
	if t == T_PUNCT and (p == "-" or p == "!" or p == "~") then
		ti += 1
		local operand = subexpr(11)
		lhs = { E_UN, p, operand }
	else
		lhs = primary()
	end
	while T[ti] == T_PUNCT do
		local op = V[ti] :: string
		if op == "?" then
			if limit >= 0 then break end
			ti += 1
			local a = subexpr(-1)
			expectp(":")
			local b = subexpr(-1)
			lhs = { E_TERN, lhs, a, b }
		else
			local prec = BINPREC[op]
			if not prec or prec <= limit then break end
			ti += 1
			local rhs = subexpr(prec)
			lhs = { E_BIN, op, lhs, rhs }
		end
	end
	return lhs
end

-- is the token stream at an assignment / inc-dec statement? (lookahead past optional [..])
local function looksLikeAssign(): boolean
	local j = ti + 1
	if T[j] == T_PUNCT and V[j] == "[" then
		local depth = 0
		while T[j] ~= T_EOF do
			if T[j] == T_PUNCT then
				if V[j] == "[" then depth += 1
				elseif V[j] == "]" then depth -= 1; if depth == 0 then j += 1; break end end
			end
			j += 1
		end
	end
	if T[j] ~= T_PUNCT then return false end
	local p = V[j] :: string
	return ASSIGNOPS[p] == true or p == "++" or p == "--"
end

local statement: (forEnd: boolean?, swOK: boolean?) -> any

-- statement-level command call (parse_callfunc with require_paren=0)
local function commandStmt(forEnd: boolean?): any
	local name = V[ti] :: string
	local line = L[ti]
	ti += 1
	local term = forEnd and ")" or ";"
	local args
	if isp(term) then
		args = {}
	elseif isp("(") then
		local save = ti
		local ok, res = pcall(function()
			ti += 1
			local a
			if isp(")") then a = {} else a = arglist() end
			expectp(")")
			if not isp(term) then perr("paren_form_mismatch") end
			return a
		end)
		if ok then args = res else ti = save; args = arglist() end
	else
		args = arglist()
	end
	checkArity(name, #args)
	return { E_CALL, name, args, line }
end

local function simpleStmt(forEnd: boolean?): any
	-- returns an expression node (assignment / incdec / command call)
	local t, w = T[ti], V[ti]
	if t == T_PUNCT and (w == "++" or w == "--") then
		return subexpr(-1)
	elseif t == T_WORD then
		if isFunc(w :: string) then
			return commandStmt(forEnd)
		elseif looksLikeAssign() then
			return subexpr(-1)
		end
		if isSigil(byte(w :: string, 1) :: number) then perr("bad_statement", w) end
		perr("unknown_command", w)
	end
	perr("unexpected_token", w)
end

local function blockBody(swOK: boolean): { any }
	local list = {}
	local n = 0
	while not isp("}") do
		if T[ti] == T_EOF then perr("unexpected_eof_in_block") end
		local s = statement(false, swOK)
		if s then n += 1; list[n] = s end
	end
	ti += 1
	return list
end

function statement(forEnd: boolean?, swOK: boolean?): any
	local t, w = T[ti], V[ti]
	if t == T_PUNCT then
		if w == ";" then ti += 1; return nil end
		if w == "{" then
			ti += 1
			return { S_BLOCK, blockBody(false) }
		end
		if w == ")" and forEnd then return nil end
	elseif t == T_WORD then
		local line = L[ti]
		-- label?
		if T[ti + 1] == T_PUNCT and V[ti + 1] == ":" and w ~= "default" then
			ti += 2
			return { S_LABEL, w, line }
		end
		if not isSigil(byte(w :: string, 1) :: number) then
			if w == "if" then
				ti += 1
				if not isp("(") then perr("expected_(", V[ti]) end
				local c = subexpr(-1) -- rA: parse_expr from '(' (so `if (a) && (b)` is valid)
				local th = statement(false, false)
				local el
				if T[ti] == T_WORD and V[ti] == "else" then
					ti += 1
					el = statement(false, false)
				end
				return { S_IF, c, th, el, line }
			elseif w == "while" then
				ti += 1
				if not isp("(") then perr("expected_(", V[ti]) end
				local c = subexpr(-1)
				return { S_WHILE, c, statement(false, false), line }
			elseif w == "do" then
				ti += 1
				local body = statement(false, false)
				if not (T[ti] == T_WORD and V[ti] == "while") then perr("expected_while", V[ti]) end
				ti += 1
				if not isp("(") then perr("expected_(", V[ti]) end
				local c = subexpr(-1)
				if isp(";") then ti += 1 end
				return { S_DO, body, c, line }
			elseif w == "for" then
				ti += 1
				expectp("(")
				local init, cond, step
				if isp(";") then ti += 1 else
					init = simpleStmt(false)
					if isp(",") and (init[1] == E_ASSIGN or init[1] == E_INCDEC) then
						M.quirks[","] = (M.quirks[","] or 0) + 1; ti += 1 -- rA quirk, see below
					else expectp(";") end
				end
				if isp(";") then ti += 1 else
					cond = subexpr(-1); expectp(";")
				end
				if not isp(")") then step = simpleStmt(true) end
				expectp(")")
				return { S_FOR, init, cond, step, statement(false, false), line }
			elseif w == "switch" then
				ti += 1
				if not isp("(") then perr("expected_(", V[ti]) end
				local e = subexpr(-1)
				expectp("{")
				return { S_SWITCH, e, blockBody(true), line }
			elseif w == "case" then
				if not swOK then perr("unexpected_case") end
				ti += 1
				local key
				if T[ti] == T_NUM then key = V[ti]; ti += 1
				elseif isp("-") and T[ti + 1] == T_NUM then key = -(V[ti + 1] :: number); ti += 2
				elseif isp("+") and T[ti + 1] == T_NUM then key = V[ti + 1]; ti += 2
				elseif T[ti] == T_WORD and not isSigil(byte(V[ti] :: string, 1) :: number) then key = V[ti]; ti += 1
				else perr("bad_case_label", V[ti]) end
				expectp(":")
				return { S_CASE, key, line }
			elseif w == "default" then
				if not swOK then perr("unexpected_default") end
				ti += 1
				expectp(":")
				return { S_DEFAULT }
			elseif w == "break" then
				ti += 1; expectp(";")
				return { S_BREAK }
			elseif w == "continue" then
				ti += 1; expectp(";")
				return { S_CONTINUE }
			elseif w == "function" then
				ti += 1
				if T[ti] ~= T_WORD then perr("function_name_missing") end
				local name = V[ti] :: string
				ti += 1
				if isp(";") then
					ti += 1
					DECLFUNCS[name] = true
					return { S_FUNCDECL, name }
				elseif isp("{") then
					ti += 1
					DECLFUNCS[name] = true
					return { S_FUNCDEF, name, blockBody(false) }
				end
				perr("expected_;_or_{_at_function", V[ti])
			end
		end
	end
	-- expression statement
	local line = L[ti]
	local isAssign = T[ti] == T_PUNCT or (T[ti] == T_WORD and not isFunc(V[ti]))
	local e = simpleStmt(forEnd)
	if forEnd then
		if not isp(")") then perr("expected_)", V[ti]) end
	elseif isp(";") then
		ti += 1
	elseif isAssign and T[ti] == T_PUNCT and (V[ti] == "," or V[ti] == ")" or V[ti] == ":") then
		-- rAthena quirk: after an assignment statement parse_line does parse_syntax_close(p2 + 1),
		-- i.e. it skips ANY one character as if it were ';'
		M.quirks[V[ti]] = (M.quirks[V[ti]] or 0) + 1
		ti += 1
	else
		perr("expected_;", V[ti])
	end
	return { S_EXPR, e, line }
end

-- parse(body, opts) -> ast (array of statements) | nil, err
function M.parse(body: string, builtins: { [string]: string }, userfuncs: { [string]: boolean }?): ({ any }?, any?)
	local lerr = lex(body)
	if lerr then return nil, lerr end
	BUILTINS = builtins
	USERFUNCS = userfuncs or {}
	DECLFUNCS = {}
	ti = 1
	local list = {}
	local n = 0
	local ok, err = pcall(function()
		while T[ti] ~= T_EOF do
			local s = statement(false, false)
			if s then n += 1; list[n] = s end
		end
	end)
	local ntok = NT
	if not ok then
		if type(err) ~= "table" then return nil, { cause = "internal_error", detail = tostring(err), line = L[ti] } end
		return nil, err
	end
	return list, { ntok = ntok, funcs = DECLFUNCS }
end

return M
