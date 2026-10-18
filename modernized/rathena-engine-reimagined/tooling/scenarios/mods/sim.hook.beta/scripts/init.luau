--!nonstrict
events.on("sim:stage", { id = "guard", priority = 5, phase = "pre", mode = "observe" }, function(ctx)
	if ctx:get() < 0 then
		ctx:cancel("negative")
		return
	end
	ctx:enqueue("beta:pre", "target")
end)
