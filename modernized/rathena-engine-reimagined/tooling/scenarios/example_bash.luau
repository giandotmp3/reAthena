-- rule: BR-RULESET-020
-- Card fix multiplies (spec example E-3). Requires sim.player / cast, not yet implemented.
local p = sim.player { job = "knight", level = 99 }
local mob = sim.spawn("ruleset.prerenewal:mob/poring")
p:cast("ruleset.prerenewal:skill/sm_bash", mob)
assert(sim.last_damage(mob) > 0)
