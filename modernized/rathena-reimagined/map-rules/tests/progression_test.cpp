// Acceptance tests: rules/progression, BR-MAP-021..034 (spec §5.4, CAP-15).
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "golden_vectors.hpp"
#include "rathena/map_rules/progression.hpp"
#include "rathena/map_rules/rng.hpp"

namespace mr = rathena::map_rules;
namespace pg = rathena::map_rules::progression;

namespace {

mr::BattleConfig re() { return {}; }

// Synthetic exp table: next[level] = 100 x level, max level 10.
pg::ExpTable small_table() {
	pg::ExpTable t;
	for (int lv = 0; lv <= 10; ++lv) {
		t.next.push_back(lv == 10 ? 0 : 100LL * lv);
	}
	return t;
}

} // namespace

TEST(Progression, BR_MAP_021_RenewalStatCostWorkedExample) {
	// 10 -> 11 costs 2; 11 -> 12 costs 3.
	EXPECT_EQ(pg::stat_cost(10, mr::Mode::Renewal), 2);
	EXPECT_EQ(pg::stat_cost(11, mr::Mode::Renewal), 3);
	EXPECT_EQ(pg::stat_cost(100, mr::Mode::Renewal), 16);
	EXPECT_EQ(pg::stat_cost(105, mr::Mode::Renewal), 20);
}

TEST(Progression, BR_MAP_021_PreRenewalStatCost) {
	EXPECT_EQ(pg::stat_cost(1, mr::Mode::PreRenewal), 2);
	EXPECT_EQ(pg::stat_cost(98, mr::Mode::PreRenewal), 11);
}

