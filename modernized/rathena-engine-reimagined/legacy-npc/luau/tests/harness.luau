--!nonstrict
-- Minimal test harness shared by the Luau test files. Runs unchanged under the Luau CLI and inside a script-host sandbox
-- (the C++ test runner loads the bundled package + tests and reads the "@case" lines printed by H.run()).
local H = { cases = {}, suites = {} }

-- H.case(tag, name, fn): fn raises (error or H.eq failure) to fail. A tag names the rule (LNPC-*, BR-RULESET-*).
function H.case(tag: string, name: string, fn: () -> ())
	H.cases[#H.cases + 1] = { tag = tag, name = name, fn = fn }
end

function H.eq(got, want, msg: string?)
	if got ~= want then
		error(string.format("%sexpected %s, got %s", msg and (msg .. ": ") or "", tostring(want), tostring(got)), 0)
	end
end

-- H.skip(reason): the case needs something this host lacks (reported as SKIP, never as a pass)
function H.skip(reason: string)
	error({ skip = true, reason = reason }, 0)
end

function H.truthy(v, msg: string?)
	if not v then error(msg or "expected a truthy value", 0) end
end

-- runs every registered case; prints one "@case" line per case and returns pass, fail
function H.run(filter: string?)
	local pass, fail, skip = 0, 0, 0
	for _, c in H.cases do
		if not filter or string.find(c.tag, filter, 1, true) or string.find(c.name, filter, 1, true) then
			local ok, err = pcall(c.fn)
			if ok then
				pass += 1
				print("@case\tPASS\t" .. c.tag .. "\t" .. c.name)
			elseif type(err) == "table" and err.skip then
				skip += 1
				print("@case\tSKIP\t" .. c.tag .. "\t" .. c.name .. "\t" .. tostring(err.reason))
			else
				fail += 1
				if type(err) == "table" then err = (err.type or "error") .. ":" .. tostring(err.cmd or err.msg or err.cause or "") end
				print("@case\tFAIL\t" .. c.tag .. "\t" .. c.name .. "\t" .. string.gsub(tostring(err), "[\n\t]", " "))
			end
		end
	end
	print(string.format("@summary\t%d\t%d\t%d", pass, fail, skip))
	return pass, fail
end

return H
