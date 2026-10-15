// Acceptance tests: per-char_id save->auth ordering barrier (architecture §3 char-server row,
// §9 M2, phase-3 exit criterion). A save (0x2b01) is committed before an auth for the same
// char (0x2b26) is answered. Ties into BR-CHAR-031 (map->char auth) and BR-CHAR-032 (save).
#include <gtest/gtest.h>

#include "rathena/char_rules/save_auth_barrier.hpp"

using namespace rathena::char_rules;

namespace {
PendingAuth auth(CharId cid, std::uint64_t seq, ServerId from = 1) {
	return PendingAuth{2'000'000u + cid, cid, 0xABCDu, from, seq};
}
} // namespace

TEST(SaveAuthBarrier, ARCH_M2_AuthWithoutInFlightSaveIsAnsweredNow) {
	SaveAuthBarrier b;
	EXPECT_EQ(b.auth_requested(auth(150'001, 1)), SaveAuthBarrier::AuthGate::AnswerNow);
}

TEST(SaveAuthBarrier, ARCH_M2_AuthWaitsForSameCharSaveThenIsReleased) {
	// Map change: map A saves char C (0x2b01), map B asks for auth of C (0x2b26) before commit.
	SaveAuthBarrier b;
	b.save_received(150'001);
	EXPECT_EQ(b.auth_requested(auth(150'001, 1, 2)), SaveAuthBarrier::AuthGate::Deferred);
	EXPECT_EQ(b.deferred_auths(150'001), 1u);
	const auto released = b.save_committed(150'001);
	ASSERT_EQ(released.size(), 1u);
	EXPECT_EQ(released[0], auth(150'001, 1, 2));
	EXPECT_EQ(b.saves_in_flight(150'001), 0u);
	EXPECT_EQ(b.auth_requested(auth(150'001, 2)), SaveAuthBarrier::AuthGate::AnswerNow);
}

TEST(SaveAuthBarrier, ARCH_M2_OtherCharactersAreNeverBlocked) {
	SaveAuthBarrier b;
	b.save_received(150'001);
	EXPECT_EQ(b.auth_requested(auth(150'002, 1)), SaveAuthBarrier::AuthGate::AnswerNow);
}

TEST(SaveAuthBarrier, ARCH_M2_ReleasedOnlyAfterLastOfSeveralSavesInArrivalOrder) {
	SaveAuthBarrier b;
	b.save_received(150'001);
	b.save_received(150'001);
	EXPECT_EQ(b.auth_requested(auth(150'001, 7)), SaveAuthBarrier::AuthGate::Deferred);
	EXPECT_EQ(b.auth_requested(auth(150'001, 8)), SaveAuthBarrier::AuthGate::Deferred);
	EXPECT_TRUE(b.save_committed(150'001).empty());
	const auto released = b.save_committed(150'001);
	ASSERT_EQ(released.size(), 2u);
	EXPECT_EQ(released[0].request_seq, 7u);
	EXPECT_EQ(released[1].request_seq, 8u);
}

TEST(SaveAuthBarrier, ARCH_M2_FailedSaveRefusesWaitingAuthsInsteadOfServingStaleData) {
	SaveAuthBarrier b;
	b.save_received(150'001);
	b.auth_requested(auth(150'001, 1));
	const auto refused = b.save_failed(150'001);
	ASSERT_EQ(refused.size(), 1u);
	EXPECT_EQ(refused[0].request_seq, 1u);
	EXPECT_EQ(b.deferred_auths(150'001), 0u);
}

TEST(SaveAuthBarrier, ARCH_M2_UnmatchedCommitIsIgnored) {
	SaveAuthBarrier b;
	EXPECT_TRUE(b.save_committed(150'001).empty());
	EXPECT_EQ(b.saves_in_flight(150'001), 0u);
}

TEST(SaveAuthBarrier, BR_CHAR_031_EndToEndBarrierWithLegacyChrifHandlers) {
	GTEST_SKIP() << "PENDING BR-CHAR-031/032: chmapif_parse_reqsavechar and the 0x2b26 auth handler call "
	                "SaveAuthBarrier around their synchronous SQL (phase 3 delegate); dual-map-server test";
}
