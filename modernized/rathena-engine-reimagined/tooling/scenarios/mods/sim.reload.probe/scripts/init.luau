--!nonstrict
-- generation marker: the reload scenario rewrites this file with GEN = 2
GEN = 1
events.on("sim:probe", { id = "gen", priority = 1, phase = "on", mode = "observe" }, function(ctx)
	ctx:enqueue("probe:gen", "target", tostring(GEN))
end)
