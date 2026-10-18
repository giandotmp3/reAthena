--!nonstrict
events.on("sim:stage", { id = "audit", priority = 1, phase = "post", mode = "observe", after = { "sim.hook.alpha/bonus" } }, function(ctx)
	ctx:enqueue("gamma:post", "target")
end)
