# Status value fields: val1..val4 named per status (open question Q-F)

1276 value slots in 319 statuses: 300 are read by the covered stat code (named by role), 789 are `valN_unused` (set by the legacy, but read only by the combat, skill or status-start code outside this scope: battle.cpp, skill.cpp, the rest of status.cpp), the others are named from their legacy comment or a curated name (flags, enums, packed values). Where the legacy comment is misleading it is noted below the table.

The status database never names `val1..val4`: their meaning lives in `src/map/status.cpp` (`status_change_start` sets them, the `status_calc_*`
functions read them). For the 319 statuses the stat functions of the pre-renewal branch know, the package names them
(`scripts/status/defs/*.luau`, `fields = { val1 = ..., }`; a status instance carries both the names and `val1..val4`). The save format can adopt
the names status by status; until then `val1..val4` stay the persisted keys.

* **Name**: the Luau field name. Rule: `<attribute>_<inc|dec|pct_inc|pct_dec|set>` for a value added / subtracted / applied as a percentage to the
  attribute(s) named, `lv` for the skill level that other values derive from, a short curated name for flags, enums and packed values
  (`str_agi_vit_packed` = three bytes `(v >> 16) & 0xFF`, `(v >> 8) & 0xFF`, `v & 0xFF`), `valN_unused` when the covered stat code does not read it.
* **Read at**: `status.cpp:line` of the contribution that reads the value (attribute in front); the lines are also the `at =` keys of the definitions.
* **Set at**: the line in `status_change_start` (and the expression and comment of the legacy) where the value is assigned; `-` when the value comes
  straight from the caller (`sc_start4(..., val1, val2, val3, val4)`) or is set in a path this extraction does not follow.
* Statuses marked **not in PRE db** are known to the stat code but absent from `db/pre-re/status.yml`: a pre-renewal server never starts them.

Read by code other than the stat functions (`scripts/status/states.luau`, `chain.luau`, `scripted.luau`): `GOSPEL.val4`, `BASILICA.val4`, `GRAVITATION.val3`,
`CAMOUFLAGE.val1`, `MAGNETICFIELD.val2`, `FEAR.val2`, `DANCING.val1/val4`, `OBLIVIONCURSE.val3`, `NOCHAT.val1` (states, status.cpp:5606-5850), `MAGICPOWER.val3/val4`
(`status_calc_bl_main`, 6268), every `getstatus(SC_X, n)` of a status script (`scripts/status/scripted.luau`).

