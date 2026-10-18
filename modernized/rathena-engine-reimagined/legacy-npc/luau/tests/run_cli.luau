--!nonstrict
-- Luau CLI entry: luau luau/tests/run_cli.luau   (all suites)
local H = require("./harness")
require("./all")
local _, fail = H.run()
if fail > 0 then error("failed cases: " .. fail) end