TEST(Progression, BR_MAP_022_StatRaiseRequiresPositiveAmountJobMaxAndPoints) {
	const auto cfg = re();
	EXPECT_EQ(pg::raise_stat(50, 0, 100, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::NonPositiveAmount);
	EXPECT_EQ(pg::raise_stat(99, 1, 100, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::AboveJobMax);
	EXPECT_TRUE(pg::raise_stat(99, 1, 100, mr::ClassTier::Third, cfg).ok()); // third jobs: 130
	EXPECT_EQ(pg::raise_stat(10, 2, 4, mr::ClassTier::Normal, cfg).error, pg::StatRaiseError::NotEnoughPoints);
	const auto ok = pg::raise_stat(10, 2, 5, mr::ClassTier::Normal, cfg);
	ASSERT_TRUE(ok.ok());
	EXPECT_EQ(ok.new_value, 12);
	EXPECT_EQ(ok.points_spent, 5);
}

TEST(Progression, BR_MAP_023_StatusPointsFromTable) {
	pg::StatPointTable t{{0, 48, 51, 54, 58}};
	EXPECT_EQ(pg::status_points_gained(t, 1, 2), 3);
	EXPECT_EQ(pg::status_points_gained(t, 1, 4), 10);
}

TEST(Progression, BR_MAP_024_SingleLevelUpCapsOverflowAndHeals) {
	// Level 1, need 100; gain 1000 without multi_level_up -> level 2, exp capped at next(2)-1 = 199.
	const auto r = pg::apply_base_exp(1, 0, 1000, small_table(), re());
	EXPECT_EQ(r.level, 2);
	EXPECT_EQ(r.exp, 199);
	EXPECT_TRUE(r.full_heal);
	EXPECT_TRUE(r.recheck_equipment);
}

TEST(Progression, BR_MAP_024_MultiLevelUpWhenEnabled) {
	auto cfg = re();
	cfg.multi_level_up = true;
	const auto r = pg::apply_base_exp(1, 0, 900, small_table(), cfg); // 100 + 200 + 300 = 600, rest 300
	EXPECT_EQ(r.level, 4);
	EXPECT_EQ(r.exp, 300);
	EXPECT_EQ(r.levels_gained, 3);
}

TEST(Progression, BR_MAP_025_JobLevelUpGrantsOneSkillPoint) {
	const auto r = pg::apply_job_exp(3, 250, 100, small_table(), re());
	EXPECT_EQ(r.level, 4);
	EXPECT_EQ(r.skill_points_gained, 1);
}

TEST(Progression, BR_MAP_026_NoExpWhenDeadOrOffMap) {
	EXPECT_EQ(pg::gain_exp({.dead = true, .base_exp = 1000}, re()).base_exp, 0);
	EXPECT_EQ(pg::gain_exp({.on_map = false, .base_exp = 1000}, re()).base_exp, 0);
}

TEST(Progression, BR_MAP_026_GuildTaxBeforeBonusAndMaxLevelClamp) {
	// 1000 base, 10% tax -> 900, then +50% -> 1350. Guild gets the untaxed-share 100.
	const auto g = pg::gain_exp({.base_exp = 1000, .job_exp = 500, .in_guild = true, .guild_tax_pct = 10,
	                             .bonus_pct = 50},
	                            re());
	EXPECT_EQ(g.base_exp, 1350);
	EXPECT_EQ(g.guild_exp, 100);
	EXPECT_EQ(g.job_exp, 750);
	EXPECT_EQ(pg::gain_exp({.base_exp = 1000, .at_max_base_level = true}, re()).base_exp, 0);
}

TEST(Progression, BR_MAP_027_GuildTaxWorkedExample) {
	// 1,000 exp at 10% -> 900 to the player, 100 to the guild.
	const auto s = pg::guild_tax(1000, 10, re());
	EXPECT_EQ(s.player, 900);
	EXPECT_EQ(s.guild, 100);
	EXPECT_EQ(pg::guild_tax(1000, 90, re()).guild, 500); // clamped to guild_exp_limit 50%
}

TEST(Progression, BR_MAP_028_EvenShareWorkedExample) {
	// 3,000 exp, 3 members -> 1,000 each.
	constexpr std::uint64_t kMaxExp = INT64_MAX;
	EXPECT_EQ(pg::even_share(3000, 3, kMaxExp, re()), 1000u);
	EXPECT_EQ(pg::even_share(3000, 0, kMaxExp, re()), 0u);
}

TEST(Progression, BR_MAP_028_EvenShareDividesBeforeTheBonus) {
	// party_exp_share: exp / n first, then x (100 + bonus x (n-1)) / 100 in double, truncated.
	auto cfg = re();
	cfg.party_even_share_bonus = 10;
	constexpr std::uint64_t kMaxExp = INT64_MAX;
	EXPECT_EQ(pg::even_share(10, 3, kMaxExp, cfg), 3u); // 10/3 = 3; 3 x 1.2 = 3.6 -> 3 (not 12/3 = 4)
	EXPECT_EQ(pg::even_share(3000, 3, kMaxExp, cfg), 1200u);
	EXPECT_EQ(pg::even_share(3000, 1, kMaxExp, cfg), 3000u); // no bonus alone
	EXPECT_EQ(pg::even_share(2, 3, kMaxExp, cfg), 0u);
	// Capped at MAX_EXP (INT32_MAX before PACKETVER 20170830).
	cfg.party_even_share_bonus = 100;
	EXPECT_EQ(pg::even_share(4000000000ull, 2, INT32_MAX, cfg), static_cast<std::uint64_t>(INT32_MAX));
}

TEST(Progression, BR_MAP_029_LevelPenaltyBucketsRenewalOnly) {
	pg::LevelPenaltyTable t{2, {50, 90, 100, 110, 120}}; // diff -2..+2
	EXPECT_EQ(pg::level_penalty_pct(t, 50, 50, re()), 100);
	EXPECT_EQ(pg::level_penalty_pct(t, 60, 50, re()), 120); // clamped to +2 edge
	EXPECT_EQ(pg::level_penalty_pct(t, 40, 50, re()), 50);
	mr::BattleConfig pre;
	pre.mode = mr::Mode::PreRenewal;
	EXPECT_EQ(pg::level_penalty_pct(t, 40, 50, pre), 100);
}

TEST(Progression, BR_MAP_030_DeathPenaltyWorkedExample) {
	// Type 1 at 1%: next level needs 1,000,000 -> -10,000 exp.
	const auto p = pg::death_penalty({.base_exp = 500000, .base_next = 1000000}, re());
	EXPECT_EQ(p.base_loss, 10000);
}

TEST(Progression, BR_MAP_030_NovicesGvgAndInsuranceExempt) {
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .novice = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .in_gvg = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 500000, .base_next = 1000000, .has_insurance = true}, re()).base_loss, 0);
	EXPECT_EQ(pg::death_penalty({.base_exp = 3000, .base_next = 1000000}, re()).base_loss, 3000); // never below 0
}

