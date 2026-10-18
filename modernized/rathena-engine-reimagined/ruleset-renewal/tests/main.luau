--!nonstrict
-- Test entry. Run by tools/run_tests.py (bundled for the Luau CLI) and by the Python wrapper in tests/test_ruleset_renewal.py.
-- Returns a process exit code (0 = success). Every test prints one line "T <STATUS> <rule> <title>" so the wrapper can map
-- Luau results to BR-RULESET IDs.
import("tests/support/booted")
local T = import("tests/support/harness")
import("tests/acceptance/units_test")
import("tests/acceptance/weapon_golden_test")
import("tests/acceptance/architecture_test")
return T.summary()
