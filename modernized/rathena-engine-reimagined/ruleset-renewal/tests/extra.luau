--!nonstrict
-- Extra suite: the pipeline API behaviour (move, no-body, effective-order probes) and legacy_quirks. tests/main is what rathena-sim runs
-- and pins at 111 tests; this entry adds to it, tools/run_tests.py runs both.
import("tests/support/booted")
local T = import("tests/support/harness")
import("tests/acceptance/pipeline_api_test")
import("tests/acceptance/quirks_test")
import("tests/acceptance/status_golden_test")
import("tests/acceptance/status_define_test")
import("tests/acceptance/status_model_test")
return T.summary()
