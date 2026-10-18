--!nonstrict
-- PROVISIONAL starter items. equip_bonus is a compiled bonus-delta list (BR-RULESET-112),
-- never live memory writes. Critical is scaled x10 (BR-RULESET-100).
local P = "ruleset.prerenewal:item/"
return {
	{ id = P .. "red_potion", client_id = 501, type = "healing", heal_hp = { 45, 65 } },
	{ id = P .. "orange_potion", client_id = 502, type = "healing", heal_hp = { 105, 145 } },
	{ id = P .. "knife", client_id = 1201, type = "weapon", weapon_level = 1, attack = 17, slots = 3 },
	{ id = P .. "cotton_shirt", client_id = 2301, type = "armor", defense = 10, slots = 0 },
	{ id = P .. "poring_card", client_id = 4001, type = "card",
	  equip_bonus = { { attr = "luk", op = "add", value = 2 }, { attr = "cri", op = "add", value = 10 } } },
}
