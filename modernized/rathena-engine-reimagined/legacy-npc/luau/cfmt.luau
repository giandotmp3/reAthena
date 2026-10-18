--!nonstrict
-- C-library formatting helpers for the legacy `sprintf` and `sscanf` script commands (script.cpp BUILDIN_FUNC(sprintf)/(sscanf)):
-- the legacy VM hands every `%...` conversion of the format to libc with exactly one argument; this module reproduces that for the
-- conversions scripts use (d i u o x X c s e E f F g G, flags - + space # 0, width, .precision, l/ll/h modifiers; %% and the
-- skipped %n). Pure Luau, no VM state.
local M = {}

local INTCONV = { d = true, i = true, u = true, o = true, x = true, X = true, c = true }
local FLTCONV = { e = true, E = true, f = true, F = true, g = true, G = true }

-- sprintf: returns the formatted string, or nil + reason when the legacy command fails (not enough arguments, '$' or '*' in a conversion)
function M.sprintf(fmt: string, args: { any }): (string?, string?)
	local out, i, n, ai = {}, 1, #fmt, 0
	while i <= n do
		local p = string.find(fmt, "%", i, true)
		if not p then out[#out + 1] = string.sub(fmt, i); break end
		if p > i then out[#out + 1] = string.sub(fmt, i, p - 1) end
		local c = string.sub(fmt, p + 1, p + 1)
		if c == "%" then out[#out + 1] = "%"; i = p + 2
		elseif c == "n" then i = p + 2 -- warns and skips
		else
			-- conversion spec: flags width .prec length conv
			local j = p + 1
			while j <= n and not string.find("diouxXeEfFgGaAcsp", string.sub(fmt, j, j), 1, true) do
				local ch = string.sub(fmt, j, j)
				if ch == "$" or ch == "*" then return nil, "unsupported '" .. ch .. "' in a conversion" end
				j += 1
			end
			ai += 1
			if ai > #args then return nil, "not enough arguments" end
			local conv = string.sub(fmt, j, j)
			local spec = string.sub(fmt, p + 1, j - 1)
			-- the legacy code cuts the piece at the NEXT '%' (so trailing literal text belongs to this conversion's printf)
			local nxt = string.find(fmt, "%", j + 1, true)
			local tail = string.sub(fmt, j + 1, (nxt or n + 1) - 1)
			local v = args[ai]
			local flags = string.match(spec, "^[-+ #0]*") or ""
			local rest = string.sub(spec, #flags + 1)
			local width = string.match(rest, "^%d+") or ""
			rest = string.sub(rest, #width + 1)
			local prec = string.match(rest, "^%.%d*") or ""
			local lenmod = string.sub(rest, #prec + 1) -- h hh l ll z j t L: width of the C type, irrelevant for doubles-as-int64
			local s
			if conv == "s" then
				s = string.format("%" .. flags .. width .. prec .. "s", type(v) == "string" and v or tostring(v))
			elseif INTCONV[conv] then
				local num = type(v) == "number" and v or (tonumber(v) or 0)
				num = num >= 0 and math.floor(num) or math.ceil(num)
				if conv == "c" then
					s = string.format("%" .. flags .. width .. "s", string.char(num % 256))
				elseif conv == "d" or conv == "i" then
					s = string.format("%" .. flags .. width .. prec .. "d", num)
				else
					if num < 0 then num = num + 2 ^ 32 end -- unsigned int view of a negative (default 32-bit conversions)
					s = string.format("%" .. flags .. width .. prec .. conv, num)
				end
			elseif FLTCONV[conv] then
				local num = type(v) == "number" and v or (tonumber(v) or 0)
				s = string.format("%" .. flags .. width .. prec .. conv, num)
			else
				s = tostring(v)
			end
			out[#out + 1] = s .. tail
			i = (nxt or n + 1)
		end
	end
	return table.concat(out)
end

-- sscanf: scan `str` with `fmt`. `kinds[k]` is "s" (string target) or "n" (numeric target) for the k-th assigning conversion.
-- Returns count, values (array of assigned values in order), ok. ok=false => the legacy command fails and returns -1.
-- Like the legacy command, every assigning conversion is scanned on its own over the WHOLE input with the already-used
-- conversions turned into %* (skipped), which is equivalent to one left-to-right scan; a failed conversion fails the command.
function M.sscanf(str: string, fmt: string, kinds: { string }): (number, { any }, boolean)
	local vals = {}
	if #fmt ~= 0 and #str == 0 then return -1, vals, true end
	local si, fi, nfmt, k = 1, 1, #fmt, 0
	local function skipws()
		while si <= #str and string.find(" \t\n\r\v\f", string.sub(str, si, si), 1, true) do si += 1 end
	end
	local failed = false
	while fi <= nfmt do
		local fc = string.sub(fmt, fi, fi)
		if fc == "%" then
			local nc = string.sub(fmt, fi + 1, fi + 1)
			if nc == "%" then
				skipws()
				if string.sub(str, si, si) ~= "%" then break end
				si += 1; fi += 2
			else
				local j = fi + 1
				local suppress = false
				if string.sub(fmt, j, j) == "*" then suppress = true; j += 1 end
				local wd = string.match(fmt, "^%d+", j); if wd then j += #wd end
				local width = wd and tonumber(wd) or math.huge
				while string.find("hlLqjzt", string.sub(fmt, j, j), 1, true) and j <= nfmt do j += 1 end
				local conv = string.sub(fmt, j, j)
				local setspec
				if conv == "[" then
					local e = j + 1
					if string.sub(fmt, e, e) == "^" then e += 1 end
					if string.sub(fmt, e, e) == "]" then e += 1 end
					while e <= nfmt and string.sub(fmt, e, e) ~= "]" do e += 1 end
					setspec = string.sub(fmt, j, e)
					j = e
				end
				fi = j + 1
				local value
				if conv == "n" then
					value = si - 1
				elseif conv == "c" then
					local w = width == math.huge and 1 or width
					if si + w - 1 > #str then break end
					value = string.sub(str, si, si + w - 1); si += w
				elseif conv == "s" then
					skipws()
					local e = si
					while e <= #str and e - si < width and not string.find(" \t\n\r\v\f", string.sub(str, e, e), 1, true) do e += 1 end
					if e == si then break end
					value = string.sub(str, si, e - 1); si = e
				elseif setspec then
					local neg = string.sub(setspec, 2, 2) == "^"
					local body = string.sub(setspec, neg and 3 or 2, #setspec - 1)
					local e = si
					while e <= #str and e - si < width do
						local ch = string.sub(str, e, e)
						local inset = false
						local b = 1
						while b <= #body do
							if string.sub(body, b + 1, b + 1) == "-" and b + 2 <= #body then
								if ch >= string.sub(body, b, b) and ch <= string.sub(body, b + 2, b + 2) then inset = true end
								b += 3
							else
								if ch == string.sub(body, b, b) then inset = true end
								b += 1
							end
						end
						if inset == neg then break end
						e += 1
					end
					if e == si then break end
					value = string.sub(str, si, e - 1); si = e
				elseif conv == "d" or conv == "i" or conv == "u" or conv == "x" or conv == "X" or conv == "o" then
					skipws()
					local base = (conv == "x" or conv == "X") and 16 or conv == "o" and 8 or 10
					local pat
					local rest = string.sub(str, si)
					local sign, digits
					if conv == "i" then
						sign, digits = string.match(rest, "^([+-]?)(0[xX]%x+)")
						if digits then base = 16; digits = string.sub(digits, 3) else
							sign, digits = string.match(rest, "^([+-]?)(0[0-7]*)")
							if digits then base = 8 else sign, digits = string.match(rest, "^([+-]?)(%d+)"); base = 10 end
						end
					elseif base == 16 then
						sign, digits = string.match(rest, "^([+-]?)0[xX](%x+)")
						if not digits then sign, digits = string.match(rest, "^([+-]?)(%x+)") end
					elseif base == 8 then sign, digits = string.match(rest, "^([+-]?)([0-7]+)")
					else sign, digits = string.match(rest, "^([+-]?)(%d+)") end
					if not digits then break end
					if #digits > width then digits = string.sub(digits, 1, width) end
					local consumed = #(string.match(rest, "^[+-]?") or "")
					local mm = string.match(rest, "^[+-]?0[xX]")
					if base == 16 and mm then consumed += 2 end
					si += consumed + #digits
					value = tonumber(digits, base) or 0
					if sign == "-" then value = -value end
				else
					break
				end
				if not suppress then
					k += 1
					if conv ~= "n" or true then vals[k] = value end
					local kind = kinds[k]
					if kind == "s" and type(value) ~= "string" then vals[k] = tostring(value) end
					if kind == "n" and type(value) == "string" then
						-- a string conversion into a numeric variable: the legacy code would write garbage; use the numeric prefix
						vals[k] = tonumber(value) or 0
					end
				end
			end
		elseif string.find(" \t\n\r\v\f", fc, 1, true) then
			skipws(); fi += 1
		else
			if string.sub(str, si, si) ~= fc then break end
			si += 1; fi += 1
		end
	end
	-- count = assigned conversions; the legacy command fails if one of its (kinds) targets was not assigned
	local assigned = #vals
	local ok = assigned >= #kinds
	return assigned, vals, ok
end

return M
