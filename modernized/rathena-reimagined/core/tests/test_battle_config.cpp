// Acceptance tests: spec §5.6 config drift (shipped value is the single source of defaults)
// and the typed, immutable BattleConfig contract (architecture §2, critique M10).
#include <gtest/gtest.h>

#include <algorithm>
#include <type_traits>

#include "rathena/core/config.hpp"
#include "rathena/core/rng.hpp"
#include "rathena/core/typed_config.hpp"

using namespace rathena::core;

namespace {

Config parse(std::string_view text) {
	static const MapEnvironment env;
	return ConfigLoader(env).load_string(text, "inline.conf");
}

template <class C>
std::int64_t schema_default(std::string_view key) {
	const auto& s = C::schema();
	auto it = std::find_if(s.begin(), s.end(), [&](const KnobInfo& k) { return k.name == key; });
	EXPECT_NE(it, s.end()) << key;
	return it == s.end() ? -1 : it->shipped_default;
}

} // namespace

// ------------------------------------------------- §5.6 drift: code default == shipped

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_max_third_parameter_IsShipped130) {
	constexpr BattleConfig c;
	static_assert(c.max_third_parameter() == 130);
	EXPECT_EQ(schema_default<BattleConfig>("max_third_parameter"), 130);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_vending_max_value_IsShipped1e9) {
	constexpr BattleConfig c;
	static_assert(c.vending_max_value() == 1'000'000'000);
	EXPECT_EQ(schema_default<BattleConfig>("vending_max_value"), 1'000'000'000);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_vending_tax_IsShipped500) {
	constexpr BattleConfig c;
	static_assert(c.vending_tax() == 500);
	EXPECT_EQ(schema_default<BattleConfig>("vending_tax"), 500);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_party_share_level_IsShipped15) {
	constexpr InterConfig c;
	static_assert(c.party_share_level() == 15);
	EXPECT_EQ(schema_default<InterConfig>("party_share_level"), 15);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_mail_return_and_delete_days_AreShipped15) {
	constexpr CharConfig c;
	static_assert(c.mail_return_days() == 15);
	static_assert(c.mail_delete_days() == 15);
	EXPECT_EQ(schema_default<CharConfig>("mail_return_days"), 15);
	EXPECT_EQ(schema_default<CharConfig>("mail_delete_days"), 15);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_new_account_IsShippedNo) {
	constexpr LoginPolicyConfig c;
	static_assert(!c.new_account());
	EXPECT_EQ(schema_default<LoginPolicyConfig>("new_account"), 0);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_death_penalty_IsShipped_1_100_100) {
	constexpr BattleConfig c;
	static_assert(c.death_penalty_type() == 1);
	static_assert(c.death_penalty_base() == 100);
	static_assert(c.death_penalty_job() == 100);
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_RepresentativeKnobsMatchShippedConf) {
	// Values shipped in conf/battle/*.conf @ d4b8e7b, which the rules in map-rules depend on.
	constexpr BattleConfig c;
	EXPECT_EQ(c.max_parameter(), 99);           // BR-MAP-022
	EXPECT_EQ(c.max_aspd(), 190);               // BR-MAP-019
	EXPECT_EQ(c.max_third_aspd(), 193);         // BR-MAP-019
	EXPECT_EQ(c.min_hitrate(), 5);              // BR-MAP-011
	EXPECT_EQ(c.max_hitrate(), 100);            // BR-MAP-011
	EXPECT_EQ(c.agi_penalty_count(), 3);        // BR-MAP-012
	EXPECT_EQ(c.agi_penalty_num(), 10);         // BR-MAP-012
	EXPECT_EQ(c.vit_penalty_num(), 5);          // BR-MAP-013
	EXPECT_EQ(c.vending_tax_min(), 100'000'000);// BR-MAP-044
	EXPECT_EQ(c.prevent_logout(), 10000);       // BR-MAP-056
	EXPECT_EQ(c.vip_storage_increase(), 300);   // BR-MAP-050
	EXPECT_EQ(c.guild_exp_limit(), 50);         // BR-MAP-027
	EXPECT_FALSE(c.multi_level_up());           // BR-MAP-024
	EXPECT_EQ(c.base_exp_rate(), 100);
	EXPECT_EQ(c.item_rate_common(), 100);
	constexpr CharConfig ch;
	EXPECT_EQ(ch.char_del_delay(), 86400);      // BR-CHAR-009
	EXPECT_EQ(ch.pincode_maxtry(), 3);          // BR-CHAR-021
}

TEST(SPEC_5_6_ConfigDrift, SPEC_5_6_SchemaDefaultsEqualDefaultConstructedValues) {
	// One source of defaults: the schema row and the constructed value cannot disagree.
	const BattleConfig c;
	for (const auto& k : BattleConfig::schema()) {
		EXPECT_GE(k.shipped_default, k.min) << k.name;
		EXPECT_LE(k.shipped_default, k.max) << k.name;
	}
	EXPECT_EQ(BattleConfig::from_config(Config{}), c);
}

// ------------------------------------------------------- Typed immutable value contract

TEST(CORE_BattleConfig, CORE_BattleConfig_IsAValueTypeWithoutSetters) {
	static_assert(std::is_trivially_copyable_v<BattleConfig>);
	static_assert(std::is_nothrow_copy_constructible_v<BattleConfig>);
	// Overlay produces a new value and leaves the original untouched.
	const BattleConfig base;
	const auto tuned = base.overlaid_with(parse("vending_tax: 0\n"));
	EXPECT_EQ(base.vending_tax(), 500);
	EXPECT_EQ(tuned.vending_tax(), 0);
	EXPECT_NE(base, tuned);
}

TEST(CORE_BattleConfig, CORE_BattleConfig_OverridesAndLegacySwitchSyntax) {
	const auto c = BattleConfig::from_config(parse(
		"base_exp_rate: 500\nmulti_level_up: yes\nitem_logarithmic_drops: on\nmax_third_parameter: +150\n"));
	EXPECT_EQ(c.base_exp_rate(), 500);
	EXPECT_TRUE(c.multi_level_up());
	EXPECT_TRUE(c.item_logarithmic_drops());
	EXPECT_EQ(c.max_third_parameter(), 150);
}

TEST(CORE_BattleConfig, CORE_BattleConfig_OutOfRangeRejected) {
	try {
		(void)BattleConfig::from_config(parse("min_hitrate: 101\n"));
		FAIL();
	} catch (const ConfigError& e) {
		EXPECT_EQ(e.code(), ConfigErrc::InvalidValue);
	}
	EXPECT_THROW((void)BattleConfig::from_config(parse("vending_tax: -1\n")), ConfigError);
	EXPECT_THROW((void)BattleConfig::from_config(parse("vending_max_value: 3000000000\n")), ConfigError);
	EXPECT_THROW((void)BattleConfig::from_config(parse("multi_level_up: 2\n")), ConfigError);
	EXPECT_THROW((void)BattleConfig::from_config(parse("base_exp_rate: lots\n")), ConfigError);
}

TEST(CORE_BattleConfig, CORE_BattleConfig_CrossKnobInvariants) {
	EXPECT_THROW((void)BattleConfig::from_config(parse("min_hitrate: 90\nmax_hitrate: 80\n")), ConfigError);
	EXPECT_THROW((void)BattleConfig::from_config(parse("item_drop_common_min: 500\nitem_drop_common_max: 100\n")), ConfigError);
}

TEST(CORE_BattleConfig, CORE_BattleConfig_UnknownKeysCollectedOrRejected) {
	const auto cfg = parse("base_exp_rate: 200\nnot_yet_modelled_knob: 1\n");
	std::vector<std::string> unknown;
	const auto c = BattleConfig::from_config(cfg, UnknownKeyPolicy::Collect, &unknown);
	EXPECT_EQ(c.base_exp_rate(), 200);
	ASSERT_EQ(unknown.size(), 1u);
	EXPECT_EQ(unknown[0], "not_yet_modelled_knob");
	try {
		(void)BattleConfig::from_config(cfg, UnknownKeyPolicy::Reject);
		FAIL();
	} catch (const ConfigError& e) {
		EXPECT_EQ(e.code(), ConfigErrc::UnknownKey);
	}
}

TEST(CORE_BattleConfig, CORE_BattleConfig_LoadedThroughImportOverlay) {
	const MapEnvironment env;
	const auto cfg = ConfigLoader(env).load_file(RATHENA_CORE_FIXTURES_DIR "/battle_base.conf");
	const auto c = BattleConfig::from_config(cfg);
	EXPECT_EQ(c.base_exp_rate(), 500); // overlay wins
	EXPECT_EQ(c.job_exp_rate(), 300);  // later definition after the import wins
	EXPECT_TRUE(c.multi_level_up());
}

namespace {
// Shape of a rule signature under the dependency rule `rules -> domain, core/{Rng, BattleConfig}`.
int clamp_hit_rate(int raw, const BattleConfig& cfg, Rng&) {
	return std::clamp(raw, cfg.min_hitrate(), cfg.max_hitrate());
}
} // namespace

TEST(CORE_BattleConfig, CORE_BattleConfig_PassedExplicitlyToRules) {
	SeededRng rng(1);
	const BattleConfig cfg;
	EXPECT_EQ(clamp_hit_rate(-20, cfg, rng), 5);
	EXPECT_EQ(clamp_hit_rate(74, cfg, rng), 74);
	EXPECT_EQ(clamp_hit_rate(250, cfg, rng), 100);
}
