--!nonstrict
-- Step 1: declaration splitter for rAthena NPC source files (mirrors npc_parsesrcfile / npc_skip_script).
local find, sub, byte, lower = string.find, string.sub, string.byte, string.lower

local M = {}

-- skip whitespace and // and /* */ comments (script.cpp skip_space)
local function skip_space(text: string, p: number): number
	local n = #text
	while p <= n do
		local s, e = find(text, "^%s+", p)
		if s then p = e + 1 end
		local c = byte(text, p)
		if c == 47 then
			local d = byte(text, p + 1)
			if d == 47 then
				local nl = find(text, "\n", p, true)
				p = nl and nl or n + 1
			elseif d == 42 then
				local _, e2 = find(text, "*/", p + 2, true)
				p = e2 and e2 + 1 or n + 1
			else
				break
			end
		else
			break
		end
	end
	return p
end
M.skip_space = skip_space

-- npc_skip_script: p points at the opening '{'. Returns index of the matching '}' or nil, err.
local function skip_curly(text: string, p: number): (number?, string?)
	local n = #text
	local depth = 1
	local i = p + 1
	while true do
		i = find(text, '[{}"/]', i) :: any
		if not i then return nil, "missing right curly (EOF)" end
		local c = byte(text, i)
		if c == 123 then depth += 1; i += 1
		elseif c == 125 then
			depth -= 1
			if depth == 0 then return i, nil end
			i += 1
		elseif c == 34 then
			i += 1
			while true do
				local j = find(text, '["\\\n]', i) :: any
				if not j then return nil, "unexpected end of string" end
				local d = byte(text, j)
				if d == 34 then i = j + 1; break
				elseif d == 92 then i = j + 2
				else return nil, "unexpected newline in string" end
			end
		else -- '/'
			local d = byte(text, i + 1)
			if d == 47 then
				local nl = find(text, "\n", i, true)
				i = nl and nl or n + 1
			elseif d == 42 then
				local _, e2 = find(text, "*/", i + 2, true)
				i = e2 and e2 + 1 or n + 1
			else
				i += 1
			end
		end
	end
end

function M.lineof(text: string, pos: number): number
	local line, i = 1, 1
	while true do
		local nl = find(text, "\n", i, true)
		if not nl or nl >= pos then return line end
		line += 1; i = nl + 1
	end
end

local SHOPS = { shop = true, cashshop = true, itemshop = true, pointshop = true, marketshop = true }

-- returns { decls = {...}, counts = {kind=n}, errors = {{msg,pos}} }
function M.split(text: string)
	local decls = {}
	local counts: { [string]: number } = {}
	local errors = {}
	local n = #text
	local p = skip_space(text, 1)
	local function bump(k: string) counts[k] = (counts[k] or 0) + 1 end
	while p <= n do
		local nl = find(text, "\n", p, true)
		local lineEnd = nl and nl - 1 or n
		-- fields: w1 \t w2 \t w3 \t rest
		local t1 = find(text, "\t", p, true)
		local w1, w2, w3, w4, count
		if not t1 or t1 > lineEnd then
			w1 = sub(text, p, lineEnd); count = 1
		else
			w1 = sub(text, p, t1 - 1)
			local t2 = find(text, "\t", t1 + 1, true)
			if not t2 or t2 > lineEnd then
				w2 = sub(text, t1 + 1, lineEnd); count = 2
			else
				w2 = sub(text, t1 + 1, t2 - 1)
				local t3 = find(text, "\t", t2 + 1, true)
				if not t3 or t3 > lineEnd then
					w3 = sub(text, t2 + 1, lineEnd); count = 3; w4 = ""
				else
					w3 = sub(text, t2 + 1, t3 - 1)
					w4 = sub(text, t3 + 1, lineEnd); count = 4
				end
			end
		end
		if count < 3 then
			bump("error_unknown_syntax")
			errors[#errors + 1] = { "unknown syntax (<3 fields)", p }
			break -- npc_parsesrcfile stops the file
		end
		local l2 = lower(w2 :: string)
		local startp = p
		local nextp: number
		local isScript = count > 3 and sub(l2, 1, 6) == "script"
		if (sub(l2, 1, 4) == "warp") and count > 3 then
			bump("warp"); nextp = lineEnd + 1
		elseif SHOPS[l2] and count > 3 then
			bump("shop"); nextp = lineEnd + 1
			bump("shop:" .. l2)
		elseif isScript then
			local isFunc = lower(w1) == "function"
			local braceAt
			if isFunc then
				if l2 ~= "script" then
					bump("error_function_not_script"); nextp = lineEnd + 1
				else
					local s = find(text, "\t{", startp, true)
					if sub(w4 :: string, 1, 1) ~= "{" or not s or s > lineEnd then
						bump("error_missing_curly"); errors[#errors + 1] = { "missing left curly '\\t{'", startp }; break
					end
					braceAt = s + 1
				end
			else
				local s = find(text, ",{", startp, true)
				if not find(w4 :: string, ",{", 1, true) or not s or s > lineEnd then
					bump("error_missing_curly"); errors[#errors + 1] = { "missing left curly ',{'", startp }; break
				end
				braceAt = s + 1
			end
			if braceAt then
				local close, err = skip_curly(text, braceAt)
				if not close then
					bump("error_skip_script"); errors[#errors + 1] = { err, startp }; break
				end
				decls[#decls + 1] = {
					kind = isFunc and "function" or "script",
					w1 = w1, name = w3, pos = braceAt, endpos = close,
					body = sub(text, braceAt + 1, close - 1),
					head = isFunc and "" or (w4 :: string),
				}
				bump(isFunc and "function" or "script")
				nextp = close + 1
			end
		elseif sub(l2, 1, 9) == "duplicate" and sub(l2, 10, 10) == "(" and count > 3 then
			bump("duplicate")
			decls[#decls + 1] = { kind = "duplicate", w1 = w1, name = w3, template = sub(w2 :: string, 11, (find(w2 :: string, ")", 1, true) or 12) - 1), pos = startp }
			nextp = lineEnd + 1
		elseif (l2 == "monster" or l2 == "boss_monster") and count > 3 then
			bump("monster"); nextp = lineEnd + 1
		elseif l2 == "mapflag" and count >= 3 then
			bump("mapflag"); nextp = lineEnd + 1
		else
			bump("error_unparsed_line"); errors[#errors + 1] = { "unable to parse line (" .. l2 .. ")", startp }
			nextp = lineEnd + 1
		end
		p = skip_space(text, nextp)
	end
	return { decls = decls, counts = counts, errors = errors }
end

return M
