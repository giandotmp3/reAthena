-- rathena.base: table-loading helper (mechanism only).
-- Builds a read-only lookup from rows keyed by a caller-supplied field.
local M = {}

function M.index_by(rows, key)
  local out = {}
  for _, row in ipairs(rows) do
    out[row[key]] = row
  end
  return table.freeze(out)
end

return M
