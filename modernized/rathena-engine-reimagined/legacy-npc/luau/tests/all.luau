--!nonstrict
-- Registers every Luau suite with the harness (the bundle runner and the CLI entry both require this).
require("./t_semantics")
require("./t_neg")
require("./t_vars")
require("./t_loader")
require("./t_events")
require("./t_runtime")
return true
