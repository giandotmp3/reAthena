--!nonstrict
-- Declarative stat-contribution constructors (architecture 6.5). Aggregation order
-- per attribute is owned by the engine `attrs` module; this package only declares.
local M = {}
function M.add(fn) return { op = "add", fn = fn } end
function M.mul_pct(fn) return { op = "mul_pct", fn = fn } end
function M.override(fn) return { op = "override", fn = fn } end
return M
