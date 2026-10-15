// Acceptance tests: character creation (CAP-05). BR-CHAR-001..008.
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "rathena/char_rules/charlife.hpp"

using namespace rathena::char_rules;

namespace {

auto names_in(std::set<std::string> taken) {
	return [taken = std::move(taken)](std::string_view n) { return taken.count(std::string(n)) != 0; };
}

const auto kNoNames = [](std::string_view) { return false; };

CreateRequest valid_request() {
	CreateRequest r;
	r.name = "Adventurer";
	r.slot = 0;
	r.start_job = Job::Novice;
	r.client_sex = Sex::Female;
	return r;
}

AccountContext account() {
	AccountContext a;
	a.account_id = 2'000'001;
	a.char_slots = 9;
	return a;
}

} // namespace

// ---- BR-CHAR-001 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_001_NameIsTrimmedAndInternalRunsCollapsed) {
	EXPECT_EQ(normalize_char_name("  Bob \t  Smith \r\n"), "Bob Smith");
	EXPECT_EQ(normalize_char_name(std::string(30, 'a')).size(), 23u);
}

TEST(CharCreate, BR_CHAR_001_WorkedExampleBobBangWithAllowListIsMinus2) {
	NameConfig cfg; // shipped: char_name_option 1 (allow-list), letters without '!'
	EXPECT_EQ(check_char_name("Bob!", cfg, kNoNames), NameResult::Invalid);
}

TEST(CharCreate, BR_CHAR_001_RejectsShortControlHashServerAndDuplicates) {
	NameConfig cfg;
	EXPECT_EQ(check_char_name("", cfg, kNoNames), NameResult::Invalid);
	EXPECT_EQ(check_char_name("Bob", cfg, kNoNames), NameResult::Invalid);           // < 4
	EXPECT_EQ(check_char_name("Bo\x01" "b", cfg, kNoNames), NameResult::Invalid);    // control char
	EXPECT_EQ(check_char_name("server", cfg, kNoNames), NameResult::Taken);          // reserved, case-insensitive
	EXPECT_EQ(check_char_name("Taken1", cfg, names_in({"Taken1"})), NameResult::Taken);
	EXPECT_EQ(check_char_name("Valid Name 1", cfg, kNoNames), NameResult::Ok);

	cfg.policy = NameLetterPolicy::Any;
	EXPECT_EQ(check_char_name("#channel", cfg, kNoNames), NameResult::Invalid);
	EXPECT_EQ(check_char_name("Bob!", cfg, kNoNames), NameResult::Ok);
	cfg.policy = NameLetterPolicy::Forbid;
	cfg.letters = "!";
	EXPECT_EQ(check_char_name("Bob!", cfg, kNoNames), NameResult::Invalid);
}

// ---- BR-CHAR-002 / 003 -----------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_002_SlotOutOfRangeIsMinus4OccupiedIsMinus2) {
	EXPECT_EQ(check_slot(9, 9, false), CreateResult::InvalidSlot);
	EXPECT_EQ(check_slot(-1, 9, false), CreateResult::InvalidSlot);
	EXPECT_EQ(check_slot(3, 9, true), CreateResult::Invalid);
	EXPECT_EQ(check_slot(3, 9, false), CreateResult::Ok);
	EXPECT_EQ(static_cast<int>(CreateResult::InvalidSlot), -4);
}

TEST(CharCreate, BR_CHAR_003_CountLimitDependsOnVipBuild) {
	EXPECT_FALSE(may_create_another(9, 9, false));
	EXPECT_TRUE(may_create_another(8, 9, false));
	EXPECT_TRUE(may_create_another(9, 9, true));   // VIP build: < MAX_CHARS (15)
	EXPECT_FALSE(may_create_another(15, 9, true));
}

// ---- BR-CHAR-004 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_004_NewClientsChooseSexOldClientsUseAccountSex) {
	EXPECT_EQ(resolve_new_char_sex(20151001, Sex::Female, Sex::Male), Sex::Female);
	EXPECT_EQ(resolve_new_char_sex(20150930, Sex::Female, Sex::Male), Sex::Male);
	EXPECT_FALSE(resolve_new_char_sex(20211103, Sex::Server, Sex::Male).has_value());
	EXPECT_FALSE(resolve_new_char_sex(20211103, std::nullopt, Sex::Male).has_value());
}

// ---- BR-CHAR-005 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_005_OldClientStatsSum30PairsOf10EachIn1To9) {
	constexpr int kOld = 20120306;
	EXPECT_EQ(validate_start_stats({5, 5, 5, 5, 5, 5}, kOld, 48), 0);
	EXPECT_EQ(validate_start_stats({9, 1, 9, 1, 1, 9}, kOld, 48), 0); // str+int, agi+luk, vit+dex = 10
	EXPECT_FALSE(validate_start_stats({9, 9, 1, 1, 1, 9}, kOld, 48)); // sum 30 but agi+luk = 18
	EXPECT_FALSE(validate_start_stats({10, 0, 5, 0, 5, 10}, kOld, 48));
	EXPECT_FALSE(validate_start_stats({5, 5, 5, 5, 5, 6}, kOld, 48));
}

