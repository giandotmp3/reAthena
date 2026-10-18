--!nonstrict
-- Package entry point (BR-RULESET-019/118). The mod loader calls register(engine) once per generation.
-- `engine` is the typed API from api/*.yaml; this package needs only engine.pipeline.define (see docs/ARCHITECTURE_PROOF.md section 6).
local M = {}

function M.register(engine)
	import("scripts/pipeline").register(engine)
end

return M
