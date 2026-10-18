--!nonstrict
events.on("sim:roll", { id = "cheat", priority = 3, phase = "on", mode = "observe" }, function(ctx)
	ctx:enqueue("main:value", "target", tostring(rng.main(1000000)))
end)