| Status | sc id | val | Name | Read at (status.cpp) | Set at (status_change_start) |
|---|---|---|---|---|---|
| ABYSS_SLAYER *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_smatk_inc` | 8579 patk, 8616 smatk | 12907: `val2 = 10 + 2 * val1` // PAtk/SMatk Increase |
|  |  | 3 | `hit_inc` | 7619 hit | 12908: `val3 = 100 + 20 * val1` // Hit Increase |
|  |  | 4 | `val4_unused` | - | - |
| ACCELERATION | 349 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ADJUSTMENT | 229 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ADORAMUS | 324 | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 6882 agi | 10875: `val2 = 2 + val1` // Agi change |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ADRENALINE | 43 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `aspd_rate_quicken` | 8456 aspd_rate | 11620: `val3 = (val2) ? 5 * val1 : 5` // Power increase |
|  |  | 4 | `val4_unused` | - | - |
| ADRENALINE2 | 145 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `aspd_rate_quicken` | 8452 aspd_rate | 11620: `val3 = (val2) ? 5 * val1 : 5` // Power increase |
|  |  | 4 | `val4_unused` | - | - |
| AGIUP | 952 | 1 | `speed_haste` | 8169 speed | - |
|  |  | 2 | `flee_pct_inc` | 7697 flee | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| AIN_RHAPSODY *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `res_dec` | 8661 res | 12920: `val2 = 10 * val1` // Res/MRes Decrease |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ALL_RIDING | 592 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ALL_STAT_DOWN *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 6835 str, 6908 agi, 6969 vit, 7047 int, 7122 dex, 7183 luk | 11236: `val2 = 20 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ANALYZE | 353 | 1 | `lv` | 7834 def, 7920 def2, 7980 mdef, 8023 mdef2 | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ANGELUS | 29 | 1 | `lv` | - | - |
|  |  | 2 | `def_pct_inc` | 7904 def2 | 11642: `val2 = 5*val1` // def increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ANGRIFFS_MODUS | 546 | 1 | `lv` | 3256 hp_rate, 7810 def | - |
|  |  | 2 | `atk_val` | 7334 batk, 7424 watk | 12401: `val2 = 50 + 20 * val1` // atk bonus |
|  |  | 3 | `flee_dec` | 7665 flee | 12402: `val3 = 25 + 10 * val1` // Flee reduction. |
|  |  | 4 | `drain_timer` | - | 12403: `val4 = tick/1000` // hp/sp reduction timer |
| APPLEIDUN | 176 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3234 hp_rate | 12525: `val2 = (5 + 2 * val1) + (status_get_vit(src) / 10)` // HP Rate: (5 + 2 * skill_lv) + (vit/10) + (BA_MUSICALLESSON level) |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| AQUAPLAY_OPTION | 487 | 1 | `val1_unused` | - | - |
|  |  | 2 | `matk_inc` | 7473 matk | 12231: `val2 = 40` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ARCLOUSEDASH | 634 | 1 | `lv` | - | - |
|  |  | 2 | `stat_inc` | 6898 agi | 12644: `val2 = 15 + 5 * val1` // AGI |
|  |  | 3 | `speed_haste` | 8203 speed | 12645: `val3 = 25` // Move speed increase |
|  |  | 4 | `ranged_atk_bonus` | - | 12647: `val4 = 10` // Ranged ATK increase if the target is a Doram |
| ARCWANDCLAN | 660 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ARMOR | 104 | 1 | `val1_unused` | - | - |
|  |  | 2 | `damage_divisor` | - | 11760: `val2 = 8` // Damage will be divided by this value |
|  |  | 3 | `range` | - | 11762: `val3 = BF_LONG` // Range |
|  |  | 4 | `type` | - | 11763: `val4 = BF_WEAPON/BF_MISC` // Type |
| ASH | 549 | 1 | `val1_unused` | - | - |
|  |  | 2 | `hit_pct_dec` | 7599 hit | 12465: `val2 = 0` // hit % reduc |
|  |  | 3 | `def_def2_pct_dec` | 7852 def, 7922 def2 | 12466: `val3 = 0` // def % reduc |
|  |  | 4 | `batk_flee_pct_dec` | 7330 batk, 7723 flee | 12467: `val4 = 0` // atk flee % reduc |
| ASPDPOTION0 | 55 | 1 | `aspd_inc` | - | - |
|  |  | 2 | `aspd_rate_dec` | 8501 aspd_rate | 11156: `val2 = 50*(2+type-SC_ASPDPOTION0)` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ASPDPOTION1 | 56 | 1 | `aspd_inc` | - | - |
|  |  | 2 | `aspd_rate_dec` | 8501 aspd_rate | 11156: `val2 = 50*(2+type-SC_ASPDPOTION0)` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ASPDPOTION2 | 57 | 1 | `aspd_inc` | - | - |
|  |  | 2 | `aspd_rate_dec` | 8501 aspd_rate | 11156: `val2 = 50*(2+type-SC_ASPDPOTION0)` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ASPDPOTION3 | 58 | 1 | `aspd_inc` | - | - |
|  |  | 2 | `aspd_rate_dec` | 8501 aspd_rate | 11156: `val2 = 50*(2+type-SC_ASPDPOTION0)` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ASPERSIO | 37 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ASSNCROS | 174 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8474 aspd_rate, 8476 aspd_rate | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ATTACK_STANCE *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `def_dec` | 7866 def | 12855: `val2 = 40 * val1` // DEF Decrease |
|  |  | 3 | `patk_smatk_inc` | 8585 patk, 8622 smatk | 12856: `val3 = 3 * val1` // P.ATK/S.MATK Increase |
|  |  | 4 | `val4_unused` | - | - |
| AVOID | 241 | 1 | `lv` | - | - |
|  |  | 2 | `speed_haste` | 8185 speed | 11696: `val2 = 40 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BANANA_BOMB | 430 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BANDING | 383 | 1 | `lv` | 7840 def | - |
|  |  | 2 | `def_cond` | 7840 def | 12184: `val2 = (sd ? skill_banding_count(sd) : 1)` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BATTLEORDERS | 152 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BENEDICTIO | 38 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BENEDICTUM *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `pow_con_crt_inc` | 7202 pow, 7282 con, 7302 crt | 12879: `val2 = 2 * val1` // Trait Stats Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BERSERK | 112 | 1 | `lv` | - | - |
|  |  | 2 | `no_hp_loss` | - | 10714: `val2 = 0` // Mark to not lose hp |
|  |  | 3 | `skill_duration` | - | 11386: `val3 = tick/val4` // val3 holds skill duration |
|  |  | 4 | `val4_unused` | - | 11384: `val4 = skill_get_time2(scdb->skill_id,val1)` |
| BEYONDOFWARCRY | 409 | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 6813 str | 12139: `val2 = 10 + 10 * val1` // STR Reduction |
|  |  | 3 | `cri_maxhp_bonus_val` | 3282 hp_rate, 7551 cri | 12140: `val3 = 4 * val1` // MaxHP Reduction |
|  |  | 4 | `val4_unused` | - | - |
| BLAST_OPTION | 495 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | 12245: `val2 = 20` |
|  |  | 3 | `element_change` | - | 12246: `val3 = ELE_WIND` // Change into wind element. |
|  |  | 4 | `val4_unused` | - | - |
| BLESSING | 30 | 1 | `lv` | - | - |
|  |  | 2 | `stat_inc` | 6800 str, 6802 str, 7001 int, 7003 int, 7083 dex, 7085 dex | 11590: `val2 = val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BLESSING_OF_M_CREATURES *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_smatk_inc` | 8594 patk, 8631 smatk | 13023: `val2 = val1 * 10` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BLIND | 8 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| BURNING | 310 | 1 | `lv` | - | 10299: `val1 = val1` |
|  |  | 2 | `val2_unused` | - | 10300: `val2 = val2` |
|  |  | 3 | `val3_unused` | - | 10301: `val3 = val3` |
|  |  | 4 | `val4_unused` | - | 10302: `val4 = val4` |
| B_TRAP | 578 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | 12585: `val2 = src->id` |
|  |  | 3 | `speed_slow` | 8146 speed | 12586: `val3 = val1 * 25` // -movespeed TODO: Figure out movespeed rate |
|  |  | 4 | `val4_unused` | - | - |
| CAMOUFLAGE | 348 | 1 | `lv` | 8132 speed | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `def_def2_pct_dec_arg` | 7844 def, 7928 def2 | - |
|  |  | 4 | `val4_unused` | - | 11971: `val4 = tick/1000` |
| CARTBOOST | 123 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CATNIPPOWDER | 631 | 1 | `lv` | - | - |
|  |  | 2 | `watk_pct_inc` | 7430 watk | 12630: `val2 = -50` // WATK%, MATK% |
|  |  | 3 | `speed_slow` | 8148 speed | 12631: `val3 = 25 * val1` // Move speed reduction |
|  |  | 4 | `val4_unused` | - | 12633: `val4 = status_get_lv(src) / 12` |
| CHANGE | 242 | 1 | `lv` | - | - |
|  |  | 2 | `vit_inc` | 6933 vit | 11751: `val2 = 30*val1` // Vit increase |
|  |  | 3 | `int_inc` | 6994 int | 11752: `val3 = 20*val1` // Int increase |
|  |  | 4 | `val4_unused` | - | - |
| CHANGEUNDEAD | 128 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CHASEWALK | 124 | 1 | `lv` | - | - |
|  |  | 2 | `sp_interval` | - | 11280: `val2 = tick>0?tick:10000` // Interval at which SP is drained. |
|  |  | 3 | `speed_value` | 8086 speed, 8095 speed, 8099 speed, 8102 speed, 8104 speed, 8106 speed, 8108 speed, 8110 speed, 8112 speed, 8114 speed, 8116 speed, 8118 speed, 8120 speed, 8122 speed, 8124 speed, 8126 speed, 8128 speed, 8130 speed, 8132 speed, 8134 speed, 8136 speed, 8138 speed, 8140 speed, 8142 speed, 8144 speed, 8146 speed, 8148 speed, 8150 speed, 8152 speed, 8154 speed, 8156 speed | 11281: `val3 = 35 - 5 * val1` // Speed adjustment. |
|  |  | 4 | `sp_cost` | - | 11284: `val4 = 10+val1*2` // SP cost. |
| CHASEWALK2 | 596 | 1 | `stat_inc` | 6785 str | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CHATTERING | 679 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_inc` | 7432 watk | 12705: `val2 = 100` // eATK, eMATK |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CHEERUP | 685 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CHILLY_AIR_OPTION | 491 | 1 | `val1_unused` | - | - |
|  |  | 2 | `matk_inc` | 7475 matk | 12238: `val2 = 120` // Matk. Renewal (Matk1) |
|  |  | 3 | `val3_unused` | - | 12239: `val3 = MG_COLDBOLT` |
|  |  | 4 | `val4_unused` | - | - |
| CLIMAX_CRYIMP *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CLIMAX_DES_HU *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CLOAKING | 25 | 1 | `lv` | 8116 speed, 8179 speed | 11294: `val1 = 10` |
|  |  | 2 | `sp_interval` | - | 11289: `val2 = tick>0?tick:10000` // SP consumption interval |
|  |  | 3 | `unused` | - | 11297: `val3 = 0` // Unused, previously walk speed adjustment |
|  |  | 4 | `mode_flags` | 8116 speed, 8179 speed | - |
| CLOAKINGEXCEED | 364 | 1 | `lv` | - | - |
|  |  | 2 | `hit_count` | - | 11911: `val2 = (val1 + 1) / 2` // Hits |
|  |  | 3 | `speed_haste` | 8189 speed | 11912: `val3 = (val1 - 1) * 10` // Walk speed |
|  |  | 4 | `val4_unused` | - | - |
| CLOSECONFINE | 162 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `flee_inc` | 7663 flee | - |
|  |  | 4 | `val4_unused` | - | - |
| COMPETENTIA *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_smatk_inc` | 8577 patk, 8614 smatk | 12875: `val2 = 10 * val1` // PAtk/SMatk Increase - Unconfirmed if this is official formula but its 50 at Lv 5. [Rytech] |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CONCENTRATE | 23 | 1 | `lv` | - | - |
|  |  | 2 | `stat_pct` | 6858 agi, 7070 dex | 11599: `val2 = 2 + val1` |
|  |  | 3 | `agi_card_bonus` | 6858 agi | 11601: `val3 = sd->indexed_bonus.param_bonus[1]` // Agi |
|  |  | 4 | `dex_card_bonus` | 7070 dex | 11602: `val4 = sd->indexed_bonus.param_bonus[4]` // Dex |
| CONCENTRATION | 110 | 1 | `lv` | - | - |
|  |  | 2 | `atk_pct` | - | 11635: `val2 = 5*val1` // Batk/Watk Increase |
|  |  | 3 | `hit_inc` | 7577 hit | 11638: `val3 = 10*val1` // Hit Increase |
|  |  | 4 | `def_pct_dec` | 7822 def, 7906 def2 | 11636: `val4 = 5*val1` // Def reduction |
| COOLER_OPTION | 489 | 1 | `val1_unused` | - | - |
|  |  | 2 | `matk_inc` | 7477 matk | 12234: `val2 = 80` |
|  |  | 3 | `element_change` | - | 12235: `val3 = ELE_WATER` // Change into water element. |
|  |  | 4 | `val4_unused` | - | - |
| CREATINGSTAR | 718 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12762: `val4 = tick / tick_time` |
| CROSSBOWCLAN | 662 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CRYSTAL_ARMOR_OPTION *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CURSE | 5 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| CURSED_SOIL_OPTION | 501 | 1 | `val1_unused` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3262 hp_rate | 12259: `val2 = 10` // HP rate bonus |
|  |  | 3 | `element_change` | - | 12260: `val3 = ELE_EARTH` // Change into earth element. |
|  |  | 4 | `val4_unused` | - | - |
| C_MARKER | 576 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `flee_dec` | 7683 flee | 12592: `val3 = 10` // -10 flee |
|  |  | 4 | `val4_unused` | - | 12594: `val4 = tick/1000` |
| DANCEWITHWUG | 405 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `aspd_rate_dec_arg` | 8543 aspd_rate | 12128: `val3 = 5 * val1` // ASPD Increase |
|  |  | 4 | `fixed_cast_time` | - | 12129: `val4 = 20 + 10 * val1` // Fixed Cast Time Reduction |
| DANCING | 164 | 1 | `lv` | - | 13230: `val1 = val1` |
|  |  | 2 | `val2_unused` | - | 13231: `val2 = val2` |
|  |  | 3 | `tick_duration` | - | 11093: `val3 = tick/1000` // Tick duration |
|  |  | 4 | `ensemble_flag` | - | 13198: `val4 = vd->look[LOOK_CLOTHES_COLOR]` |
| DECREASEAGI | 33 | 1 | `lv` | - | - |
|  |  | 2 | `agi_dec` | 6870 agi | 10875: `val2 = 2 + val1` // Agi change |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DEFENCE | 246 | 1 | `lv` | - | - |
|  |  | 2 | `stat_def_inc` | 6959 vit, 7802 def | 11705: `val2 = 2 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DEFENDER | 80 | 1 | `lv` | - | - |
|  |  | 2 | `damage_reduction` | - | 11338: `val2 = 5 + 15*val1` // Damage reduction |
|  |  | 3 | `unused` | - | 11339: `val3 = 0` // Unused, previously speed adjustment |
|  |  | 4 | `aspd_rate_inc` | 8515 aspd_rate | 11340: `val4 = 250 - 50*val1` // Aspd adjustment |
| DEFSET | 556 | 1 | `def_set` | 7793 def, 7892 def2 | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DELUGE | 101 | 1 | `val1_unused` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3236 hp_rate | 11049: `val2 = deluge_eff[i]` // HP increase |
|  |  | 3 | `val3_unused` | - | 11054: `val3 = enchant_eff[i]` |
|  |  | 4 | `val4_unused` | - | - |
| DONTFORGETME | 179 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_inc` | 8504 aspd_rate | - |
|  |  | 3 | `speed_slow` | 8106 speed | - |
|  |  | 4 | `val4_unused` | - | - |
| DORAM_FLEE2 | 682 | 1 | `flee2_inc` | 7757 flee2 | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DORAM_MATK | 681 | 1 | `matk_inc` | 7471 matk | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DORAM_WALKSPEED | 680 | 1 | `speed_haste` | 8205 speed | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DPOISON | 10 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| DRUMBATTLE | 168 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_inc` | 7367 watk | - |
|  |  | 3 | `def_inc` | 7796 def | - |
|  |  | 4 | `val4_unused` | - | - |
| D_MACHINE *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `def_inc` | 7860 def | 12889: `val2 = 200 + 50 * val1` // DEF Increase |
|  |  | 3 | `res_inc` | 8653 res | 12890: `val3 = 20 * val1` // Res Increase |
|  |  | 4 | `val4_unused` | - | - |
| EARTHWEAPON | 99 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| EARTH_INSIGNIA | 517 | 1 | `level` | 3202 hp_fix, 3366 sp_fix, 7412 watk, 7806 def, 7966 mdef, 8868 atk_element | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12573: `val4 = tick / tick_time` |
| ECHOSONG | 397 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `def_inc` | 7842 def | 12084: `val3 = 6 * val1 + val2 + (sd?sd->status.job_level:50) / 4` // DEF Increase |
|  |  | 4 | `val4_unused` | - | - |
| ELEMENTALCHANGE | 165 | 1 | `element_lv` | 8838 element_lv | 10965: `val1 = 1 + rnd()%4` |
|  |  | 2 | `element` | 8810 element | 10962: `val2 = rnd()%ELE_ALL` |
|  |  | 3 | `unused` | - | 10968: `val3 = 0` // Not need to keep this info. |
|  |  | 4 | `val4_unused` | - | - |
| EMERGENCY_MOVE *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `speed_haste` | 8209 speed | 12726: `val2 = 25` // Movement speed increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ENCHANTARMS | 250 | 1 | `element` | 8862 atk_element | 11768: `val1 = val1%ELE_ALL` |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ENCPOISON | 26 | 1 | `lv` | - | - |
|  |  | 2 | `poison_chance` | - | 10956: `val2 = 250+50*val1` // Poisoning Chance (2.5+0.5%) in 1/10000 rate |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ENDURE | 21 | 1 | `lv` | 7968 mdef | - |
|  |  | 2 | `hit_count` | - | 10878: `val2 = 7` // Hit-count [Celest] |
|  |  | 3 | `permanent_flag` | 7968 mdef | - |
|  |  | 4 | `card_flag` | 7968 mdef | - |
| EPICLESIS | 325 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3242 hp_rate | 12543: `val2 = 5 * val1` // HP rate bonus |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| EQC | 544 | 1 | `lv` | - | - |
|  |  | 2 | `def2_pct_dec` | 7926 def2 | 12461: `val2 = 5 * val1` // def % reduc |
|  |  | 3 | `maxhp_bonus_dec` | 3286 hp_rate | 12462: `val3 = 2 * val1` // HP drain % |
|  |  | 4 | `val4_unused` | - | - |
| ETERNALCHAOS | 167 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| EXPLOSIONSPIRITS | 92 | 1 | `lv` | - | - |
|  |  | 2 | `cri_inc` | 7531 cri | 11149: `val2 = 75 + 25*val1` // Cri bonus |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| EYES_OF_STORM_OPTION *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FEAR | 309 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FIGHTINGSPIRIT | 322 | 1 | `watk_inc` | 7393 watk | - |
|  |  | 2 | `fix_aspd_dec` | 8407 fix_aspd | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FIREWEAPON | 96 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FIRE_INSIGNIA | 514 | 1 | `level` | 7407 watk, 7412 watk, 7495 matk, 8871 atk_element | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12573: `val4 = tick / tick_time` |
| FIRM_FAITH *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3276 hp_rate | 12834: `val2 = 2 * val1` // MaxHP Increase |
|  |  | 3 | `res_inc` | 8651 res | 12835: `val3 = 8 * val1` // Res Increase |
|  |  | 4 | `val4_unused` | - | - |
| FLAMEARMOR_OPTION *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FLASHCOMBO | 569 | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7428 watk | 12507: `val2 = 20 * val1 + 20` // atk bonus |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FLEET | 244 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8468 aspd_rate | 11718: `val2 = 30*val1` // Aspd change |
|  |  | 3 | `atk_pct_change` | - | 11719: `val3 = 5+5*val1` // bAtk/wAtk rate change |
|  |  | 4 | `val4_unused` | - | - |
| FLING | 240 | 1 | `lv` | - | - |
|  |  | 2 | `def_pct_dec` | 7830 def | 11677: `val2 = 0` // No armor reduction to players. |
|  |  | 3 | `def2_pct_dec` | 7918 def2 | 11680: `val3 = 5*val1` // Def2 reduction |
|  |  | 4 | `val4_unused` | - | - |
| FORCEOFVANGUARD | 377 | 1 | `lv` | 3248 hp_rate | - |
|  |  | 2 | `chance` | - | 12151: `val2 = 8 + 12 * val1` // Chance |
|  |  | 3 | `max_rage` | - | 12152: `val3 = 5 + 2 * val1` // Max rage counters |
|  |  | 4 | `val4_unused` | - | - |
| FORTUNE | 180 | 1 | `val1_unused` | - | - |
|  |  | 2 | `cri_inc` | 7533 cri | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FREEZE | 1 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `petrify_time` | - | 10273: `val3 = std::max<int32>(1, tick - delay)` // Petrify time |
|  |  | 4 | `val4_unused` | - | - |
| FREEZING | 311 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| FRIGG_SONG | 565 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3244 hp_rate | 12501: `val2 = 5 * val1` // maxhp bonus |
|  |  | 3 | `healing` | - | 12502: `val3 = 80 + 20 * val1` // healing |
|  |  | 4 | `val4_unused` | - | 12504: `val4 = tick / tick_time` |
| FULL_THROTTLE | 559 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | 12477: `val2 = ( val1 == 1 ? 6 : 6 - val1 )` |
|  |  | 3 | `stat_pct_inc` | 6825 str, 6896 agi, 6957 vit, 7030 int, 7112 dex, 7173 luk | 12478: `val3 = 20` // +% AllStats |
|  |  | 4 | `val4_unused` | - | 12480: `val4 = tick / tick_time` |
| FUSION | 220 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GATLINGFEVER | 231 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8464 aspd_rate | 11670: `val2 = 20*val1` // Aspd increase |
|  |  | 3 | `batk_inc` | 7325 batk | 11671: `val3 = 20+10*val1` // Atk increase |
|  |  | 4 | `flee_dec` | 7671 flee | 11672: `val4 = 5*val1` // Flee decrease |
| GEF_NOCTURN *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `mres_dec` | 8685 mres | 12920: `val2 = 10 * val1` // Res/MRes Decrease |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GHOSTWEAPON | 146 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GIANTGROWTH | 318 | 1 | `val1_unused` | - | - |
|  |  | 2 | `stat_inc` | 6811 str | 11881: `val2 = 30` // Damage success rate and STR increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GLOOMYDAY | 402 | 1 | `lv` | - | - |
|  |  | 2 | `flee_pct_dec` | 7713 flee | 12107: `val2 = 20 + 5 * val1` // Flee reduction. |
|  |  | 3 | `aspd_rate_inc_arg` | 8545 aspd_rate | 12108: `val3 = 15 + 5 * val1` // ASPD reduction. |
|  |  | 4 | `speed_cond` | 8104 speed | 12110: `val4 = 1` // Reduce walk speed by half. |
| GLORIA | 41 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GLORYWOUNDS | 509 | 1 | `stat_inc` | 6935 vit | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GN_CARTBOOST | 421 | 1 | `val1_unused` | - | - |
|  |  | 2 | `speed_haste` | 8195 speed | 12049: `val2 = 50` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GOLDENE_FERSE | 545 | 1 | `lv` | - | - |
|  |  | 2 | `flee_pct_inc` | 7725 flee | 12407: `val2 = 10 + 10*val1` // flee bonus |
|  |  | 3 | `aspd_rate_dec_arg` | 8553 aspd_rate | 12408: `val3 = 6 + 4 * val1` // Aspd Bonus |
|  |  | 4 | `holy_chance` | - | 12409: `val4 = 2 + 2 * val1` // Chance of holy attack |
| GOLDENE_TONE *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `res_mres_inc` | 8657 res, 8681 mres | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GOLDENMACECLAN | 661 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GOSPEL | 114 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | 11392: `val2 = tick/10000` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `target_type` | 8118 speed, 8517 aspd_rate | - |
| GRAVITATION | 155 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_inc` | 8520 aspd_rate | 11468: `val2 = 50*val1` // aspd reduction |
|  |  | 3 | `target_type` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GROOMING | 677 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee_inc` | 7733 flee | 12702: `val2 = 100` // Flee |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GROUNDGRAVITY *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| GT_CHANGE | 419 | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7399 watk | 12209: `val2 = val1 * 8` // ATK increase |
|  |  | 3 | `aspd_rate_dec_arg` | 8547 aspd_rate | 12210: `val3 = status->agi * val1 / 60` // ASPD increase: [(Target AGI x Skill Level) / 60] % |
|  |  | 4 | `val4_unused` | - | - |
| GT_REVITALIZE | 420 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3254 hp_rate | 12214: `val2 = 2 * val1` // MaxHP: [(Skill Level * 2)]% |
|  |  | 3 | `hp_regen_inc` | - | 12215: `val3 = val1 * 30 + 50` // Natural HP recovery increase: [(Skill Level x 30) + 50] % |
|  |  | 4 | `def2_inc` | - | 12217: `val4 = val1 * 20` // STAT DEF increase |
| GUARD_STANCE *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `def_inc` | 7864 def | 12842: `val2 = 50 + 50 * val1` // DEF Increase |
|  |  | 3 | `watk_dec` | 7440 watk | 12843: `val3 = 50 * val1` // ATK Decrease |
|  |  | 4 | `val4_unused` | - | - |
| GUST_OPTION | 493 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HALLUCINATIONWALK | 365 | 1 | `lv` | - | - |
|  |  | 2 | `flee_inc` | 7677 flee | 11921: `val2 = 50 * val1` // Evasion rate of physical attacks. Flee |
|  |  | 3 | `mdef_evasion` | - | 11922: `val3 = 10 * val1` // Evasion rate of magical attacks. |
|  |  | 4 | `val4_unused` | - | - |
| HALLUCINATIONWALK_POSTDELAY | 366 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HARMONIZE | 398 | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 6779 str, 6854 agi, 6927 vit, 6988 int, 7066 dex, 7141 luk | 12087: `val2 = 5 + 5 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HAWKEYES | 511 | 1 | `stat_inc` | 7076 dex | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HEATER_OPTION | 483 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_inc` | 7416 watk | 12223: `val2 = 120` // Eatk Renewal (Atk2) |
|  |  | 3 | `element_change` | - | 12224: `val3 = ELE_FIRE` // Change into fire element. |
|  |  | 4 | `val4_unused` | - | - |
| HEAT_BARREL | 572 | 1 | `lv` | 8411 fix_aspd | - |
|  |  | 2 | `fixed_casttime` | - | 12607: `val2 = n * 5` // -fixed casttime |
|  |  | 3 | `atk` | - | 12608: `val3 = (6 + val1 * 2) * n` // ATK |
|  |  | 4 | `hit_dec` | 7593 hit | 12609: `val4 = 25 + val1 * 5` // -hit |
| HIDDEN_CARD *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_inc` | 8588 patk | 12988: `val2 = 3 * val1` |
|  |  | 3 | `val3_unused` | - | 12989: `val3 = 10 * val1` |
|  |  | 4 | `val4_unused` | - | - |
| HIDING | 24 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | 11274: `val2 = tick/1000` |
|  |  | 3 | `unused` | - | 11276: `val3 = 0` // Unused, previously speed adjustment |
|  |  | 4 | `sp_interval` | - | 11277: `val4 = val1+3` // Seconds before SP substraction happen. |
| HISS | 675 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee2_inc_arg` | 7755 flee2 | 12698: `val2 = 50` // Perfect Dodge |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HOVERING | 350 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| HUMMING | 178 | 1 | `val1_unused` | - | - |
|  |  | 2 | `hit_inc` | 7575 hit | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ILLUSIONDOPING | 568 | 1 | `val1_unused` | - | - |
|  |  | 2 | `hit_dec` | 7603 hit | 12546: `val2 = 50` // -Hit |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| IMPOSITIO | 35 | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7370 watk | 11645: `val2 = 5*val1` // WATK/MATK increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCALLSTATUS | 187 | 1 | `stat_inc` | 6783 str, 6860 agi, 6931 vit, 6992 int, 7072 dex, 7147 luk | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCASPDRATE | 247 | 1 | `lv` | 8551 aspd_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCDEFRATE | 202 | 1 | `def_pct_inc` | 7804 def | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCFLEE | 196 | 1 | `flee_inc` | 7653 flee | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCFLEERATE | 197 | 1 | `flee_pct_inc` | 7695 flee | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCHIT | 194 | 1 | `hit_inc` | 7571 hit | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCHITRATE | 195 | 1 | `hit_pct_inc` | 7587 hit | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCMHP | 526 | 1 | `maxhp_bonus_inc` | 3200 hp_fix | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCMHPRATE | 198 | 1 | `maxhp_bonus_inc` | 3232 hp_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCMSP | 527 | 1 | `maxsp_bonus_inc` | 3364 sp_fix | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCMSPRATE | 199 | 1 | `maxsp_bonus_inc` | 3401 sp_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCREASEAGI | 32 | 1 | `lv` | - | - |
|  |  | 2 | `agi_inc` | 6866 agi | 10875: `val2 = 2 + val1` // Agi change |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INCREASING | 230 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INFRAREDSCAN | 352 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INSPIRATION | 386 | 1 | `lv` | 3250 hp_rate, 7579 hit | - |
|  |  | 2 | `atk_inc` | 7397 watk, 7481 matk | 12192: `val2 = 40 * val1` // ATK/MATK |
|  |  | 3 | `stat_inc` | 6815 str, 6886 agi, 6945 vit, 7014 int, 7096 dex, 7159 luk | 12193: `val3 = 6 * val1` // All stat bonus |
|  |  | 4 | `val4_unused` | - | 12194: `val4 = tick / 5000` |
| INTENSIVE_AIM *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| INVINCIBLE | 295 | 1 | `val1_unused` | - | - |
|  |  | 2 | `atk_pct_inc` | - | 11843: `val2 = 100` // ATKpercent increase |
|  |  | 3 | `speed_haste` | 8187 speed | 11844: `val3 = 50` // Speed increase |
|  |  | 4 | `aspd_rate_quicken` | 8472 aspd_rate | 11845: `val4 = 700` // ASPD increase |
| IZAYOI | 532 | 1 | `lv` | 7493 matk | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| JAWAII_SERENADE *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `smatk_inc` | 8618 smatk | 12930: `val2 = 3 * val1` // SMatk Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| JOINTBEAT | 129 | 1 | `val1_unused` | - | - |
|  |  | 2 | `break_flags` | 7915 def2, 8114 speed, 8524 aspd_rate, 8526 aspd_rate | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| JUMPINGCLAN | 663 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| KEEPING | 66 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| KYOUGAKU | 531 | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 6819 str, 6890 agi, 6949 vit, 7022 int, 7100 dex, 7163 luk | 12361: `val2 = 2*val1 + rnd()%val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LAUDAAGNUS | 327 | 1 | `lv` | 3266 hp_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LEADERSHIP | 508 | 1 | `stat_inc` | 6789 str | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LERADSDEW | 407 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `maxhp_bonus_inc` | 3246 hp_rate | 12132: `val3 = 2 + 3 * val1 + min(3 * val2, 25)` // MaxHP Increase |
|  |  | 4 | `val4_unused` | - | - |
| LONGING | 157 | 1 | `lv` | 8095 speed | - |
|  |  | 2 | `aspd_rate_inc` | 8510 aspd_rate | 11098: `val2 = 500-100*val1` // Aspd penalty. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LOUD | 48 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LUNARSTANCE | 709 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3272 hp_rate | 12734: `val2 = 2 + val1` // MaxHP Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| LUXANIMA | 743 | 1 | `val1_unused` | - | - |
|  |  | 2 | `storm_blast_chance` | - | 11884: `val2 = 15` // Storm Blast success % |
|  |  | 3 | `maxhp_bonus_maxsp_bonus_inc` | 3274 hp_rate, 3409 sp_rate | 11885: `val3 = 30` // Damage/HP/SP % increase |
|  |  | 4 | `val4_unused` | - | - |
| MADNESSCANCEL | 228 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MAGICPOWER | 118 | 1 | `lv` | - | - |
|  |  | 2 | `uses` | - | 10947: `val2 = 1` // Lasts 1 invocation |
|  |  | 3 | `matk_pct` | - | 10945: `val3 = 5 * val1` // Matk% increase |
|  |  | 4 | `active` | - | 10948: `val4 = 0` // 0 = ready to be used, 1 = activated and running |
| MANDRAGORA | 426 | 1 | `lv` | 7018 int | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MARINE_FESTIVAL *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `pow_con_crt_inc` | 7204 pow, 7284 con, 7304 crt | 13016: `val2 = 2 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MARIONETTE | 126 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `str_agi_vit_packed` | 6805 str, 6876 agi, 6939 vit | 11402: `val3 = 0` |
|  |  | 4 | `int_dex_luk_packed` | 7008 int, 7090 dex, 7153 luk | 11403: `val4 = 0` |
| MARIONETTE2 | 127 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `str_agi_vit_packed` | 6807 str, 6878 agi, 6941 vit | 11426: `val3 = 0` |
|  |  | 4 | `int_dex_luk_packed` | 7010 int, 7092 dex, 7155 luk | 11427: `val4 = 0` |
| MARSHOFABYSS | 334 | 1 | `lv` | - | - |
|  |  | 2 | `stat_pct_dec` | 6884 agi, 7110 dex | 11926: `val2 = 3 * val1` // AGI and DEX Reduction |
|  |  | 3 | `speed_slow` | 8130 speed | 11929: `val3 = 10 * val1` // Movement Speed Reduction |
|  |  | 4 | `val4_unused` | - | - |
| MDEFSET | 557 | 1 | `mdef_set` | 7963 mdef, 8016 mdef2 | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MELODYOFSINK | 408 | 1 | `lv` | - | - |
|  |  | 2 | `stat_dec` | 7016 int | 12135: `val2 = 10 * val1` // INT Reduction. |
|  |  | 3 | `maxsp_bonus_dec` | 3413 sp_rate | 12136: `val3 = 2 + 2 * val1` // MaxSP reduction |
|  |  | 4 | `val4_unused` | - | - |
| MELON_BOMB | 429 | 1 | `val1_unused` | - | - |
|  |  | 2 | `speed_slow` | 8142 speed | - |
|  |  | 3 | `aspd_rate_inc_arg` | 8549 aspd_rate | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_ATKUP | 279 | 1 | `lv` | - | 10778: `val1 = sce->val1` |
|  |  | 2 | `watk_inc` | 7376 watk | 11833: `val2 = 15 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_FLEEUP | 278 | 1 | `lv` | - | 10778: `val1 = sce->val1` |
|  |  | 2 | `flee_inc` | 7675 flee | 11833: `val2 = 15 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_HITUP | 282 | 1 | `lv` | - | 10778: `val1 = sce->val1` |
|  |  | 2 | `hit_inc` | 7585 hit | 11833: `val2 = 15 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_HPUP | 280 | 1 | `lv` | - | 10778: `val1 = sce->val1` |
|  |  | 2 | `maxhp_bonus_inc` | 3240 hp_rate | 11837: `val2 = 5 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_QUICKEN | 283 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8448 aspd_rate | 11078: `val2 = 300` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MERC_SPUP | 281 | 1 | `lv` | - | 10778: `val1 = sce->val1` |
|  |  | 2 | `maxsp_bonus_inc` | 3407 sp_rate | 11837: `val2 = 5 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MINDBREAKER | 130 | 1 | `lv` | - | - |
|  |  | 2 | `matk_increase` | - | 11722: `val2 = 20*val1` // matk increase. |
|  |  | 3 | `mdef2_pct_dec` | 8019 mdef2 | 11723: `val3 = 12*val1` // mdef2 reduction. |
|  |  | 4 | `val4_unused` | - | - |
| MODECHANGE | 177 | 1 | `val1_unused` | - | - |
|  |  | 2 | `mode_set` | - | 10379: `val2 = sc->getSCE(type)->val2` |
|  |  | 3 | `mode_add` | - | - |
|  |  | 4 | `mode_del` | - | - |
| MOONLITSERENADE | 395 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `matk_inc` | 7487 matk | 12081: `val3 = 4 + val1 * 3 + val2 + (sd?sd->status.job_level:50) / 5` |
|  |  | 4 | `val4_unused` | - | - |
| MOON_COMFORT | 218 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee_inc` | 7661 flee | 11659: `val2 = (status_get_lv(bl) + status->dex + status->luk)/10` // flee increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| MUSICAL_INTERLUDE *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `res_inc` | 8655 res | 12925: `val2 = 5 + 5 * val1` // Res Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| NEN | 237 | 1 | `stat_inc` | 6797 str, 7006 int | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| NEUTRALBARRIER | 355 | 1 | `lv` | - | - |
|  |  | 2 | `def_mdef_pct_inc` | 7836 def, 7978 mdef | 12576: `val2 = 10 + val1 * 5` // Def/Mdef |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| NIBELUNGEN | 169 | 1 | `val1_unused` | - | - |
|  |  | 2 | `atk_inc` | 7380 watk, 7382 watk | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| NPC_HALLUCINATIONWALK *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `flee_inc` | 7679 flee | 11921: `val2 = 50 * val1` // Evasion rate of physical attacks. Flee |
|  |  | 3 | `mdef_evasion` | - | 11922: `val3 = 10 * val1` // Evasion rate of magical attacks. |
|  |  | 4 | `val4_unused` | - | - |
| NYANGGRASS | 676 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ODINS_POWER | 512 | 1 | `lv` | 7426 watk, 7491 matk, 7808 def, 7984 mdef | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ONEHAND | 150 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8444 aspd_rate | 11073: `val2 = 300` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| OVERCOMING_CRISIS *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_smatk_inc` | 8596 patk, 8633 smatk | 13037: `val2 = 3 * val1` |
|  |  | 3 | `maxhp_bonus_inc` | 3222 hp_fix | 13038: `val3 = 15000 * val1` |
|  |  | 4 | `val4_unused` | - | - |
| OVERED_BOOST | 547 | 1 | `lv` | - | - |
|  |  | 2 | `flee_set` | 7649 flee | 12416: `val2 = 400 + 40 * val1` // flee bonus |
|  |  | 3 | `fix_aspd_set_arg` | 8402 fix_aspd | 12417: `val3 = 180 + 2 * val1` // aspd bonus |
|  |  | 4 | `def_pct_dec` | 7854 def | 12418: `val4 = 50` // def reduc % |
| PARALYSE | 369 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `kind` | 7707 flee, 8191 speed, 8233 speed, 8533 aspd_rate | - |
|  |  | 4 | `val4_unused` | - | - |
| PARALYSIS | 553 | 1 | `lv` | - | - |
|  |  | 2 | `def2_pct_dec` | 7924 def2 | 12438: `val2 = 2*val1` // def reduction |
|  |  | 3 | `varcast_inc` | - | 12439: `val3 = 500*val1` // varcast augmentation |
|  |  | 4 | `val4_unused` | - | - |
| PARTYFLEE | 528 | 1 | `lv` | 7673 flee | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| PETROLOGY_OPTION | 499 | 1 | `val1_unused` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3258 hp_rate | 12252: `val2 = 5` // HP Rate bonus |
|  |  | 3 | `val3_unused` | - | 12253: `val3 = 50` |
|  |  | 4 | `val4_unused` | - | - |
| POISON | 4 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `remaining_time` | - | 11189: `val4 = tick - tick_time` // Remaining time |
| POISON_MIST *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| POISON_SHIELD_OPTION *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| POWERFUL_FAITH *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7438 watk | 12830: `val2 = 5 + 5 * val1` // ATK Increase |
|  |  | 3 | `patk_inc` | 8575 patk | 12831: `val3 = 5 + 2 * val1` // PAtk Increase |
|  |  | 4 | `val4_unused` | - | - |
| POWERUP | 951 | 1 | `val1_unused` | - | - |
|  |  | 2 | `hit_pct_inc` | 7589 hit | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| POWER_OF_GAIA | 479 | 1 | `val1_unused` | - | - |
|  |  | 2 | `def_speed_val` | 7850 def, 8140 speed | 12321: `val2 = 33` // Def rate bonus/Speed rate reduction |
|  |  | 3 | `maxhp_bonus_inc` | 3260 hp_rate | 12322: `val3 = 20` // HP rate bonus |
|  |  | 4 | `val4_unused` | - | - |
| PRESTIGE | 382 | 1 | `lv` | - | - |
|  |  | 2 | `magic_evade` | - | 12167: `val2 = (status->int_ + status->luk) * val1 / 20 * status_get_lv(bl) / 200 + val1` // Chance to evade magic damage. |
|  |  | 3 | `def_inc` | 7838 def | 12168: `val3 = ((val1 * 15) + (10 * (sd?pc_checkskill(sd,CR_DEFENDER):skill_get_max(CR_DEFENDER)))) * status_get_lv(bl) / 100` // Defence added |
|  |  | 4 | `val4_unused` | - | - |
| PRE_ACIES *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `crate_inc` | 8721 crate | 12872: `val2 = 2 * val1` // CRate Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| PROMOTE_HEALTH_RESERCH | 625 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `maxhp_bonus_inc` | 3204 hp_fix | 12343: `val3 = 1000 * val2 - 500 + status_get_lv(bl) * 10 / 3` |
|  |  | 4 | `val4_unused` | - | - |
| PRON_MARCH *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_inc` | 8581 patk | 12935: `val2 = 3 * val1` // PAtk Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| PROVOKE | 20 | 1 | `lv` | - | - |
|  |  | 2 | `atk_pct` | - | 11684: `val2 = 0` // 0% Atk increase |
|  |  | 3 | `def_pct_dec` | 7826 def, 7913 def2 | 11685: `val3 = 100` // 100% Def reduction |
|  |  | 4 | `auto` | - | - |
| PUSH_CART | 518 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| PYROCLASTIC | 552 | 1 | `val1_unused` | - | - |
|  |  | 2 | `batk_watk_inc` | 7332 batk, 7422 watk | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| PYROTECHNIC_OPTION | 481 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_inc` | 7414 watk | 12220: `val2 = 60` // Eatk Renewal (Atk2) |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| QUAGMIRE | 28 | 1 | `lv` | - | - |
|  |  | 2 | `agi_dex_dec` | 6872 agi, 7080 dex | 11665: `val2 = (sd?5:10)*val1` // Agi/Dex decrease. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| RAISINGDRAGON | 417 | 1 | `maxhp_bonus_maxsp_bonus_inc` | 3252 hp_rate, 3403 sp_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| REBOUND | 560 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12484: `val4 = tick / tick_time` |
| RECOGNIZEDSPELL | 335 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| RELIGIO *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `sta_wis_spl_inc` | 7222 sta, 7242 wis, 7262 spl | 12879: `val2 = 2 * val1` // Trait Stats Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ROCK_CRUSHER | 506 | 1 | `val1_unused` | - | - |
|  |  | 2 | `def_pct_dec` | 7848 def | 12321: `val2 = 33` // Def rate bonus/Speed rate reduction |
|  |  | 3 | `hp_rate_bonus` | - | 12322: `val3 = 20` // HP rate bonus |
|  |  | 4 | `val4_unused` | - | - |
| ROCK_CRUSHER_ATK | 507 | 1 | `val1_unused` | - | - |
|  |  | 2 | `speed_slow` | 8138 speed | 12321: `val2 = 33` // Def rate bonus/Speed rate reduction |
|  |  | 3 | `hp_rate_bonus` | - | 12322: `val3 = 20` // HP rate bonus |
|  |  | 4 | `val4_unused` | - | - |
| RUN | 143 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | 11579: `val3 = (int32)(currenttick & 0x00000000ffffffffLL)` |
|  |  | 4 | `val4_unused` | - | 11580: `val4 = (int32)((currenttick & 0xffffffff00000000LL) >> 32)` |
| RUSHWINDMILL | 396 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `watk_inc` | 7405 watk | 12081: `val3 = 4 + val1 * 3 + val2 + (sd?sd->status.job_level:50) / 5` |
|  |  | 4 | `val4_unused` | - | - |
| SANDY_FESTIVAL *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `sta_wis_spl_inc` | 7224 sta, 7244 wis, 7264 spl | 13016: `val2 = 2 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SATURDAYNIGHTFEVER | 406 | 1 | `lv` | 7617 hit, 7715 flee | - |
|  |  | 2 | `mark_not_lose` | - | 10714: `val2 = 0` // Mark to not lose hp |
|  |  | 3 | `val3_unused` | - | 12103: `val3 = tick/val4` |
|  |  | 4 | `val4_unused` | - | 12101: `val4 = skill_get_time2(scdb->skill_id,val1)` |
| SERVICE4U | 181 | 1 | `val1_unused` | - | - |
|  |  | 2 | `maxsp_bonus_inc` | 3405 sp_rate | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHADOWWEAPON | 144 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHADOW_CLOCK *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHADOW_STRIP *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `res_mres_pct_dec` | 8659 res, 8683 mres | 12894: `val2 = 25` // Need official reduction amount. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHAPESHIFT | 351 | 1 | `val1_unused` | - | - |
|  |  | 2 | `element` | 8812 element | 11949: `val2 = ELE_FIRE` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHIELDCHAINRUSH *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SHIELDSPELL_ATK | 380 | 1 | `val1_unused` | - | - |
|  |  | 2 | `atk_inc` | 7395 watk, 7483 matk | 12181: `val2 = 150` // WATK/MATK bonus |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SIGNUMCRUCIS | 31 | 1 | `lv` | - | - |
|  |  | 2 | `def_pct_dec` | 7820 def | 10899: `val2 = 10 + 4*val1` // Def reduction |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SINCERE_FAITH *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `fix_aspd_dec_arg` | 8413 fix_aspd | 12838: `val2 = (1 + val1) / 2` // ASPD Increase |
|  |  | 3 | `perfect_hit_inc` | - | 12839: `val3 = 4 * val1` // Perfect Hit Increase |
|  |  | 4 | `val4_unused` | - | - |
| SKA | 225 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SKE | 222 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SLOWDOWN | 64 | 1 | `speed_slow` | 8120 speed | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SMOKEPOWDER | 424 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SOLID_SKIN_OPTION | 476 | 1 | `val1_unused` | - | - |
|  |  | 2 | `def_pct_inc` | 7846 def | 12256: `val2 = 33` // % Increase DEF |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SOULCOLD | 510 | 1 | `stat_inc` | 6862 agi | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SOULFAIRY | 726 | 1 | `lv` | - | - |
|  |  | 2 | `matk_inc` | 7479 matk | 12786: `val2 = 10 * val1` // MATK Increase |
|  |  | 3 | `varcast_dec` | - | 12787: `val3 = 5` // Variable Cast Time Reduction |
|  |  | 4 | `val4_unused` | - | - |
| SOULFALCON | 727 | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7436 watk | 12778: `val2 = 10 * val1` // WATK Increase |
|  |  | 3 | `hit_inc` | 7615 hit | 12779: `val3 = 10` // HIT Increase |
|  |  | 4 | `val4_unused` | - | - |
| SOULGOLEM | 728 | 1 | `lv` | - | - |
|  |  | 2 | `def_inc` | 7856 def | 12770: `val2 = 60 * val1` // DEF Increase |
|  |  | 3 | `mdef_inc` | 7986 mdef | 12771: `val3 = 15 + 5 * val1` // MDEF Increase |
|  |  | 4 | `val4_unused` | - | - |
| SOULSHADOW | 725 | 1 | `lv` | - | - |
|  |  | 2 | `fix_aspd_dec_arg` | 8409 fix_aspd | 12774: `val2 = (1 + val1) / 2` // ASPD Increase |
|  |  | 3 | `cri_inc_arg` | 7549 cri | 12775: `val3 = 10 + 2 * val1` // CRIT Increase |
|  |  | 4 | `val4_unused` | - | - |
| SPEARQUICKEN | 86 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8460 aspd_rate | 11082: `val2 = 200+10*val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SPEED | 245 | 1 | `lv` | 7669 flee | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SPELL_ENCHANTING *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `smatk_inc` | 8620 smatk | 12940: `val2 = 4 * val1` // SMatk Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SPIDERWEB | 133 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SPIRIT | 184 | 1 | `lv` | - | - |
|  |  | 2 | `link_type` | 6809 str, 6880 agi, 6943 vit, 7012 int, 7094 dex, 7157 luk, 8099 speed | - |
|  |  | 3 | `str_agi_vit_packed` | 6809 str, 6880 agi, 6943 vit | 11443: `val3 = 0` |
|  |  | 4 | `int_dex_luk_packed` | 7012 int, 7094 dex, 7157 luk | 11444: `val4 = 0` |
| SPURT | 183 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SP_SHA | 733 | 1 | `val1_unused` | - | - |
|  |  | 2 | `speed_slow` | 8150 speed | 12810: `val2 = 50` // Move speed reduction |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STARSTANCE | 714 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_dec_arg` | 8557 aspd_rate | 12738: `val2 = 4 + 2 * val1` // ASPD Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STAR_COMFORT | 219 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8436 aspd_rate | 11662: `val2 = (status_get_lv(bl) + status->dex + status->luk)` // Aspd increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STEALTHFIELD | 357 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STEELBODY | 136 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STOMACHACHE | 427 | 1 | `stat_dec` | 6817 str, 6888 agi, 6947 vit, 7020 int, 7098 dex, 7161 luk | - |
|  |  | 2 | `sp_consume` | - | 12334: `val2 = 8` // SP consume. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12335: `val4 = tick / 10000` |
| STONE | 0 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `petrify_time` | - | 10273: `val3 = std::max<int32>(1, tick - delay)` // Petrify time |
|  |  | 4 | `remaining_time` | - | 11189: `val4 = tick - tick_time` // Remaining time |
| STONEHARDSKIN | 319 | 1 | `def_mdef_inc` | 7812 def, 7970 mdef | 11861: `val1 = sd->status.job_level * pc_checkskill(sd, RK_RUNEMASTERY) / 4` // DEF/MDEF Increase |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STONE_WALL *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `def_inc` | 7858 def | 12412: `val2 = 100 * val1` // DEF bonus |
|  |  | 3 | `mdef_inc` | 7988 mdef | 12413: `val3 = 30 * val1` // MDEF bonus |
|  |  | 4 | `val4_unused` | - | - |
| STRIKING | 389 | 1 | `lv` | - | - |
|  |  | 2 | `watk_inc` | 7403 watk | - |
|  |  | 3 | `sp_cost` | - | 12060: `val3 = 6 - val1` // spcost = 6 - level (lvl1:5 ... lvl 5: 1) |
|  |  | 4 | `val4_unused` | - | 12061: `val4 = tick / 1000` |
| STRIPARMOR | 70 | 1 | `val1_unused` | - | - |
|  |  | 2 | `stat_pct_dec` | 6955 vit | 10999: `val2 = 40` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STRIPHELM | 71 | 1 | `val1_unused` | - | - |
|  |  | 2 | `stat_pct_dec` | 7038 int | 11003: `val2 = 40` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STRIPSHIELD | 69 | 1 | `val1_unused` | - | - |
|  |  | 2 | `def_pct_dec` | 7828 def | 10418: `val2 = 0` // GX effect. Do not take shield off.. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| STRONG_PROTECTION_OPTION *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SUITON | 236 | 1 | `val1_unused` | - | - |
|  |  | 2 | `stat_dec` | 6874 agi | 11060: `val2 = 0` // Agi penalty |
|  |  | 3 | `stat_speed_slow` | 6874 agi, 8124 speed | 11061: `val3 = 0` // Walk speed penalty |
|  |  | 4 | `val4_unused` | - | - |
| SUNSTANCE | 711 | 1 | `lv` | - | - |
|  |  | 2 | `batk_watk_pct_inc` | 7346 batk, 7434 watk | 12730: `val2 = 2 + val1` // ATK Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SUN_COMFORT | 217 | 1 | `val1_unused` | - | - |
|  |  | 2 | `def2_inc` | 7895 def2 | 11656: `val2 = (status_get_lv(bl) + status->dex + status->luk)/2` // def increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SWINGDANCE | 393 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `aspd_rate_speed_val` | 8197 speed, 8541 aspd_rate | 12074: `val3 = 3 * val1 + val2` // Walk speed and aspd reduction. |
|  |  | 4 | `val4_unused` | - | - |
| SWOO | 224 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SWORDCLAN | 659 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| SYMPHONYOFLOVER | 394 | 1 | `lv` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `mdef_pct_inc` | 7982 mdef | 12077: `val3 = 2 * val1 + val2 + (sd?sd->status.job_level:50) / 4` // MDEF Increase |
|  |  | 4 | `val4_unused` | - | - |
| TALISMAN_OF_MAGICIAN *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `smatk_inc` | 8625 smatk | 13000: `val2 = 2 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TALISMAN_OF_WARRIOR *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_inc` | 8590 patk | 13000: `val2 = 2 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TEARGAS | 425 | 1 | `val1_unused` | - | - |
|  |  | 2 | `hp_drain` | - | 12325: `val2 = status_get_max_hp(bl) * 5 / 100` // Drain 5% HP |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12326: `val4 = tick / 2000` |
| TEMPERING *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `patk_inc` | 8583 patk | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TEMPORARY_COMMUNION *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `patk_smatk_hplus_inc` | 8592 patk, 8629 smatk, 8703 hplus | 13012: `val2 = val1 * 3` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TIDAL_WEAPON | 504 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_pct_inc` | 7420 watk | 12316: `val2 = 20` // Increase Elemental's attack. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TIDAL_WEAPON_OPTION | 505 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TINDER_BREAKER | 541 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TINDER_BREAKER2 | 542 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TOXIN_OF_MANDARA *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `res_dec` | 8663 res | 12442: `val2 = 15*val1` // res reduction |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| TROPIC_OPTION | 485 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_inc` | 7418 watk | 12227: `val2 = 180` // Eatk Renewal (Atk2) |
|  |  | 3 | `val3_unused` | - | 12228: `val3 = MG_FIREBOLT` |
|  |  | 4 | `val4_unused` | - | - |
| TRUESIGHT | 120 | 1 | `lv` | - | - |
|  |  | 2 | `cri_inc` | 7535 cri | 11652: `val2 = 10*val1` // Critical increase |
|  |  | 3 | `hit_inc` | 7573 hit | 11653: `val3 = 3*val1` // Hit increase |
|  |  | 4 | `val4_unused` | - | - |
| TWOHANDQUICKEN | 22 | 1 | `val1_unused` | - | - |
|  |  | 2 | `aspd_rate_quicken` | 8440 aspd_rate | 11073: `val2 = 300` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| T_FIFTH_GOD *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `smatk_inc` | 8627 smatk | 13003: `val2 = 5 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| UNIVERSESTANCE | 710 | 1 | `lv` | - | - |
|  |  | 2 | `stat_inc` | 6833 str, 6906 agi, 6967 vit, 7034 int, 7120 dex, 7181 luk | 12747: `val2 = 2 + val1` // All Stats Increase |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| UPHEAVAL_OPTION | 503 | 1 | `val1_unused` | - | - |
|  |  | 2 | `maxhp_bonus_inc` | 3264 hp_rate | 12263: `val2 = 15` // HP rate bonus |
|  |  | 3 | `val3_unused` | - | 12264: `val3 = WZ_EARTHSPIKE` |
|  |  | 4 | `val4_unused` | - | - |
| VENOMBLEED | 370 | 1 | `val1_unused` | - | - |
|  |  | 2 | `reflect_dec` | - | 11221: `val2 = 30` // Reflect damage % reduction |
|  |  | 3 | `hp_penalty_flag` | 3280 hp_rate | - |
|  |  | 4 | `val4_unused` | - | - |
| VIOLENTGALE | 102 | 1 | `lv` | - | - |
|  |  | 2 | `flee_inc` | 7659 flee | 11035: `val2 = val1*3` // Flee increase |
|  |  | 3 | `val3_unused` | - | 11040: `val3 = enchant_eff[i]` |
|  |  | 4 | `val4_unused` | - | - |
| VOLCANO | 100 | 1 | `lv` | - | - |
|  |  | 2 | `batk_watk_inc` | 7322 batk, 7374 watk | 11023: `val2 = val1*10` // Watk increase |
|  |  | 3 | `val3_unused` | - | 11027: `val3 = enchant_eff[i]` |
|  |  | 4 | `val4_unused` | - | - |
| WALKSPEED | 277 | 1 | `speed_pct` | 8243 speed | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WATERWEAPON | 97 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WATER_BARRIER | 469 | 1 | `val1_unused` | - | - |
|  |  | 2 | `watk_flee_dec` | 7378 watk, 7681 flee | 12310: `val2 = 30` // Reductions. Atk2 and Flee1 |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WATER_INSIGNIA | 515 | 1 | `level` | 7412 watk, 8865 atk_element | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12573: `val4 = tick / tick_time` |
| WATKFOOD | 212 | 1 | `watk_inc` | 7372 watk | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WEDDING | 63 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WHISTLE | 173 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee_inc` | 7655 flee | - |
|  |  | 3 | `flee2_inc` | 7751 flee2 | - |
|  |  | 4 | `val4_unused` | - | - |
| WIDEWEB *(not in PRE db)* |  | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WILD_STORM_OPTION | 497 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | 12249: `val2 = MG_LIGHTNINGBOLT` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WILD_WALK *(not in PRE db)* |  | 1 | `lv` | - | - |
|  |  | 2 | `speed_haste` | 8214 speed | 13033: `val2 = (1 + val1 / 2) * 25` |
|  |  | 3 | `flee_inc` | 7685 flee | 13034: `val3 = 50 + 50 * val1` |
|  |  | 4 | `val4_unused` | - | - |
| WINDWALK | 121 | 1 | `lv` | 8173 speed | - |
|  |  | 2 | `flee_inc` | 7657 flee | 11369: `val2 = (val1+1)/2` // Flee bonus is 1/1/2/2/3/3/4/4/5/5 |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WINDWEAPON | 98 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| WIND_INSIGNIA | 516 | 1 | `level` | 7412 watk, 8555 aspd_rate, 8874 atk_element | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | 12573: `val4 = tick / tick_time` |
| WIND_STEP_OPTION | 471 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee_speed_val` | 7717 flee, 8199 speed | 12242: `val2 = 50` // % Increase speed and flee. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| ZANGETSU | 537 | 1 | `lv` | - | - |
|  |  | 2 | `batk_inc` | 7338 batk | 12372: `val2 = (status_get_lv(bl) / 3) + (20 * val1)` // +Watk |
|  |  | 3 | `matk_inc` | 7485 matk | 12377: `val3 = (status_get_lv(bl) / 3) + (20 * val1)` // +Matk |
|  |  | 4 | `val4_unused` | - | - |
| ZEPHYR | 474 | 1 | `val1_unused` | - | - |
|  |  | 2 | `flee_inc` | 7721 flee | 12313: `val2 = 25` // Flee. |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| _AUTOSHADOWSPELL | 447 | 1 | `val1_unused` | - | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `matk_inc` | 7489 matk | - |
| _BODYPAINT | 449 | 1 | `lv` | 8535 aspd_rate | - |
|  |  | 2 | `val2_unused` | - | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| _ENERVATION | 452 | 1 | `lv` | - | - |
|  |  | 2 | `batk_watk_pct_dec` | 7336 batk, 7401 watk | 12005: `val2 = 20 + 10 * val1` // ATK Reduction |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| _GROOMY | 453 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_inc_arg` | 8539 aspd_rate | 12012: `val2 = 20 + 10 * val1` // ASPD |
|  |  | 3 | `hit_pct_dec` | 7595 hit | 12013: `val3 = 20 * val1` // HIT |
|  |  | 4 | `val4_unused` | - | - |
| _INVISIBILITY | 450 | 1 | `lv` | - | - |
|  |  | 2 | `aspd_rate_inc_arg` | 8537 aspd_rate | 11998: `val2 = 50 - 10 * val1` // ASPD |
|  |  | 3 | `cri_inc_arg` | 7545 cri | 11999: `val3 = 20 * val1` // CRITICAL |
|  |  | 4 | `val4_unused` | - | 12000: `val4 = tick / 1000` |
| _LAZINESS | 455 | 1 | `lv` | - | - |
|  |  | 2 | `cast_inc` | - | 12026: `val2 = 10 + 10 * val1` // Cast Increase |
|  |  | 3 | `flee_pct_dec` | 7711 flee | 12027: `val3 = 10 * val1` // Flee Reduction |
|  |  | 4 | `val4_unused` | - | - |
| _STRIPACCESSORY | 458 | 1 | `val1_unused` | - | - |
|  |  | 2 | `stat_pct_dec` | 7040 int, 7108 dex, 7165 luk | 11995: `val2 = 20` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| _UNLUCKY | 456 | 1 | `val1_unused` | - | - |
|  |  | 2 | `cri_flee2_val` | 7547 cri, 7753 flee2 | - |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |
| _WEAKNESS | 457 | 1 | `lv` | - | - |
|  |  | 2 | `maxhp_bonus_dec` | 3284 hp_rate | 12042: `val2 = 10 * val1` |
|  |  | 3 | `val3_unused` | - | - |
|  |  | 4 | `val4_unused` | - | - |

