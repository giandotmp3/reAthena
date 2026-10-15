// Typed knob schemas. Each knob: X(Type, name, shipped_default, min, max)
//
// ONE source of defaults (spec §5.6): the value shipped in conf/ is the code default.
// Drift keys from spec §5.6 are marked [§5.6 drift: legacy code default -> shipped].
//
// Battle knobs come in two parts:
//   RATHENA_BATTLE_KNOBS_TYPED      below, written by hand: the knobs the rules use, with the
//                                   type and bounds the rules rely on.
//   RATHENA_BATTLE_KNOBS_GENERATED  battle_knobs.gen.hpp, generated from legacy battle_data[]
//                                   and conf/battle/*.conf by tools/codegen/battle_knobs.py:
//                                   every other battle_config knob.
// The generator checks every hand-typed knob against battle_data[] and the shipped conf, so
// after editing this list (or conf/battle, or battle_data[]) run
// `python3 tools/codegen/battle_knobs.py`. A knob's name is its conf key; a key that is not an
// identifier ("feature.banking") gets '_' for '.' and is mapped back by
// RATHENA_BATTLE_KNOB_CONF_KEYS.
#pragma once

#include <cstdint>
#include <limits>

#define RATHENA_I32_MAX (std::numeric_limits<std::int32_t>::max())
#define RATHENA_I32_MIN (std::numeric_limits<std::int32_t>::min())

#include "rathena/core/battle_knobs.gen.hpp"

// conf/battle/*.conf  ->  rathena::core::BattleConfig (all of battle_data[])
#define RATHENA_BATTLE_KNOBS(X) RATHENA_BATTLE_KNOBS_TYPED(X) RATHENA_BATTLE_KNOBS_GENERATED(X)

