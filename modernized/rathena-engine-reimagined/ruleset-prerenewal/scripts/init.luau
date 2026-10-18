--!strict-compatible
-- Package entry point. The mod loader calls register(engine) once per generation.
-- `engine` is the typed API from api/*.yaml (pipeline, status, skill, item).
local M = {}

function M.register(engine)
	local domain = import("scripts/domain")
	import("scripts/pipeline").register(engine)

	local function each(kind, defs, validate)
		for _, def in ipairs(defs) do
			local ok, err = validate(def)
			if not ok then error("invalid " .. kind .. " definition: " .. tostring(err)) end
			engine[kind].define(def.id, def)
		end
	end
	each("status", import("data/statuses"), domain.validate_status)
	each("skill", import("data/skills"), domain.validate_skill)
	each("item", import("data/items"), domain.validate_item)
end

return M