TEST(Progression, BR_MAP_031_SuperNoviceAngelAt99PercentOncePerLevel) {
	EXPECT_TRUE(pg::super_novice_angel(990, 1000, false));
	EXPECT_FALSE(pg::super_novice_angel(989, 1000, false));
	EXPECT_FALSE(pg::super_novice_angel(999, 1000, true));
}

TEST(Progression, BR_MAP_031_PercentageIsLegacyDouble) {
	// get_percentage_exp works in double: 98,999,999,999,999,999 converts to 99e15 exactly, so the
	// ratio is 0.99 and the angel triggers although the integer ratio is just below 99%.
	EXPECT_TRUE(pg::super_novice_angel(98999999999999999LL, 100000000000000000LL, false));
	EXPECT_FALSE(pg::super_novice_angel(98, 99, false));
	EXPECT_TRUE(pg::super_novice_angel(5000, 1000, false)); // above 100% too
}

// --- BR-MAP-032: pc.cpp pc_dead (case BL_MOB), status.cpp status_calc_mob_, mob.cpp mob_dead ----

namespace {
mr::BattleConfig mobs_level_up_on() {
	auto c = re();
	c.mobs_level_up = true;
	return c;
}
} // namespace

TEST(Progression, BR_MAP_032_MobLevelsUpOnKillWhenEnabled) {
	// Shipped monster.conf: mobs_level_up: no.
	EXPECT_FALSE(pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 500}, re()).levelled);
	const auto r = pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 500, .victim_max_base_level = 99},
	                                        mobs_level_up_on());
	EXPECT_TRUE(r.levelled);
	EXPECT_EQ(r.level, 11);
	EXPECT_FALSE(r.refresh_name); // show_mob_info 0
	auto cfg = mobs_level_up_on();
	cfg.show_mob_info = 4;
	EXPECT_TRUE(pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 500}, cfg).refresh_name);
}

TEST(Progression, BR_MAP_032_MobLevelUpCappedByVictimMaxLevelGuardiansAndSummonsExcluded) {
	const auto cfg = mobs_level_up_on();
	// `md->level < pc_maxbaselv(sd)`: strict, against the *victim's* max base level.
	EXPECT_FALSE(pg::mob_level_after_kill({.mob_level = 99, .mob_hp = 1, .victim_max_base_level = 99}, cfg).levelled);
	const auto r = pg::mob_level_after_kill({.mob_level = 98, .mob_hp = 1, .victim_max_base_level = 99}, cfg);
	EXPECT_TRUE(r.levelled);
	EXPECT_EQ(r.level, 99);
	EXPECT_TRUE(pg::mob_level_after_kill({.mob_level = 99, .mob_hp = 1, .victim_max_base_level = 200}, cfg).levelled);
	EXPECT_FALSE(pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 0}, cfg).levelled); // md->status.hp == 0
	// (uint32)md->level: a negative level compares as huge and never levels.
	EXPECT_FALSE(pg::mob_level_after_kill({.mob_level = -1, .mob_hp = 1, .victim_max_base_level = 99}, cfg).levelled);
	EXPECT_FALSE(pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 1, .guardian = true}, cfg).levelled);
	const auto summon = pg::mob_level_after_kill({.mob_level = 10, .mob_hp = 1, .summoned_ai = true}, cfg);
	EXPECT_FALSE(summon.levelled);
	EXPECT_EQ(summon.level, 10);
}

