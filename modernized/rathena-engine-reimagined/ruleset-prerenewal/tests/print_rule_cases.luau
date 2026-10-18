--!nonstrict
-- Prints, for every rule in tests/support/rule_slices, the lowest golden id satisfying `find` and the Lua literals of
-- the derived `given` / `expected`. Consumed by conformance/golden_gen/splice_rule_cases.py.
--   python3 tools/bundle_luau.py tests/print_rule_cases build/print_rules.luau && luau build/print_rules.luau
local runner = import("tests/support/golden_runner")
local slices = import("tests/support/rule_slices")

local function ser(v)
	local t = type(v)
	if t == "number" then return tostring(v) end
	if t == "boolean" or t == "nil" then return tostring(v) end
	if t == "string" then return string.format("%q", v) end
	local keys, is_array = {}, true
	for k in pairs(v) do
		keys[#keys + 1] = k
		if type(k) ~= "number" then is_array = false end
	end
	if is_array then
		local n = 0
		for _ in pairs(v) do n = n + 1 end
		for i = 1, n do if v[i] == nil then is_array = false end end
	end
	table.sort(keys, function(a, b)
		if type(a) == type(b) then return a < b end
		return type(a) == "number"
	end)
	local out = {}
	for _, k in ipairs(keys) do
		if is_array then out[#out + 1] = ser(v[k])
		elseif type(k) == "number" then out[#out + 1] = "[" .. k .. "] = " .. ser(v[k])
		else out[#out + 1] = k .. " = " .. ser(v[k]) end
	end
	return "{" .. table.concat(out, ", ") .. "}"
end

return function()
	local cases = runner.load()
	local ids = {}
	for id in pairs(slices) do ids[#ids + 1] = id end
	table.sort(ids)
	for _, id in ipairs(ids) do
		local sl = slices[id]
		local found
		for _, rec in ipairs(cases) do
			if sl.find(rec) then
				found = rec
				break
			end
		end
		if not found then
			print(id .. "\tNOT_FOUND")
		else
			print(id .. "\t" .. found.id .. "\t" .. ser(sl.given(found)) .. "\t" .. ser(sl.expected(found)))
		end
	end
	return 0
end
