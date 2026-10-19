--!nonstrict
-- Prints, for every rule in tests/support/status_rule_slices that has a golden record, the Lua literals of the derived `given` / `expected`
-- (tab separated: rule, golden, given, expected). Consumed by conformance/golden_gen/splice_status_cases.py.
--   python3 tools/bundle_luau.py tests/print_status_rule_cases build/print_status_rules.luau && luau build/print_status_rules.luau
local runner = import("tests/support/status_golden_runner")
local slices = import("tests/support/status_rule_slices")

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
		elseif k:match("^[%a_][%w_]*$") then out[#out + 1] = k .. " = " .. ser(v[k])
		else out[#out + 1] = "[" .. string.format("%q", k) .. "] = " .. ser(v[k]) end
	end
	return "{" .. table.concat(out, ", ") .. "}"
end

return function()
	local g = runner.load()
	local ids = {}
	for id in pairs(slices) do
		if id:sub(1, 11) == "BR-RULESET-" then ids[#ids + 1] = id end
	end
	table.sort(ids)
	for _, id in ipairs(ids) do
		local sl = slices[id]
		if sl.find then
			local golden = sl.golden or slices.golden_tags[id]
			local rec = sl.find(g, golden)
			if rec == nil then
				print(id .. "\tNO RECORD")
			else
				print(id .. "\t" .. string.format("%q", golden) .. "\t" .. ser(sl.given(g, rec)) .. "\t" .. ser(sl.expected(g, rec)))
			end
		end
	end
	return 0
end
