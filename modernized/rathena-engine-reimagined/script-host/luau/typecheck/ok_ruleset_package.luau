--!strict
-- A ruleset package: register(engine) defines the order, registers the bodies and runs the pipeline.
local M = {}
function M.register(e: Engine)
  e.pipeline.define("weapon", { "init", "final" })
  e.pipeline.stage("weapon", "init", function(S: any) S.n = 1 end, { protected = true })
  local quirks: boolean = e.config.legacy_quirks
  local x: number = e.num.f32(1.4)
end
function M.attack(S: any)
  local res = pipeline.run("weapon", S, { skip = { init = true }, lane = S.dmg, view = S })
  if res and res.cancelled then
    local why: string? = res.cancel_reason
  end
end
return M
