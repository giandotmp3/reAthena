--!nonstrict
-- In-memory adapter with the same Luau surface as script-host's `storage` global (api/engine.d.luau):
--   storage.character(ch), storage.account(acc) (needs the storage.account permission), storage.world()
--   scope:get(key) / scope:set(key, value-or-nil) / scope:keys() / scope:mod(id)
-- Used where no script-host sandbox is present (Luau CLI tests, the replay harness, the memory benchmark) and as the fake in unit
-- tests. Values behave like the host's: deep-copied in both directions; nil erases; only nil, boolean, number, string and
-- nested tables (string or integer keys, depth <= 16) can be stored; a per-mod quota is optional.
local M = {}

local MAX_DEPTH = 16

local function copy(v, depth)
	local t = type(v)
	if t == "table" then
		if depth > MAX_DEPTH then error("stored value nested too deeply (or cyclic); limit is 16 levels", 0) end
		local out = {}
		for k, x in v do
			local kt = type(k)
			if kt ~= "string" and kt ~= "number" then error("table keys must be strings or integers", 0) end
			out[k] = copy(x, depth + 1)
		end
		return out
	elseif t == "nil" or t == "boolean" or t == "number" or t == "string" then
		return v
	end
	error("cannot store a " .. t .. " (allowed: nil, boolean, number, string, table)", 0)
end

local function size(v, depth)
	local t = type(v)
	if t == "string" then return #v + 8 end
	if t == "table" then
		local n = 16
		for k, x in v do n += (type(k) == "string" and #k or 8) + size(x, depth + 1) end
		return n
	end
	return 8
end

-- opts: { account = true|false (storage.account permission), quota_bytes = number|nil }
function M.new(opts)
	opts = opts or {}
	local rows = { character = {}, account = {}, world = { world = {} } }
	local used = 0
	local self = { rows = rows, permissions = { account = opts.account ~= false }, quota_bytes = opts.quota_bytes, writes = 0, reads = 0 }

	local function scopeOf(kind, owner)
		local bucket = rows[kind]
		local key = tostring(owner)
		if type(owner) == "table" and owner.id ~= nil then key = tostring(owner.id) end
		local row = bucket[key]
		if not row then row = {}; bucket[key] = row end
		local sc = {}
		function sc.get(_, k)
			self.reads += 1
			local v = row[k]
			if v == nil then return nil end
			return copy(v, 0)
		end
		function sc.set(_, k, v)
			self.writes += 1
			local old = row[k]
			if old ~= nil then used -= size(old, 0) + #k end
			if v == nil then row[k] = nil; return end
			local c = copy(v, 0)
			local add = size(c, 0) + #k
			if self.quota_bytes and used + add > self.quota_bytes then
				if old ~= nil then used += size(old, 0) + #k end
				error("storage quota exceeded", 0)
			end
			used += add
			row[k] = c
		end
		function sc.keys(_)
			local out = {}
			for k in row do out[#out + 1] = k end
			table.sort(out)
			return out
		end
		function sc.mod(_, _id) error("permission denied: storage.read is not granted", 0) end
		return sc
	end

	self.api = {
		character = function(ch) return scopeOf("character", ch) end,
		account = function(acc)
			if not self.permissions.account then error("permission denied: storage.account", 0) end
			return scopeOf("account", acc)
		end,
		world = function() return scopeOf("world", "world") end,
		migrate = function() end,
	}
	function self.usage() return used end
	return self
end

return M