TEST(Progression, BR_MAP_032_LevelUpHealIsTenPercentOfCurrentHp) {
	// status_percent_heal(src, 10, 0) -> apply_rate(status->hp, 10): of current HP, not max HP.
	EXPECT_EQ(pg::mob_level_up_heal(500, 1000), 50);
	EXPECT_EQ(pg::mob_level_up_heal(5, 1000), 1);   // 0 -> at least 1
	EXPECT_EQ(pg::mob_level_up_heal(995, 1000), 5); // status_heal caps at max HP
	EXPECT_EQ(pg::mob_level_up_heal(1000, 1000), 0);
	// apply_rate divides first above 100000: 123456 / 100 * 10 = 12340 (not 12345).
	EXPECT_EQ(pg::mob_level_up_heal(123456, 500000), 12340);
}

TEST(Progression, BR_MAP_032_LevelledMobStatusAndExpBonus) {
	const pg::MobBaseStatus db{10, 10, 10, 10, 10, 10, 1000, 100, 200};
	// status_calc_mob_ flag&1, diff 3: stats 13; max_hp 1000 + 3 x 13 (the raised VIT) = 1039;
	// max_sp 100 + 3 x 13 = 139; speed 200 - 3 = 197.
	const auto s = pg::levelled_mob_status(db, 13, 10, mobs_level_up_on());
	EXPECT_EQ(s.str, 13);
	EXPECT_EQ(s.luk, 13);
	EXPECT_EQ(s.max_hp, 1039);
	EXPECT_EQ(s.max_sp, 139);
	EXPECT_EQ(s.speed, 197);
	// Speed never drops below 10: cap_value(3, 0, 12 - 10) = 2.
	auto fast = db;
	fast.speed = 12;
	EXPECT_EQ(pg::levelled_mob_status(fast, 13, 10, mobs_level_up_on()).speed, 10);
	// Disabled, or not above the db level: base status unchanged.
	EXPECT_EQ(pg::levelled_mob_status(db, 13, 10, re()).max_hp, 1000);
	EXPECT_EQ(pg::levelled_mob_status(db, 10, 10, mobs_level_up_on()).str, 10);
	// mob_dead: bonus += (level - db level) x mobs_level_up_exp_rate (shipped 1).
	EXPECT_EQ(pg::mob_level_up_exp_bonus(13, 10, mobs_level_up_on()), 3);
	auto rate5 = mobs_level_up_on();
	rate5.mobs_level_up_exp_rate = 5;
	EXPECT_EQ(pg::mob_level_up_exp_bonus(13, 10, rate5), 15);
	EXPECT_EQ(pg::mob_level_up_exp_bonus(13, 10, re()), 0);
}

// --- BR-MAP-033: pc.cpp pc_dead (bone_drop) ---------------------------------------------------

TEST(Progression, BR_MAP_033_SkullDropFollowsBoneDrop) {
	EXPECT_FALSE(pg::death_skull_drop({150000}, true, re()).has_value()); // shipped bone_drop: 0
	auto pvp_only = re();
	pvp_only.bone_drop = 1;
	EXPECT_TRUE(pg::death_skull_drop({150000}, true, pvp_only).has_value());
	EXPECT_FALSE(pg::death_skull_drop({150000}, false, pvp_only).has_value());
	auto always = re();
	always.bone_drop = 2;
	EXPECT_TRUE(pg::death_skull_drop({150000}, false, always).has_value());
}

