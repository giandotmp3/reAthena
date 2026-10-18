--!nonstrict
-- Recording fake of the engine API used by package registration tests.
return function()
	local log = { pipelines = {}, status = {}, skill = {}, item = {}, native = {} }
	local function kind(name)
		return { define = function(id, def) log[name][id] = def end }
	end
	local engine = {
		pipeline = {
			define = function(name, order) log.pipelines[name] = order end,
			use_native = function(...) log.native[#log.native + 1] = { ... } end,
		},
		status = kind("status"), skill = kind("skill"), item = kind("item"),
	}
	return engine, log
end
