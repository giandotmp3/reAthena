--!nonstrict
-- Legacy numeric identities used by the weapon pipeline (values copied from the legacy headers; the golden
-- data records them as plain numbers, so they are part of the conformance surface).
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
	ELE_ALL = 10, RC_ALL = 12, SZ_ALL = 3, CLASS_ALL = 6,
	-- weapon types (status.hpp / mmo.hpp)
	W_FIST = 0, W_DAGGER = 1, W_BOW = 11, W_KATAR = 16,
	-- storage widths of the legacy integer fields the pipeline narrows into
	DEF_MIN = -128, DEF_MAX = 127, -- defType is `signed char` in pre-renewal (config/const.hpp)
}
