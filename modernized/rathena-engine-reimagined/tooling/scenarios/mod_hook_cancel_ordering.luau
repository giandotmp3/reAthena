--@rule BR-RULESET-116 BR-RULESET-117
--@mods sim.hook.alpha,sim.hook.beta,sim.hook.gamma
--@mods-dir mods
-- Hook ordering, cancellation and the effect queue (architecture section 5, "Ordering and cancellation"):
--   order     = phase (pre, on, post), then priority, then before/after constraints, then load order
--   cancel()  in a pre hook discards the effects queued during the dispatch and skips later hooks
--   effects   are queued by hooks and applied by the caller after the stage settled (BR-RULESET-116)
-- beta: pre/priority 5 "guard" (cancels on a negative value); alpha: on/priority 10 "bonus" (+1);
-- gamma: post/priority 1 "audit", declared after alpha/bonus.
local r = sim.dispatch("sim:stage", 100)
sim.assert_eq(r.cancelled, false)
sim.assert_eq(table.concat(r.ran, ","), "sim.hook.beta/guard,sim.hook.alpha/bonus,sim.hook.gamma/audit", "pre, on, post")
sim.assert_eq(r.value, 101, "the modify hook changed the value")
local kinds = {}
for _, e in ipairs(r.effects) do kinds[#kinds + 1] = e.kind end
sim.assert_eq(table.concat(kinds, ","), "beta:pre,alpha:fx,gamma:post", "effects commit in hook order")

local c = sim.dispatch("sim:stage", -5)
sim.assert_eq(c.cancelled, true, "the pre hook cancelled")
sim.assert_eq(c.reason, "negative")
sim.assert_eq(c.cancelled_by, "sim.hook.beta/guard")
sim.assert_eq(table.concat(c.ran, ","), "sim.hook.beta/guard", "no later hook ran")
sim.assert_eq(#c.effects, 0, "nothing was committed")
sim.assert_eq(c.value, -5, "the value is untouched")

-- the kill switch removes a mod's hooks at once; the others keep their order
sim.disable_mod("sim.hook.alpha")
sim.assert_eq(sim.mod_state("sim.hook.alpha"), "disabled")
local k = sim.dispatch("sim:stage", 10)
sim.assert_eq(table.concat(k.ran, ","), "sim.hook.beta/guard,sim.hook.gamma/audit", "alpha is gone")
sim.assert_eq(k.value, 10)
sim.enable_mod("sim.hook.alpha")
local again = sim.dispatch("sim:stage", 10)
sim.assert_eq(table.concat(again.ran, ","), "sim.hook.beta/guard,sim.hook.alpha/bonus,sim.hook.gamma/audit", "and back")

-- a reload that contradicts the declared order is refused naming both hooks; the old generation stays
local gen = sim.generation("sim.hook.gamma")
local bad = sim.reload("sim.hook.gamma", [[
events.on("sim:stage", { id = "audit", priority = 1, phase = "post", after = { "sim.hook.alpha/bonus" } }, function(ctx) end)
events.on("sim:stage", { id = "loop", priority = 1, phase = "post", after = { "sim.hook.gamma/audit" }, before = { "sim.hook.gamma/audit" } }, function(ctx) end)
]])
sim.assert_eq(bad.ok, false, "a constraint cycle is a load error")
sim.assert_true(string.find(bad.error, "audit", 1, true) ~= nil and string.find(bad.error, "loop", 1, true) ~= nil, "naming both hooks: " .. tostring(bad.error))
sim.assert_eq(sim.generation("sim.hook.gamma"), gen, "the old generation is still active")
