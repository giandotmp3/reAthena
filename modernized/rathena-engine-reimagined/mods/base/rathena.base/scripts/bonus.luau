-- rathena.base: bonus vocabulary registry (mechanism only).
-- Rulesets register vocabulary names and meanings; base ships none.
local M = {}
local registry = {}

function M.register(name, handler)
  assert(registry[name] == nil, "bonus already registered: " .. name)
  registry[name] = handler
end

function M.lookup(name)
  return registry[name]
end

return M