TEST(Progression, BR_MAP_033_SkullCarriesVictimCharIdAsCreator) {
	auto cfg = re();
	cfg.bone_drop = 2;
	const auto skull = pg::death_skull_drop({150000}, false, cfg);
	ASSERT_TRUE(skull.has_value());
	EXPECT_EQ(skull->nameid, 7420); // ITEMID_SKULL_
	EXPECT_EQ(skull->amount, 1);
	EXPECT_TRUE(skull->identify);
	// 150000 = 0x0002'49F0 -> GetWord(.., 0) = 0x49F0, GetWord(.., 1) = 0x0002.
	const std::array<std::int32_t, 4> cards{0x00FE, 0, 0x49F0, 2};
	EXPECT_EQ(skull->card, cards);
}

TEST(Progression, BR_MAP_033_MatchesGoldenSkullDrops) {
	// Captured from legacy pc_dead (the bone_drop test and the skull as they were before the rule moved
	// here, evaluated next to it on every death: tools/golden/capture.sh re). bone_drop 0/1/2 on maps with
	// and without the pvp mapflag, for the capture character.
	const auto vectors = golden::load("skull_drop_re.jsonl");
	ASSERT_GE(vectors.size(), 6u) << "golden vectors missing; run tools/golden/capture.sh re";
	bool dropped = false, kept = false;
	for (const auto& v : vectors) {
		auto cfg = re();
		cfg.bone_drop = static_cast<std::int32_t>(golden::at(v, "cfg_bone_drop"));
		const auto skull = pg::death_skull_drop({static_cast<std::int32_t>(golden::at(v, "char_id"))},
		                                         golden::at(v, "pvp_map") != 0, cfg);
		ASSERT_EQ(skull.has_value(), golden::at(v, "legacy_drops") != 0) << golden::str(v);
		(skull ? dropped : kept) = true;
		if (!skull) continue;
		EXPECT_EQ(skull->nameid, golden::at(v, "legacy_nameid")) << golden::str(v);
		EXPECT_EQ(skull->amount, 1) << golden::str(v);
		EXPECT_EQ(skull->identify ? 1 : 0, golden::at(v, "legacy_identify")) << golden::str(v);
		EXPECT_EQ(skull->card[0], golden::at(v, "legacy_card0")) << golden::str(v);
		EXPECT_EQ(skull->card[1], 0) << golden::str(v);
		EXPECT_EQ(skull->card[2], golden::at(v, "legacy_card2")) << golden::str(v);
		EXPECT_EQ(skull->card[3], golden::at(v, "legacy_card3")) << golden::str(v);
	}
	EXPECT_TRUE(dropped && kept) << "the capture must cover both outcomes";
}

// --- BR-MAP-034: pc.cpp pc_dead (MF_PVP_NIGHTMAREDROP) ---------------------------------------

namespace {
std::vector<pg::NightmareSlot> nightmare_inventory() {
	return {
	    {},                                               // 0: empty
	    {.nameid = 501, .amount = 10},                    // 1: potions
	    {.nameid = 2301, .amount = 1, .equipped = true},  // 2: equipped armor
	    {.nameid = 909, .amount = 5, .droppable = false}, // 3: pc_candrop fails
	};
}
} // namespace

TEST(Progression, BR_MAP_034_RandomDropRollsChanceThenPicksAnEligibleSlot) {
	const pg::NightmareDropRule rule{-1, 5000, pg::kNightmareDropAll};
	// Eligible slots are [1, 2]. rnd()%10000 = 4999 < 5000, then rnd() % 2 = 1 -> slot 2.
	mr::SequenceRng hit{{4999, 1}};
	EXPECT_EQ(pg::nightmare_drop_pick(rule, nightmare_inventory(), hit), std::optional<std::size_t>{2});
	EXPECT_EQ(hit.calls(), 2u);
	mr::SequenceRng miss{{5000}};
	EXPECT_FALSE(pg::nightmare_drop_pick(rule, nightmare_inventory(), miss).has_value());
	EXPECT_EQ(miss.calls(), 1u);
	// "inventory" only takes unequipped items: eligible [1].
	mr::SequenceRng inv{{0, 7}};
	EXPECT_EQ(pg::nightmare_drop_pick({-1, 5000, pg::kNightmareDropInventory}, nightmare_inventory(), inv),
	          std::optional<std::size_t>{1});
	// "equip" only takes equipped items: eligible [2].
	mr::SequenceRng eq{{0, 7}};
	EXPECT_EQ(pg::nightmare_drop_pick({-1, 5000, pg::kNightmareDropEquip}, nightmare_inventory(), eq),
	          std::optional<std::size_t>{2});
	// Nothing eligible: no roll at all.
	mr::SequenceRng none{{0}};
	EXPECT_FALSE(pg::nightmare_drop_pick(rule, {{}, {.nameid = 909, .amount = 1, .droppable = false}}, none));
	EXPECT_EQ(none.calls(), 0u);
}

