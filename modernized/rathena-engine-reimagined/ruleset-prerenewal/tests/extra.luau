--!nonstrict
-- Extra suite: the pipeline API through the engine, legacy_quirks, and the status golden replay (tests/acceptance/status_golden_test). tests/main is what rathena-sim runs and pins at 96 pass / 86
-- expected-failure; this entry adds to it, tools/run_tests.py runs both. Lua-subset source.
import("tests/support/booted")
local T = import("tests/support/harness")
import("tests/acceptance/pipeline_test")
import("tests/acceptance/quirks_test")
import("tests/acceptance/status_golden_test")
import("tests/acceptance/status_quirks_test")
return T.summary()