// Hand-typed subset (rules depend on these names, types and bounds).
#define RATHENA_BATTLE_KNOBS_TYPED(X)                                                      \
	/* player.conf */                                                                  \
	X(std::int32_t, max_parameter, 99, 10, 10000)                                      \
	X(std::int32_t, max_third_parameter, 130, 10, 10000) /* [§5.6 drift: 135 -> 130] */ \
	X(std::int32_t, max_baby_parameter, 80, 10, 10000)                                 \
	X(std::int32_t, max_aspd, 190, 100, 199)                                           \
	X(std::int32_t, max_third_aspd, 193, 100, 199)                                     \
	X(std::int32_t, max_cart_weight, 8000, 100, 1000000)                               \
	X(std::int32_t, prevent_logout, 10000, 0, 3600000) /* BR-MAP-056 */                \
	X(std::int32_t, vip_storage_increase, 300, 0, 10000) /* BR-MAP-050 */              \
	X(std::int32_t, natural_healhp_interval, 6000, 500, RATHENA_I32_MAX) /* BR-MAP-055 */ \
	X(std::int32_t, natural_healsp_interval, 8000, 500, RATHENA_I32_MAX) /* BR-MAP-055 */ \
	/* skill.conf */                                                                   \
	X(std::int32_t, castrate_dex_scale, 150, 1, RATHENA_I32_MAX) /* BR-MAP-051 */       \
	X(std::int32_t, vcast_stat_scale, 530, 1, RATHENA_I32_MAX) /* BR-MAP-052 */         \
	X(std::int32_t, default_fixed_castrate, 20, 0, 100) /* BR-MAP-052 */               \
	X(std::int32_t, delay_rate, 100, 0, RATHENA_I32_MAX) /* BR-MAP-053 */               \
	X(std::int32_t, min_skill_delay_limit, 100, 10, RATHENA_I32_MAX) /* BR-MAP-053 */   \
	/* battle.conf */                                                                  \
	X(std::int32_t, enable_critical, 17, 0, 31)                                        \
	X(std::int32_t, min_hitrate, 5, 0, 100) /* BR-MAP-011 */                           \
	X(std::int32_t, max_hitrate, 100, 0, 100) /* BR-MAP-011 */                         \
	X(std::int32_t, agi_penalty_type, 1, 0, 2) /* BR-MAP-012 */                        \
	X(std::int32_t, agi_penalty_count, 3, 2, 1000)                                     \
	X(std::int32_t, agi_penalty_num, 10, 0, 100)                                       \
	X(std::int32_t, vit_penalty_type, 1, 0, 2) /* BR-MAP-013 */                        \
	X(std::int32_t, vit_penalty_count, 3, 2, 1000)                                     \
	X(std::int32_t, vit_penalty_num, 5, 0, 100)                                        \
	/* items.conf */                                                                   \
	X(std::int32_t, vending_max_value, 1000000000, 1, RATHENA_I32_MAX) /* [§5.6 drift: 10,000,000 -> 1,000,000,000] */ \
	X(std::int32_t, vending_tax, 500, 0, 10000) /* [§5.6 drift: 0 -> 500] BR-MAP-044 */ \
	X(std::int32_t, vending_tax_min, 100000000, 0, RATHENA_I32_MAX) /* BR-MAP-044 */   \
	X(std::int32_t, min_shop_buy, 1, 0, RATHENA_I32_MAX) /* BR-MAP-047 */               \
	X(std::int32_t, min_shop_sell, 0, 0, RATHENA_I32_MAX) /* BR-MAP-047 */              \
	/* drops.conf */                                                                   \
	X(std::int32_t, item_rate_common, 100, 0, 1000000) /* BR-MAP-035 */               \
	X(std::int32_t, item_drop_common_min, 1, 1, 10000)                                 \
	X(std::int32_t, item_drop_common_max, 10000, 1, 10000)                             \
	X(bool, item_logarithmic_drops, false, 0, 1)                                       \
	X(std::int32_t, drop_rate_cap, 9000, 0, 10000) /* BR-MAP-036 */                    \
	X(std::int32_t, drop_rate_cap_vip, 9000, 0, 10000) /* BR-MAP-036 */                \
	X(bool, drop_rate0item, false, 0, 1) /* BR-MAP-036 */                              \
	X(std::int32_t, vip_drop_increase, 50, 0, RATHENA_I32_MAX) /* BR-MAP-036 */         \
	X(std::int32_t, item_first_get_time, 3000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */     \
	X(std::int32_t, item_second_get_time, 2000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */    \
	X(std::int32_t, item_third_get_time, 2000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */     \
	X(std::int32_t, mvp_item_first_get_time, 10000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */ \
	X(std::int32_t, mvp_item_second_get_time, 10000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */ \
	X(std::int32_t, mvp_item_third_get_time, 2000, 0, RATHENA_I32_MAX) /* BR-MAP-037 */ \
	/* exp.conf */                                                                     \
	X(std::int32_t, base_exp_rate, 100, 0, RATHENA_I32_MAX)                            \
	X(std::int32_t, job_exp_rate, 100, 0, RATHENA_I32_MAX)                             \
	X(bool, multi_level_up, false, 0, 1) /* BR-MAP-024 */                              \
	X(std::int32_t, death_penalty_type, 1, 0, 2) /* [§5.6 drift: 0 -> 1] BR-MAP-030 */  \
	X(std::int32_t, death_penalty_base, 100, 0, 10000) /* [§5.6 drift: 0 -> 100] */     \
	X(std::int32_t, death_penalty_job, 100, 0, 10000) /* [§5.6 drift: 0 -> 100] */      \
	/* guild.conf / party.conf / client.conf */                                        \
	X(std::int32_t, guild_exp_limit, 50, 0, 99) /* BR-MAP-027 */                       \
	X(std::int32_t, party_even_share_bonus, 0, 0, 1000)                                \
	X(std::int32_t, min_chat_delay, 0, 0, 60000)

// conf/inter_athena.conf  ->  rathena::core::InterConfig
#define RATHENA_INTER_KNOBS(X) \
	X(std::int32_t, party_share_level, 15, 0, 1000) /* [§5.6 drift: 10 -> 15] BR-CHAR-034 */

// conf/char_athena.conf  ->  rathena::core::CharConfig
#define RATHENA_CHAR_KNOBS(X)                                                                    \
	X(std::int32_t, mail_return_days, 15, 0, 365) /* [§5.6 drift: 14 -> 15] BR-CHAR-047 */        \
	X(std::int32_t, mail_delete_days, 15, 0, 365) /* [§5.6 drift: 14 -> 15] BR-CHAR-047 */        \
	X(std::int32_t, char_del_delay, 86400, 0, RATHENA_I32_MAX) /* BR-CHAR-009 */                  \
	X(std::int32_t, pincode_maxtry, 3, 1, 10) /* BR-CHAR-021 */

// conf/login_athena.conf  ->  rathena::core::LoginPolicyConfig
#define RATHENA_LOGIN_KNOBS(X)                                                   \
	X(bool, new_account, false, 0, 1) /* [§5.6 drift: yes -> no] BR-LOGIN-006 */ \
	X(std::int32_t, chars_per_account, 0, 0, 15) /* BR-LOGIN-031 */
