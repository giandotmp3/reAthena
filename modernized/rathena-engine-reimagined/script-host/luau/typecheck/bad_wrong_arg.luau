--!strict
-- Wrong argument types and unknown methods must be reported.
events.on("battle:damage", { id = "x" }, function(ctx)
  ctx:mul_pct("120")
  ctx:no_such_method()
end)
say(42)
