--!nonstrict
-- Helpers shared by the runtime-level suites.
local Legacy = require("../legacy")
local Blocking = require("../blocking")

local U = { Legacy = Legacy }

-- one floating script NPC declaration
function U.npc(name: string, body: string, head: string?): string
	return (head or "-") .. "\tscript\t" .. name .. "\t-1,{\n" .. body .. "\n}\n"
end

-- files: { path = text } or an array of { path, text } to keep order; cfg: Legacy.new config
function U.rt(files, cfg)
	local rt = Legacy.new(cfg or {})
	if files[1] then
		for _, f in files do rt:addFile(f[1], f[2]) end
	else
		for path, text in files do rt:addFile(path, text) end
	end
	return rt
end

-- runtime with a single file of NPCs
function U.rt1(text: string, cfg)
	return U.rt({ { "t.txt", text } }, cfg)
end

-- interactive run with a scripted client; returns outcome, api (api.log = requests seen), mes lines recorded
function U.play(rt, npcName: string, answers, rid: number?, label: string?)
	local api = Blocking.scripted(answers)
	rt.dialogue = api
	local ok, o = pcall(rt.run, rt, npcName, rid or 1, label)
	if not ok then
		if type(o) == "table" and o.type == "CANCELLED" then return { status = "cancelled" }, api end
		error(o, 0)
	end
	if o.status == "error" and o.err and o.err.type == "CANCELLED" then o.status = "cancelled" end
	return o, api
end

function U.worldGet(rt, name: string)
	local scope = rt.backend.storage.world()
	return scope:get("legacy:world/" .. name)
end
function U.charGet(rt, charid, name: string)
	return rt.backend.storage.character(charid):get("legacy:char/" .. name)
end

return U