TEST(Progression, BR_MAP_034_ItemDropRollsPerMatchingSlotBeforeTheTypeCheck) {
	const std::vector<pg::NightmareSlot> slots{
	    {.nameid = 501, .amount = 1},                   // unequipped
	    {.nameid = 501, .amount = 1, .equipped = true}, // equipped
	};
	// Legacy condition order: nameid && pc_candrop && rnd()%10000 < per && type. Slot 0 passes
	// its roll but is the wrong type, and the roll is still spent; slot 1 rolls again and drops.
	mr::SequenceRng rng{{100, 200}};
	EXPECT_EQ(pg::nightmare_drop_pick({501, 3000, pg::kNightmareDropEquip}, slots, rng),
	          std::optional<std::size_t>{1});
	EXPECT_EQ(rng.calls(), 2u);
	// Each matching slot gets its own chance: the first roll fails, the second succeeds.
	mr::SequenceRng second{{3000, 2999}};
	EXPECT_EQ(pg::nightmare_drop_pick({501, 3000, pg::kNightmareDropAll}, slots, second),
	          std::optional<std::size_t>{1});
	// A 100% rule still consumes a roll (rnd()%10000 < 10000).
	mr::SequenceRng sure{{9999}};
	EXPECT_EQ(pg::nightmare_drop_pick({501, 10000, pg::kNightmareDropAll}, slots, sure),
	          std::optional<std::size_t>{0});
	EXPECT_EQ(sure.calls(), 1u);
	// Slots without the item are skipped without a roll; drop id 0 is ignored.
	mr::SequenceRng skip{{0}};
	EXPECT_FALSE(pg::nightmare_drop_pick({607, 10000, pg::kNightmareDropAll}, slots, skip).has_value());
	EXPECT_FALSE(pg::nightmare_drop_pick({0, 10000, pg::kNightmareDropAll}, slots, skip).has_value());
	EXPECT_FALSE(pg::nightmare_drop_pick({909, 10000, pg::kNightmareDropAll}, nightmare_inventory(), skip));
	EXPECT_EQ(skip.calls(), 0u);
}

TEST(Progression, BR_MAP_034_RulesApplyInOrderAndSeeEarlierDrops) {
	mr::SequenceRng rng{{0}};
	const std::vector<pg::NightmareDropRule> rules{{2301, 10000, pg::kNightmareDropAll},
	                                               {2301, 10000, pg::kNightmareDropAll}};
	// The only armor drops on the first rule (one unit, unequipped first); the second finds none.
	EXPECT_EQ(pg::nightmare_drops(rules, nightmare_inventory(), rng), (std::vector<std::size_t>{2}));
	// One unit per drop: a stack of 10 potions drops once per rule.
	EXPECT_EQ(pg::nightmare_drops({{501, 10000, pg::kNightmareDropAll}, {501, 10000, pg::kNightmareDropAll}},
	                              nightmare_inventory(), rng),
	          (std::vector<std::size_t>{1, 1}));
	// A dropped equip is unequipped, so a later "equip" rule no longer sees the rest of the stack.
	const std::vector<pg::NightmareSlot> two{{.nameid = 2301, .amount = 2, .equipped = true}};
	EXPECT_EQ(pg::nightmare_drops({{2301, 10000, pg::kNightmareDropAll}, {2301, 10000, pg::kNightmareDropEquip}},
	                              two, rng),
	          (std::vector<std::size_t>{0}));
}

