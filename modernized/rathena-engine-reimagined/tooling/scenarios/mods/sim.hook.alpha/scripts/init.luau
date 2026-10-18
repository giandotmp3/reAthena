--!nonstrict
events.on("sim:stage", { id = "bonus", priority = 10, phase = "on", mode = "modify" }, function(ctx)
	ctx:add(1)
	ctx:enqueue("alpha:fx", "target")
end)
