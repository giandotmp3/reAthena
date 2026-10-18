--!nonstrict
events.on("sim:roll", { id = "roll", priority = 2, phase = "on", mode = "observe" }, function(ctx)
	ctx:enqueue("rng.b:value", "target", tostring(rng.roll(1000000)))
	ctx:enqueue("rng.b:value", "target", tostring(rng.roll(1000000)))
end)
