--!nonstrict
-- Legacy numeric identities used by the renewal weapon pipeline (values copied from the legacy headers; the golden data records
-- them as plain numbers, so they are part of the conformance surface).
return {
	-- block_list types (map.hpp enum bl_type)
	BL_PC = 1, BL_MOB = 2,
	-- battle flags (battle.hpp e_battle_flag)
	BF_WEAPON = 0x0001, BF_SHORT = 0x0010, BF_LONG = 0x0040, BF_NORMAL = 0x0200,
	-- clif.hpp e_damage_type
	DMG_NORMAL = 0, DMG_MULTI_HIT = 8, DMG_CRITICAL = 10, DMG_LUCY_DODGE = 11,
	-- battle.hpp damage_lv
	ATK_NONE = 0, ATK_LUCKY = 1, ATK_FLEE = 2, ATK_MISS = 3, ATK_BLOCK = 4, ATK_DEF = 5,
	-- "all" slots of the bonus arrays (map.hpp / mob.hpp enums)
	ELE_NEUTRAL = 0, ELE_UNDEAD = 9, ELE_ALL = 10, RC_DEMON = 6, RC_ALL = 12, SZ_ALL = 3, CLASS_ALL = 6,
	-- weapon types (status.hpp / mmo.hpp)
	W_FIST = 0, W_DAGGER = 1, W_1HSWORD = 2, W_2HSWORD = 3, W_1HSPEAR = 4, W_2HSPEAR = 5, W_1HAXE = 6, W_2HAXE = 7,
	W_MACE = 8, W_2HMACE = 9, W_BOW = 11, W_MUSICAL = 13, W_WHIP = 14, W_KATAR = 16,
	W_REVOLVER = 17, W_RIFLE = 18, W_GATLING = 19, W_SHOTGUN = 20, W_GRENADE = 21,
	-- storage widths of the legacy integer fields the pipeline narrows into
	DEF_MIN = -32768, DEF_MAX = 32767, -- defType is `int16` in renewal (config/const.hpp; it is `signed char` in pre-renewal)
	-- pc.hpp: indexed_bonus.weapon_damage_rate has 16 slots but is indexed by weapon type (up to 23)
	WDR_SLOTS = 16,
}
