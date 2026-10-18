--!nonstrict
-- in-memory world: players, env (global/instance stores, function registry), profiles
local W = {}

local PROFILES = {
	-- "poor": has nothing, low level -> exercises the refusal branches
	poor = { zeny = 0, level = 10, jlevel = 10, item = 0, class = 0 },
	-- "rich": has every item, high level -> exercises the success branches
	rich = { zeny = 99999999, level = 175, jlevel = 70, item = 1000, class = 4008 },
}
W.PROFILES = PROFILES

function W.newPlayer(profile)
	local p = PROFILES[profile or "poor"]
	return {
		profile = profile or "poor", name = "Tester", party = "Party", guild = "Guild", map = "prontera", x = 150, y = 150, charid = 150000, accountid = 2000000, partyid = 0, guildid = 0, gmlevel = 0, equip = {},
		perm = {}, temp = {}, acc = {}, acc2 = {}, inv = {}, quests = {},
		defaultItemCount = p.item,
		params = { zeny = p.zeny, baselevel = p.level, joblevel = p.jlevel, class = p.class, sex = 1, weight = 0, maxweight = 20000,
			hp = 5000, maxhp = 5000, sp = 500, maxsp = 500, statuspoint = 0, skillpoint = 0, baseexp = 0, jobexp = 0,
			nextbaseexp = 1000000, nextjobexp = 1000000, karma = 0, manner = 0, upper = 0, str = 99, agi = 99, vit = 99, int = 99,
			dex = 99, luk = 99, bantime = 0, pc_die_counter = 0, language = 0, maxweight_ = 0 },
	}
end

-- Data-provider interface: read-only game DATA (not state) that the interpreter does not own. A host passes its own table as `data`
-- (3rd argument of newEnv); the replay harness feeds values recorded from the real legacy VM. Every field is optional; a missing
-- provider or a nil answer means "unknown" and the command answers like the legacy VM does for an unknown key.
--   data.itemname(idOrAegisName) -> string | nil     item_db "Name" (getitemname)
--   data.battleflag(lowercaseName) -> number | nil   battle config value (getbattleflag; unknown = 0)
--   data.castle(mapName) -> boolean                  is a castle map (getcastledata; no guild owns any castle in this world)
--   data.shop(npcName) -> boolean                    is a shop-type NPC (callshop)
--   data.mapexists(mapName) -> boolean               map is loaded on this server (warp, mobcount; default: every map exists)
W.defaultData = {
	itemname = function(key) return nil end,
	battleflag = function(name) return nil end,
	castle = function(map) return false end,
	shop = function(name) return false end,
	mapexists = function(map) return true end,
}

function W.newEnv(funcs, seed, data)
	return { global = {}, gtemp = {}, instance = {}, funcs = funcs or {}, seed = seed or 12345, clock = 0, mobs = {}, npcvars = {},
		data = data or W.defaultData }
end

return W
