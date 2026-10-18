--!strict
-- Package entry: registers the renewal pipeline order, tables and hooks through the generic
-- pipeline/status APIs. Must not require engine changes or add mechanism to rathena.base.
local M = {}

function M.register(api: any)
  -- Pending: api.pipeline.define("weapon", data.pipeline.weapon), api.pipeline.define("magic", ...),
  -- then api.pipeline.use_native(...) for hot stages and Luau reference stages (BR-RULESET-111).
  error("NotImplemented: BR-RULESET-019")
end

return M