TEST(Progression, BR_MAP_034_RandomDropAlwaysPicksAnEligibleSlotSeeded) {
	mr::SeededRng rng{34};
	const auto inv = nightmare_inventory();
	int drops = 0;
	for (int i = 0; i < 2000; ++i) {
		const auto pick = pg::nightmare_drop_pick({-1, 2500, pg::kNightmareDropAll}, inv, rng);
		if (pick) {
			ASSERT_TRUE(*pick == 1 || *pick == 2);
			++drops;
		}
		ASSERT_FALSE(pg::nightmare_drop_pick({-1, 0, pg::kNightmareDropAll}, inv, rng).has_value());
	}
	EXPECT_GT(drops, 400); // about 25% of 2000
	EXPECT_LT(drops, 600);
}

TEST(Progression, BR_MAP_034_MatchesGoldenNightmareDrops) {
	// Captured from legacy pc_dead (the selection as it was before the rule moved here, run next to it
	// from the same generator state on every death in a pvp_nightmaredrop map: tools/golden/capture.sh re).
	// Each vector holds the rule, the occupied inventory slots and the raw rnd() values the original code
	// drew. Replaying those values through the rule must pick the same slot and draw the same number.
	const auto vectors = golden::load("nightmare_drop_re.jsonl");
	ASSERT_GE(vectors.size(), 20u) << "golden vectors missing; run tools/golden/capture.sh re";
	bool random_rule = false, item_rule = false, picked = false, missed = false, rolled_twice = false;
	for (const auto& v : vectors) {
		const pg::NightmareDropRule rule{static_cast<std::int32_t>(golden::at(v, "drop_id")),
		                                  static_cast<std::int32_t>(golden::at(v, "drop_per")),
		                                  static_cast<std::uint8_t>(golden::at(v, "drop_type"))};
		std::vector<pg::NightmareSlot> slots;
		for (std::int64_t k = 0; k < golden::at(v, "slots"); ++k) {
			const std::string name = "s" + std::to_string(k);
			slots.push_back({static_cast<std::int32_t>(golden::at(v, (name + "_nameid").c_str())),
			                 static_cast<std::int32_t>(golden::at(v, (name + "_amount").c_str())),
			                 golden::at(v, (name + "_equip").c_str()) != 0,
			                 golden::at(v, (name + "_droppable").c_str()) != 0});
		}
		std::vector<std::uint32_t> draws;
		for (std::int64_t k = 0; k < golden::at(v, "draws"); ++k) {
			draws.push_back(static_cast<std::uint32_t>(golden::at(v, ("d" + std::to_string(k)).c_str())));
		}
		mr::SequenceRng rng(draws);
		const auto pick = pg::nightmare_drop_pick(rule, slots, rng);
		EXPECT_EQ(pick.has_value() ? static_cast<std::int64_t>(*pick) : -1, golden::at(v, "legacy_pick")) << golden::str(v);
		EXPECT_EQ(rng.calls(), draws.size()) << "the rule drew a different number of values: " << golden::str(v);
		EXPECT_EQ(golden::at(v, "same_stream"), 1) << "the live rule left the generator elsewhere: " << golden::str(v);
		(rule.drop_id == -1 ? random_rule : item_rule) = true;
		(pick ? picked : missed) = true;
		rolled_twice |= draws.size() >= 2;
	}
	EXPECT_TRUE(random_rule && item_rule) << "the capture must cover 'random' and item-id rules";
	EXPECT_TRUE(picked && missed) << "the capture must cover drops and misses";
	EXPECT_TRUE(rolled_twice) << "no random pick (roll + index) captured";
}
