--!nonstrict
-- Binary cache of lowered programs (architecture section 7: "parse once, cache as binary, invalidate by file hash"). The flat ops are plain data
-- (numbers, strings, booleans, nested positional arrays with holes, a few maps), so one generic serializer covers them. A blob carries the
-- format version and the hash of the source it was built from; decode() answers nil for a different hash, a different version or a damaged blob,
-- and the caller parses the source again.
--   cache.hash(text)            32-bit FNV-1a of the body text
--   cache.encode(prog, hash)    -> blob
--   cache.decode(blob, hash)    -> prog | nil
-- The loader (loader.luau, opts.cache = { get = fn(key) -> blob?, put = fn(key, blob) }) uses it per NPC; keys are "<path>:<line>:<name>".
local M = {}

local VERSION = 1 -- bump together with any change of the op layout (parser.luau, lower.luau)
local pack, unpack, sub, byte = string.pack, string.unpack, string.sub, string.byte
local floor = math.floor
local bxor = bit32.bxor

function M.hash(text: string): number
	local h = 2166136261
	for i = 1, #text do
		h = bxor(h, byte(text, i))
		-- h * 16777619 mod 2^32 exactly (a plain product of two 32-bit numbers exceeds 2^53): multiply the halves of h separately
		local lo, hi = h % 65536, floor(h / 65536)
		h = (lo * 16777619 + (hi * 16777619 % 65536) * 65536) % 4294967296
	end
	return h
end

local function enc(v, out: { string }, depth: number)
	local t = type(v)
	if t == "number" then out[#out + 1] = "\3" .. pack("<d", v)
	elseif t == "string" then out[#out + 1] = "\4" .. pack("<I4", #v) .. v
	elseif t == "boolean" then out[#out + 1] = v and "\2" or "\1"
	elseif t == "nil" then out[#out + 1] = "\0"
	elseif t == "table" then
		if depth > 400 then error("cache: structure too deep") end
		local n = 0
		for k in v do if type(k) == "number" and k >= 1 and k == floor(k) and k > n then n = k end end
		local other = {}
		for k in v do
			if not (type(k) == "number" and k >= 1 and k == floor(k) and k <= n) then other[#other + 1] = k end
		end
		out[#out + 1] = "\5" .. pack("<I4I4", n, #other)
		for i = 1, n do enc(v[i], out, depth + 1) end
		for _, k in other do
			enc(k, out, depth + 1)
			enc(v[k], out, depth + 1)
		end
	else
		error("cache: cannot store a " .. t)
	end
end

function M.encode(prog, hash: number): string
	local out = { "LNOP", pack("<I2I4", VERSION, hash) }
	enc({ code = prog.code, labels = prog.labels, name = prog.name, file = prog.file, baseLine = prog.baseLine }, out, 0)
	return table.concat(out)
end

local function dec(blob: string, pos: number, depth: number)
	local tag = byte(blob, pos)
	pos += 1
	if tag == 0 then return nil, pos
	elseif tag == 1 then return false, pos
	elseif tag == 2 then return true, pos
	elseif tag == 3 then
		local v, np = unpack("<d", blob, pos)
		return v, np
	elseif tag == 4 then
		local len, np = unpack("<I4", blob, pos)
		if np + len - 1 > #blob then error("cache: truncated") end
		return sub(blob, np, np + len - 1), np + len
	elseif tag == 5 then
		if depth > 400 then error("cache: structure too deep") end
		local n, nother, np = unpack("<I4I4", blob, pos)
		if n > #blob or nother > #blob then error("cache: bad count") end
		local t = table.create(n)
		pos = np
		for i = 1, n do
			local v
			v, pos = dec(blob, pos, depth + 1)
			t[i] = v
		end
		for _ = 1, nother do
			local k, v
			k, pos = dec(blob, pos, depth + 1)
			v, pos = dec(blob, pos, depth + 1)
			t[k] = v
		end
		return t, pos
	end
	error("cache: bad tag")
end

function M.decode(blob: string, hash: number)
	if type(blob) ~= "string" or sub(blob, 1, 4) ~= "LNOP" or #blob < 11 then return nil end
	local ok, res = pcall(function()
		local version, h, pos = unpack("<I2I4", blob, 5)
		if version ~= VERSION or h ~= hash then return nil end
		local t = dec(blob, pos, 0)
		if type(t) ~= "table" or type(t.code) ~= "table" or type(t.labels) ~= "table" then return nil end
		return t
	end)
	if ok then return res end
	return nil
end

return M
