--!nonstrict
-- The unit facts the status functions read, as a flat table of integers (the `u.` namespace of the expression language and the `fact|<name>`
-- contributions of scripts/status/aggregate). A fact the legacy function reads is listed by the attribute that reads it (attrs_re.luau).
--
-- unit = { kind = "pc" | "mob" | "hom", id, vit,                          -- vit: status_get_vit(bl), the renewal Angelus term of DEF2
--          weapon, sex, class_, option, perm_speed, skilltimer, ud_skill_id, ud_skill_lv,
--          bonus_speed_rate, bonus_speed_add_rate, bonus_hp, bonus_sp, bonus_ap,
--          map = 0 | 1 (GvG) | 2 (battleground), cfg = { gvg_flee_penalty, bg_flee_penalty, rental_mount_speed_boost },
--          skills = { NC_MADOLICENCE = 3, ... }, summoner_sea }            -- skill levels by script constant (pc_checkskill)
local consts = import("scripts/status/consts")
local num = import("scripts/status/num")

local M = {}

local function has(v, mask) return num.b2i(num.band(v or 0, mask) ~= 0) end

local SEA = { "SU_TUNABELLY", "SU_TUNAPARTY", "SU_BUNCHOFSHRIMP", "SU_FRESHSHRIMP", "SU_GROOMING", "SU_PURRING", "SU_SHRIMPARTY" }
local function sea(skills)
	local n = 0
	for _, name in ipairs(SEA) do n += (skills and skills[name]) or 0 end
	return n
end

-- flat facts of a unit (nested `skill` table for u.skill.NAME)
function M.of(unit)
	local pc = unit.kind == "pc"
	local cfg = unit.cfg or {}
	local option = unit.option or 0
	return {
		is_pc = num.b2i(pc), is_mob = num.b2i(unit.kind == "mob"), is_hom = num.b2i(unit.kind == "hom"),
		id = unit.id or 0, vit = unit.vit or 0, weapon = unit.weapon or 0, sex = unit.sex or 0, class_ = unit.class_ or 0,
		perm_speed = unit.perm_speed or 0, skilltimer = unit.skilltimer or 0, ud_skill_id = unit.ud_skill_id or 0, ud_skill_lv = unit.ud_skill_lv or 0,
		bonus_speed_rate = unit.bonus_speed_rate or 0, bonus_speed_add_rate = unit.bonus_speed_add_rate or 0,
		bonus_hp = unit.bonus_hp or 0, bonus_sp = unit.bonus_sp or 0, bonus_ap = unit.bonus_ap or 0,
		map = unit.map or 0, nosc = unit.nosc and 1 or 0,
		-- pc_checkskill_summoner(sd, SUMMONER_POWER_SEA) (pc.cpp:7342): the sum of seven skill levels in a uint8
		summoner_sea = num.wrap_u8(sea(unit.skills)),
		cfg_gvg_flee_penalty = cfg.gvg_flee_penalty or 0, cfg_bg_flee_penalty = cfg.bg_flee_penalty or 0,
		cfg_rental_mount_speed_boost = cfg.rental_mount_speed_boost or 0,
		-- pc_isriding(sd) || sc.option & OPTION_DRAGON, pc_isridingwug(sd), pc_ismadogear(sd), sd->ud.skill_id == LG_EXEEDBREAK
		riding = has(option, consts.OPTION_RIDING + consts.OPTION_DRAGON), wug_rider = has(option, consts.OPTION_WUGRIDER),
		mado = has(option, consts.OPTION_MADOGEAR), exeedbreak = num.b2i((unit.ud_skill_id or 0) == consts.LG_EXEEDBREAK),
		second_assassin = num.b2i(num.band(unit.class_ or 0, consts.MAPID_SECONDMASK) == consts.MAPID_ASSASSIN),
		ranged_weapon = num.b2i(pc and (unit.weapon == consts.W_BOW or unit.weapon == consts.W_REVOLVER or unit.weapon == consts.W_RIFLE
			or unit.weapon == consts.W_GATLING or unit.weapon == consts.W_SHOTGUN or unit.weapon == consts.W_GRENADE)),
		skill = unit.skills or {},
	}
end

return M
