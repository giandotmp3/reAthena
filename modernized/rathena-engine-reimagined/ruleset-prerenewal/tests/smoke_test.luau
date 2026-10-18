--!strict-compatible
-- Non-rule smoke tests: package registration and domain validators.
local T = import("tests/support/harness")
local fake = import("tests/support/fake_host")
local init = import("scripts/init")
local domain = import("scripts/domain")
local pipeline = import("scripts/pipeline")

T.run("SMOKE", "register defines pipelines, statuses, skills and items", false, function()
	local engine, log = fake()
	init.register(engine)
	T.assert_deep_equal(log.pipelines.weapon, pipeline.ORDER.weapon, "SMOKE")
	assert(log.status["ruleset.prerenewal:status/blessing"], "blessing missing")
	assert(log.skill["ruleset.prerenewal:skill/al_heal"], "heal missing")
	assert(log.item["ruleset.prerenewal:item/poring_card"], "card missing")
end)

T.run("SMOKE", "status ids outside the package namespace are rejected", false, function()
	local ok = domain.validate_status({ id = "other:status/x", client_id = 1 })
	assert(not ok, "foreign namespace accepted")
end)

T.run("SMOKE", "unknown field type is rejected", false, function()
	local ok = domain.validate_status({ id = "ruleset.prerenewal:status/x", client_id = 1, fields = { a = "int64" } })
	assert(not ok, "bad field type accepted")
end)

T.run("SMOKE", "mod.toml-independent: pending list only names assigned rules", false, function()
	local required, pending = import("conformance/required"), import("conformance/pending")
	local want = {}
	for _, id in ipairs(required) do want[id] = true end
	for id in pairs(pending) do assert(want[id], "pending rule not assigned: " .. id) end
end)
