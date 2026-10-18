--!nonstrict
-- Test entry. Run by tools/run_tests.py locally and by rathena-sim in CI.
-- Returns a process exit code (0 = success).
local T = import("tests/support/harness")
import("tests/smoke_test")
for _, g in ipairs(import("conformance/index").GROUPS) do
	import("tests/acceptance/" .. g .. "_test")
end
return T.summary()
