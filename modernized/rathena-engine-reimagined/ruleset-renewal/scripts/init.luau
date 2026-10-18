--!nonstrict
-- Package entry point (BR-RULESET-019/118). The mod loader calls register(engine) once per generation.
-- `engine` is the typed API from script-host's api/engine.yaml. This package uses engine.pipeline (define, stage, run), engine.num
-- (f32) and engine.config (legacy_quirks); see docs/ARCHITECTURE_PROOF.md section 6.
local M = {}

function M.register(engine)
	-- the manifest's legacy_quirks (mod.toml [ruleset], default true) becomes the default of cfg.legacy_quirks
	local quirks = engine.config.legacy_quirks
	if quirks ~= nil then import("data/damage_cfg").legacy_quirks = quirks end
	import("scripts/pipeline").register(engine)
end

-- script-host defines the globals pipeline, num and config in every mod state (api/engine.yaml). The mod loader's bundler calls
-- register(engine) only when a global `engine` exists, which script-host does not define by default (rathena.base has a module
-- whose register(name, handler) means something else), so under script-host the package registers itself when its entry module
-- loads. The Luau CLI suites have no such globals and call M.register(fake engine) explicitly (tests/support/booted).
if type(pipeline) == "table" and type(pipeline.define) == "function" then
	M.register({ pipeline = pipeline, num = num, config = config })
end

return M
