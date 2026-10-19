--!nonstrict
-- Prints the status definitions as tab separated lines for tools/gen_status_fields.py (docs/STATUS_FIELDS.md).
local S = import("scripts/status/init")
return function()
	local names = {}
	for _, n in ipairs(S.status.names()) do names[#names + 1] = n end
	table.sort(names)
	for _, name in ipairs(names) do
		local def = S.status.get(name)
		local rec = def.record
		local f = {}
		for i = 1, 4 do f[i] = def.fields[i] or "-" end
		print(("S\t%s\t%s\t%s\t%s\t%s\t%s\t%s"):format(name, rec and ("SC_" .. rec.name) or "-", f[1], f[2], f[3], f[4], (def.note or ""):gsub("\t", " ")))
		local attrs = {}
		for a in pairs(def.contributions) do attrs[#attrs + 1] = a end
		table.sort(attrs)
		for _, a in ipairs(attrs) do
			for _, c in ipairs(def.contributions[a]) do
				print(("C\t%s\t%s\t%s\t%s\t%s\t%s\t%s"):format(name, a, c.op, c.value_src or "", c.when_src or "", tostring(c.at), c.group and (c.group .. (c.rank and ("#" .. c.rank) or "")) or ""))
			end
		end
		local states = {}
		for k, v in pairs(def.state_when) do states[#states + 1] = k .. ": " .. v.src end
		table.sort(states)
		for _, st in ipairs(states) do print(("W\t%s\t%s"):format(name, st)) end
	end
	return 0
end
