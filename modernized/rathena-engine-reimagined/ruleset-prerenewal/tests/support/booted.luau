--!nonstrict
-- Registers the package with the fake host (tests/support/fake_host) so that weapon.calc_attack can run: define + stage bodies,
-- num.f32 and config bound exactly as the mod loader does with the real engine table. Every test entry imports this first.
-- (A test that registers the package with a different engine must call booted.boot() afterwards to restore the default binding.)
local fake_host = import("tests/support/fake_host")
local init = import("scripts/init")

local M = {}

-- Runs fn with whatever it registers undone afterwards: the package's binding to the engine (pipeline.run, num) and the cfg default
-- legacy_quirks are restored, so a test can register the package with a recording or custom-config engine without disturbing the
-- binding the other tests (and, under the real host, the real runtime) use.
function M.isolated(fn)
	local weapon = import("scripts/damage/weapon")
	local cfg = import("data/damage_cfg")
	local saved, quirks = weapon.binding(), cfg.legacy_quirks
	local ok, err = pcall(fn)
	weapon.bind(saved)
	cfg.legacy_quirks = quirks
	if not ok then error(err, 0) end
end

function M.boot(config)
	local engine, log = fake_host(config)
	init.register(engine)
	M.engine, M.log = engine, log
	return engine, log
end

-- Under the real host (the mod loader, rathena-sim, script-host's tests) the globals `pipeline`, `num` and `config` exist and the
-- package registered itself with them when scripts/init loaded; only the Luau CLI, which has no host, boots the double.
if type(pipeline) ~= "table" then M.boot() end
return M
