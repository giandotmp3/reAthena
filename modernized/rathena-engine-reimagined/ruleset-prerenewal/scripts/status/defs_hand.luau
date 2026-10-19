--!nonstrict
-- Hand-written part of the status definitions: the statements of the PRE stat functions the first-draft generator could not express
-- (unit and weapon dependent branches, local variables, the else-if chains of the ASPD potions, status_calc_pseudobuff_matk).
-- Same conventions as defs/*.luau: `at` is the status.cpp line, `s` the owner's named fields, `e` the environment.
local status = import("scripts/status/define")
local K = import("scripts/status/consts")
local O = import("scripts/status/ops")

-- ---- watk (status.cpp:7380-7390): Nibelungen adds its ATK to non-players always, to players only when the weapon in the hand being
-- calculated (lr_flag selects the left hand) is a level 4 weapon.
status.extend("NIBELUNGEN", { contributes = { watk = {
	{ at = 7380, op = "add",
	  when = function(s, e)
		if e.unit.type ~= "pc" then return false end -- 7381-7382: non-players are the generated entry (defs/defs_m_r.luau)
		local wlv = e.unit.lr_flag and e.unit.wlv_l or e.unit.wlv_r -- 7385
		return wlv == 4 -- 7387 (wlv 0 = no weapon in that hand)
	  end,
	  v = function(s, e) return s.atk_inc end },
} } })

-- ---- flee (status.cpp:7635-7642): GvG / battleground penalty of players, applied before any status is looked at
status.rule("flee", { at = 7639, stage = "pre", op = "mul_pct",
	when = function(s, e) return e.unit.type == "pc" and e.unit.map == "gvg" end,
	v = function(s, e) return -e.cfg.gvg_flee_penalty end })
status.rule("flee", { at = 7641, stage = "pre", op = "mul_pct",
	when = function(s, e) return e.unit.type == "pc" and e.unit.map == "bg" end, -- (not GvG: else-if)
	v = function(s, e) return -e.cfg.bg_flee_penalty end })

-- ---- aspd_rate (status.cpp:8427-8559)
-- Assassin Cross of Sunset joins the quicken family unless a player wields a ranged weapon (8474-8489)
local RANGED = { [K.W_BOW] = true, [K.W_REVOLVER] = true, [K.W_RIFLE] = true, [K.W_GATLING] = true, [K.W_SHOTGUN] = true, [K.W_GRENADE] = true }
status.extend("ASSNCROS", { contributes = { aspd_rate = {
	{ at = 8474, op = "gmax", group = "quicken",
	  when = function(s, e) return e.unit.type ~= "pc" or not RANGED[e.unit.weapon] end,
	  v = function(s, e) return s.aspd_rate_quicken end },
} } })
-- ASPD potions: else-if chain Potion3 > 2 > 1 > 0, `aspd_rate -= val2` of the first one found (8497-8501)
local POTIONS = { "ASPDPOTION3", "ASPDPOTION2", "ASPDPOTION1", "ASPDPOTION0" }
for i, name in ipairs(POTIONS) do
	status.extend(name, { contributes = { aspd_rate = {
		{ at = 8501, op = "add",
		  when = function(s, e)
			for j = 1, i - 1 do
				if e.sc[POTIONS[j]] ~= nil then return false end
			end
			return true
		  end,
		  v = function(s, e) return -s.aspd_rate_dec end },
	} } })
end

-- ---- matk: status_calc_pseudobuff_matk (status.cpp:7452-7500), the flat MATK of skills and statuses
status.rule("matk", { at = 7454, op = "add",
	when = function(s, e) return e.skill("NV_TRANSCENDENCE") > 0 end,
	v = function(s, e)
		local lv = e.skill("NV_TRANSCENDENCE")
		return 15 * lv + (lv > 4 and 25 or 0)
	end })
local function matk(name, line, field, fn)
	status.extend(name, { contributes = { matk = { { at = line, op = "add", v = fn or function(s, e) return s[field] end } } } })
end
matk("DORAM_MATK", 7471, "matk_inc")
matk("AQUAPLAY_OPTION", 7473, "matk_inc")
matk("CHILLY_AIR_OPTION", 7475, "matk_inc")
matk("COOLER_OPTION", 7477, "matk_inc")
matk("SOULFAIRY", 7479, "matk_inc")
matk("INSPIRATION", 7481, "atk_inc")
matk("SHIELDSPELL_ATK", 7483, "atk_inc")
matk("ZANGETSU", 7485, "matk_inc")
matk("MOONLITSERENADE", 7487, "matk_inc")
matk("_AUTOSHADOWSPELL", 7489, "matk_inc")
matk("ODINS_POWER", 7491, nil, function(s, e) return 40 + 30 * s.lv end)
matk("IZAYOI", 7493, nil, function(s, e) return 25 * s.lv end)
status.extend("FIRE_INSIGNIA", { contributes = { matk = { { at = 7495, op = "add",
	when = function(s, e) return s.level == 3 end, v = 50 } } } })
status.extend("CLIMAX_DES_HU", { contributes = { matk = { { at = 7497, op = "add", v = 100 } } } })

-- ---- status_get_hpbonus / status_get_spbonus, the player part (status.cpp:3178-3196, 3338-3360, 3366-3376): item bonus and skills.
-- (Generated rules cover the Summoner flat bonuses at 3184 / 3186 / 3348 / 3350.)
local function pc_rule(attr, at, v, when)
	status.rule(attr, { at = at, op = "add",
		when = function(s, e)
			if e.unit.type ~= "pc" then return false end
			return when == nil or when(e)
		end,
		v = v })
end
local function lv(e, name) return e.skill(name) end
local function sea_power(e)
	return e.skill("SU_TUNABELLY") + e.skill("SU_TUNAPARTY") + e.skill("SU_BUNCHOFSHRIMP") + e.skill("SU_FRESHSHRIMP")
		+ e.skill("SU_GROOMING") + e.skill("SU_PURRING") + e.skill("SU_SHRIMPARTY") -- pc_checkskill_summoner(sd, SUMMONER_POWER_SEA)
end
pc_rule("hp_fix", 3180, function(s, e) return e.unit.bonus_hp end) -- sd->bonus.hp
pc_rule("hp_fix", 3182, function(s, e) return 200 * lv(e, "CR_TRUST") end, function(e) return lv(e, "CR_TRUST") > 0 end)
pc_rule("hp_fix", 3188, 3000, function(e) return e.skill("SU_POWEROFSEA") > 0 and sea_power(e) >= 20 end)
pc_rule("hp_fix", 3191, function(s, e) local l = lv(e, "NV_BREAKTHROUGH") return 350 * l + (l > 4 and 250 or 0) end, function(e) return lv(e, "NV_BREAKTHROUGH") > 0 end)
pc_rule("hp_fix", 3193, function(s, e) local l = lv(e, "NV_TRANSCENDENCE") return 350 * l + (l > 4 and 250 or 0) end, function(e) return lv(e, "NV_TRANSCENDENCE") > 0 end)
pc_rule("sp_fix", 3340, function(s, e) return e.unit.bonus_sp end) -- sd->bonus.sp
pc_rule("sp_fix", 3342, function(s, e) return 30 * lv(e, "SL_KAINA") end, function(e) return lv(e, "SL_KAINA") > 0 end)
pc_rule("sp_fix", 3344, function(s, e) return 200 + 20 * lv(e, "RA_RESEARCHTRAP") end, function(e) return lv(e, "RA_RESEARCHTRAP") > 0 end)
pc_rule("sp_fix", 3346, function(s, e) return 30 * lv(e, "WM_LESSON") end, function(e) return lv(e, "WM_LESSON") > 0 end)
pc_rule("sp_fix", 3352, 300, function(e) return e.skill("SU_POWEROFSEA") > 0 and sea_power(e) >= 20 end)
pc_rule("sp_fix", 3355, function(s, e) local l = lv(e, "NV_BREAKTHROUGH") return 30 * l + (l > 4 and 50 or 0) end, function(e) return lv(e, "NV_BREAKTHROUGH") > 0 end)
pc_rule("sp_fix", 3357, function(s, e) local l = lv(e, "NV_TRANSCENDENCE") return 30 * l + (l > 4 and 50 or 0) end, function(e) return lv(e, "NV_TRANSCENDENCE") > 0 end)
pc_rule("sp_rate", 3385, function(s, e) return lv(e, "HP_MEDITATIO") end, function(e) return lv(e, "HP_MEDITATIO") > 0 end)
pc_rule("sp_rate", 3387, function(s, e) return 2 * lv(e, "HW_SOULDRAIN") end, function(e) return lv(e, "HW_SOULDRAIN") > 0 end)

-- ---- speed, GetSpeed() (status.cpp:8229-8243): the parts of the tail that depend on the unit or compare with a constant
status.rule("speed", { at = 8231, op = "mul_pct", -- speed += speed * (50 - 5 * MC_PUSHCART) / 100 for a player pushing a cart (NEW_CARTS: Push Cart status)
	when = function(s, e) return e.unit.type == "pc" and e.sc.PUSH_CART ~= nil end,
	v = function(s, e) return 50 - 5 * e.skill("MC_PUSHCART") end })
status.extend("DEFENDER", { contributes = { speed = { { at = 8239, op = "max", v = 200 } } } }) -- speed = max(speed, 200)
status.extend("ARMOR", { contributes = { speed = { { at = 8241, op = "max", v = 200 } } } })