TEST(CharCreate, BR_CHAR_005_NewClientsStartAllOnesWith48Points) {
	EXPECT_EQ(validate_start_stats(Stats{}, 20211103, 48), 48);
	EXPECT_FALSE(validate_start_stats({2, 1, 1, 1, 1, 1}, 20211103, 48));
}

// ---- BR-CHAR-006 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_006_AllowedStartJobsFollowFlag) {
	EXPECT_TRUE(is_allowed_start_job(Job::Novice, 1, 20211103));
	EXPECT_FALSE(is_allowed_start_job(Job::Summoner, 1, 20211103));
	EXPECT_TRUE(is_allowed_start_job(Job::Summoner, 2, 20211103));
	EXPECT_FALSE(is_allowed_start_job(Job::Novice, 2, 20211103));
	EXPECT_TRUE(is_allowed_start_job(Job::Novice, 3, 20211103));
	EXPECT_FALSE(is_allowed_start_job(Job::Bard, 3, 20211103));
}

// ---- BR-CHAR-007 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_007_WorkedExampleVit1Int1Gives40HpAnd11Sp) {
	EXPECT_EQ(initial_max_hp(1), 40u);
	EXPECT_EQ(initial_max_sp(1), 11u);
	EXPECT_EQ(initial_max_hp(9), 43u);  // 40*109/100
	EXPECT_EQ(initial_max_sp(9), 11u);  // 11*109/100 = 11.99 -> 11
}

TEST(CharCreate, BR_CHAR_007_StartItemsStopAtFirstEmptyAndNeverExceedMaxStartItem) {
	std::vector<StartItem> cfg = {{1201, 1, 2}, {0, 0, 0}, {2301, 1, 16}};
	EXPECT_EQ(start_items_for(cfg).size(), 1u);

	// [DEFECT fix] legacy `k <= MAX_STARTITEM` read one entry past the array.
	std::vector<StartItem> full(kMaxStartItems + 1, StartItem{501, 1, 0});
	EXPECT_EQ(start_items_for(full).size(), kMaxStartItems);
}

TEST(CharCreate, BR_CHAR_007_CreationUsesConfiguredZenyPointAndItems) {
	CreationConfig ccfg;
	ccfg.start_zeny = 500;
	NameConfig ncfg;
	SequenceRng rng({7}); // 7 % 5 start points -> index 2
	const auto out = make_new_char(valid_request(), account(), ccfg, ncfg, rng);
	ASSERT_EQ(out.result, CreateResult::Ok);
	ASSERT_TRUE(out.character);
	EXPECT_EQ(out.character->zeny, 500);
	EXPECT_EQ(out.character->last_point, (MapPoint{"iz_int02", 18, 26}));
	EXPECT_EQ(out.character->save_point, out.character->last_point);
	EXPECT_EQ(out.character->items, ccfg.start_items);
	EXPECT_EQ(out.character->max_hp, 40u);
	EXPECT_EQ(out.character->status_points, 48);
	EXPECT_EQ(out.character->sex, Sex::Female);
}

TEST(CharCreate, BR_CHAR_007_SummonerUsesDoramStartPointAndItems) {
	CreationConfig ccfg;
	NameConfig ncfg;
	SequenceRng rng({3});
	auto req = valid_request();
	req.start_job = Job::Summoner;
	const auto out = make_new_char(req, account(), ccfg, ncfg, rng);
	ASSERT_EQ(out.result, CreateResult::Ok);
	EXPECT_EQ(out.character->last_point.map, "lasa_fild01");
	EXPECT_EQ(out.character->items, ccfg.start_items_doram);
}

// ---- BR-CHAR-008 -----------------------------------------------------------------------------------
TEST(CharCreate, BR_CHAR_008_CharNewNoRefusesAllCreation) {
	CreationConfig ccfg;
	ccfg.char_new = false;
	NameConfig ncfg;
	SequenceRng rng({0});
	EXPECT_EQ(make_new_char(valid_request(), account(), ccfg, ncfg, rng).result, CreateResult::Invalid);
}

TEST(CharCreate, BR_CHAR_008_ErrorCodeMapping) {
	EXPECT_EQ(create_error_to_client_code(CreateResult::NameTaken), 0x00);
	EXPECT_EQ(create_error_to_client_code(CreateResult::Invalid), 0xFF);
	EXPECT_EQ(create_error_to_client_code(CreateResult::Underaged), 0x01);
	EXPECT_EQ(create_error_to_client_code(CreateResult::InvalidSlot), 0x03);
}

TEST(CharCreate, BR_CHAR_002_MakeNewCharChecksSlotBeforeStatsAndOccupancyAfter) {
	CreationConfig ccfg;
	NameConfig ncfg;
	SequenceRng rng({0});
	auto req = valid_request();
	req.slot = 9;
	EXPECT_EQ(make_new_char(req, account(), ccfg, ncfg, rng).result, CreateResult::InvalidSlot);
	auto acc = account();
	acc.slot_occupied = [](int s) { return s == 0; };
	EXPECT_EQ(make_new_char(valid_request(), acc, ccfg, ncfg, rng).result, CreateResult::Invalid);
	acc = account();
	acc.name_exists = names_in({"Adventurer"});
	EXPECT_EQ(make_new_char(valid_request(), acc, ccfg, ncfg, rng).result, CreateResult::NameTaken);
}