## Notes

* `ADRENALINE` / `ADRENALINE2` / `WEAPONPERFECTION` / `OVERTHRUST` share one block in `status_change_start` (status.cpp:11611-11627): the comment says "Power increase" for val3 but Adrenaline's val3 is the ASPD value (`200` or `300`, read at 8452 / 8456).
* `BLESSING.val2` is `val1` on players and on non-undead, non-demon monsters, and `0` otherwise: `0` means "halve the stat" (status.cpp:11588-11592, read at 6800-6803).
* `CONCENTRATE.val3/val4` are the card bonuses of AGI / DEX at the time of casting, excluded from the percentage (status.cpp:11598-11608).
* `DEFSET.val1`, `MDEFSET.val1`, `OVERED_BOOST.val2` are returned **unclamped and narrowed** to the return type (signed char / int16): `val1 = 40000` gives DEF 64 (`Q-defset-narrowing` in the golden data).
* `MARIONETTE.val3/val4`, `SPIRIT.val3/val4` pack three stats per value, read with `(v >> 16) & 0xFF`, `(v >> 8) & 0xFF`, `v & 0xFF`.
* `JOINTBEAT.val2` is a bit set (`BREAK_ANKLE` 1, `BREAK_WRIST` 2, `BREAK_KNEE` 4, `BREAK_SHOULDER` 8, `BREAK_WAIST` 16).

