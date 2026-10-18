--!nonstrict
-- Package entry point. The mod loader calls register(engine) once per generation.
-- `engine` is the typed API of script-host (api/engine.yaml): pipeline, num, config; status / skill / item registries when the host has them.
local M = {}

function M.register(engine)
	local domain = import("scripts/domain")
	-- the manifest's legacy_quirks (mod.toml [ruleset], default true) becomes the default of cfg.legacy_quirks
	local config = engine.config
	if config and config.legacy_quirks ~= nil then import("data/damage_cfg").legacy_quirks = config.legacy_quirks end
	import("scripts/pipeline").register(engine)

	-- every starter definition is validated; it is handed to the engine registry of its kind when the host offers one (the
	-- script-host API has pipeline, num, config today; status / skill / item registries are not implemented there yet)
	local function each(kind, defs, validate)
		for _, def in ipairs(defs) do
			local ok, err = validate(def)
			if not ok then error("invalid " .. kind .. " definition: " .. tostring(err)) end
			local registry = engine[kind]
			if registry ~= nil then registry.define(def.id, def) end
		end
	end
	each("status", import("data/statuses"), domain.validate_status)
	each("skill", import("data/skills"), domain.validate_skill)
	each("item", import("data/items"), domain.validate_item)
end

-- script-host defines the globals pipeline, num and config in every mod state (api/engine.yaml). The mod loader's bundler calls
-- register(engine) only when a global `engine` exists, which script-host does not define by default (rathena.base has a module
-- whose register(name, handler) means something else), so under script-host the package registers itself when its entry module
-- loads. The Luau CLI suites have no such globals and call M.register(fake engine) explicitly (tests/support/booted).
if type(pipeline) == "table" and type(pipeline.define) == "function" then
	M.register({ pipeline = pipeline, num = num, config = config })
end

return M
