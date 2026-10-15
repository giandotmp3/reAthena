// Typed knob schemas. Each knob: X(Type, name, shipped_default, min, max)
//
// ONE source of defaults (spec §5.6): the value shipped in conf/ is the code default.
// Drift keys from spec §5.6 are marked [§5.6 drift: legacy code default -> shipped].
// This is a representative subset of the 581 battle_config knobs; Phase E generates the
// full list from the conf schema (architecture §2 "generated from the conf schema").
#pragma once

#include <cstdint>
#include <limits>

#define RATHENA_I32_MAX (std::numeric_limits<std::int32_t>::max())

// conf/battle/*.conf  ->  rathena::core::BattleConfig
#define RATHENA_BATTLE_KNOBS(X)                                                        \
	/* player.conf */                                                                  \
	X(std::int32_t, max_parameter, 99, 10, 10000)                                      \
	X(std::int32_t, max_third_parameter, 130, 10, 10000) /* [§5.6 drift: 135 -> 130] */ \
	X(std::int32_t, max_baby_parameter, 80, 10, 10000)                                 \
	X(std::int32_t, max_aspd, 190, 100, 199)                                           \
	X(std::int32_t, max_third_aspd, 193, 100, 199)                                     \
	X(std::int32_t, max_cart_weight, 8000, 100, 1000000)                               \
	X(std::int32_t, prevent_logout, 10000, 0, 3600000) /* BR-MAP-056 */                \
	X(std::int32_t, vip_storage_increase, 300, 0, 10000) /* BR-MAP-050 */              \
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
	/* drops.conf */                                                                   \
	X(std::int32_t, item_rate_common, 100, 0, 1000000) /* BR-MAP-035 */               \
	X(std::int32_t, item_drop_common_min, 1, 1, 10000)                                 \
	X(std::int32_t, item_drop_common_max, 10000, 1, 10000)                             \
	X(bool, item_logarithmic_drops, false, 0, 1)                                       \
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
