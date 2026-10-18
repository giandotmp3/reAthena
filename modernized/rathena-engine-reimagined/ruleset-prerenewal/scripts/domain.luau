--!nonstrict
-- Domain model validators for ruleset definitions. Pure functions, no engine calls.
local M = {}

local PREFIX = "ruleset.prerenewal:"
local FIELD_TYPES = { int = true, double = true, bool = true, string = true }
local OPS = { add = true, mul_pct = true, override = true }

local function fail(msg) return false, msg end

local function check_id(id, kind)
	if type(id) ~= "string" then return fail("id must be a string") end
	if id:sub(1, #PREFIX + #kind + 1) ~= PREFIX .. kind .. "/" then
		return fail("id must start with " .. PREFIX .. kind .. "/")
	end
	return true
end

-- Returns ok, err. Statuses name their fields (replaces val1..val4, Q-F).
function M.validate_status(def)
	local ok, err = check_id(def.id, "status")
	if not ok then return fail(err) end
	if type(def.client_id) ~= "number" then return fail("client_id must be a number") end
	for name, ty in pairs(def.fields or {}) do
		if not FIELD_TYPES[ty] then return fail("field " .. name .. " has unknown type") end
	end
	for attr, c in pairs(def.contributes or {}) do
		if not OPS[c.op] or type(c.fn) ~= "function" then return fail("bad contribution for " .. attr) end
	end
	return true
end

function M.validate_skill(def)
	local ok, err = check_id(def.id, "skill")
	if not ok then return fail(err) end
	if type(def.client_id) ~= "number" then return fail("client_id must be a number") end
	if type(def.max_level) ~= "number" or def.max_level < 1 then return fail("max_level must be >= 1") end
	return true
end

function M.validate_item(def)
	local ok, err = check_id(def.id, "item")
	if not ok then return fail(err) end
	if type(def.client_id) ~= "number" then return fail("client_id must be a number") end
	return true
end

return M
