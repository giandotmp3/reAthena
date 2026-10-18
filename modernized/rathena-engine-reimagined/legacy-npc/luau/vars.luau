--!nonstrict
-- Variable scopes (architecture section 7, "Variables"). The interpreter keeps addressing a scope as a table of registers
-- `reg[name] = number | string | { [index] = number | string }` (name is lower-case, without sigil; a trailing `$` marks a string
-- variable). This module decides what object stands behind that table:
--
--   legacy sigil   scope        backing                                    key in script-host storage
--   name           character    storage.character(char)  (persistent)      legacy:char/<name>
--   @name          char temp    plain table per character (cleared at logout)
--   $name          world        storage.world()          (persistent)      legacy:world/<name>
--   $@name         world temp   plain table per world
--   #name          account      storage.account(acc)  needs storage.account  legacy:account/<name>
--   ##name         account glob storage.account(acc)  needs storage.account  legacy:account_global/<name>
--   .name          NPC object   plain table owned by the loaded NPC body (shared by duplicate clones, see loader.luau)
--   .@name         call frame   Luau table per heap frame (interp.luau)
--   'name          instance     plain table per instance
--
-- (the key spelling equals the C++ `storage_key()` of variables.hpp.) Persistent scopes are proxies with a read cache and
-- write-behind: a slice (run until the next yield) reads each key at most once and writes each dirty key once, at flush(). The
-- host `storage` object is anything with character()/account()/world() returning scopes with get/set/keys (script-host's
-- global, or storage_mem.luau in tests).
local M = {}

M.MAX_KEY = 32 -- script_check_RegistryVariableLength(0): the full name incl. sigil and `$` must be shorter than 33 characters
M.MAX_STR = 254 -- (1): string values must be shorter than 255

local internals = setmetatable({}, { __mode = "k" }) -- proxy -> { flush = fn, drop = fn }

local function permissionDenied(permission: string)
	error({ type = "PERMISSION_DENIED", permission = permission, cmd = "storage" }, 0)
end

-- a register table backed by a storage scope. keyPrefix e.g. "legacy:char/"; sigil is only used for key-length checks upstream.
local function persistentReg(scopeObj, keyPrefix: string)
	local cache, loaded, dirty = {}, {}, {}
	local plen = #keyPrefix
	local proxy = {}
	local function flush()
		for name in dirty do
			scopeObj:set(keyPrefix .. name, cache[name])
			dirty[name] = nil
		end
	end
	local function drop()
		flush()
		cache, loaded = {}, {}
	end
	internals[proxy] = { flush = flush, drop = drop }
	setmetatable(proxy, {
		__index = function(_, name)
			if loaded[name] then return cache[name] end
			local v = scopeObj:get(keyPrefix .. name)
			loaded[name] = true
			cache[name] = v
			return v
		end,
		__newindex = function(_, name, v)
			loaded[name] = true
			cache[name] = v
			dirty[name] = true
		end,
		-- `for name, value in reg do`: a snapshot of every key of this scope (flushes first)
		__iter = function(_)
			flush()
			local snap = {}
			for _, k in scopeObj:keys() do
				if string.sub(k, 1, plen) == keyPrefix then snap[string.sub(k, plen + 1)] = scopeObj:get(k) end
			end
			return next, snap, nil
		end,
	})
	return proxy
end

local function deniedReg(permission: string)
	return setmetatable({}, {
		__index = function() permissionDenied(permission) end,
		__newindex = function() permissionDenied(permission) end,
		__iter = function() permissionDenied(permission) end,
	})
end

-- Writes the dirty keys of the given registers (plain tables are ignored).
function M.flush(...)
	for i = 1, select("#", ...) do
		local r = select(i, ...)
		local x = r and internals[r]
		if x then x.flush() end
	end
end
-- Flushes, then forgets the read cache: call when a slice ends so that the next one sees other scripts' writes.
function M.drop(...)
	for i = 1, select("#", ...) do
		local r = select(i, ...)
		local x = r and internals[r]
		if x then x.drop() end
	end
end
function M.isProxy(r) return internals[r] ~= nil end

-- Whether `name` (lower-case, with `$` suffix for strings, no sigil) may be written to `scope` ("" "@" "$" "$@" "#" "##" "." ".@" "'")
-- with `val`; returns ok, value (a string may be shortened). Mirrors the legacy rules:
--   key  : sigil + name < 33 characters, else set_reg_* refuses the write (script.cpp:3144, :3224)
--   value: a string >= 255 characters is refused for character and account registers (pc.cpp:11655) and cut to 254 for `$`
--          (the 254-character column of the save path, intif.cpp:459); temporary scopes have no limit
function M.writeOk(scope: string, name: string, val)
	if #scope + #name > M.MAX_KEY then return false, val end
	if type(val) == "string" and #val > M.MAX_STR then
		if scope == "" or scope == "#" or scope == "##" then return false, val end
		if scope == "$" then return true, string.sub(val, 1, M.MAX_STR) end
	end
	return true, val
end

-- Backend: where the persistent scopes live. `storage` is the host storage global (or storage_mem's .api).
function M.newBackend(storage, opts)
	opts = opts or {}
	local B = { storage = storage }
	-- registers of one character: perm / acc / acc2 are persistent, temp is a plain table
	function B.playerRegs(charId, accountId)
		local perm = persistentReg(storage.character(charId), "legacy:char/")
		local acc, acc2
		local ok, scope = pcall(storage.account, accountId)
		if ok then
			acc = persistentReg(scope, "legacy:account/")
			acc2 = persistentReg(scope, "legacy:account_global/")
		else
			-- the mod did not request/was not granted storage.account: using `#` or `##` variables is a typed error, nothing else is
			acc, acc2 = deniedReg("storage.account"), deniedReg("storage.account")
		end
		return { perm = perm, temp = {}, acc = acc, acc2 = acc2 }
	end
	-- registers shared by the whole world
	function B.worldRegs()
		return { global = persistentReg(storage.world(), "legacy:world/"), gtemp = {}, instance = {} }
	end
	return B
end

-- default backend for tests and harnesses: a private in-memory storage per call (nothing is shared between runs)
function M.memoryBackend(opts)
	local mem = require("./storage_mem").new(opts)
	local B = M.newBackend(mem.api)
	B.mem = mem
	return B
end

return M
