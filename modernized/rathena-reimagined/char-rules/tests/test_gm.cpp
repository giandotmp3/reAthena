// Acceptance tests: GM account operations (CAP-21). BR-CHAR-055 [DEFECT fixed: authority re-check].
#include <gtest/gtest.h>

#include "rathena/char_rules/gm.hpp"

using namespace rathena::char_rules;

namespace {
GmContext ctx_with(int requester_level, int target_level, bool permitted = true) {
	GmContext c;
	c.requester_group_level = requester_level;
	c.requester_may = [permitted](GmOp) { return permitted; };
	c.target = GmTarget{2'000'050, 150'050, target_level};
	return c;
}

GmRequest req(GmOp op, std::int32_t timediff = 0) {
	GmRequest r;
	r.requester_aid = 2'000'001;
	r.op = op;
	r.target_name = "Target";
	r.timediff = timediff;
	return r;
}
} // namespace

TEST(Gm, BR_CHAR_055_BlockSetsState5BanUnblockUnbanForwardToLogin) {
	auto out = process_gm_request(req(GmOp::LoginBlock), ctx_with(99, 0));
	ASSERT_EQ(out.result, GmResult::Done);
	EXPECT_EQ(out.forward->opcode, 0x2724);
	EXPECT_EQ(out.forward->value, 5);
	EXPECT_EQ(out.forward->account_id, 2'000'050u);

	out = process_gm_request(req(GmOp::LoginBan, 3600), ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0x2725);
	EXPECT_EQ(out.forward->value, 3600);

	out = process_gm_request(req(GmOp::LoginUnblock), ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0x2724);
	EXPECT_EQ(out.forward->value, 0);

	out = process_gm_request(req(GmOp::LoginUnban), ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0x272a);
}

TEST(Gm, BR_CHAR_055_SexAndVipOps) {
	auto out = process_gm_request(req(GmOp::LoginChangeSex), ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0x2727);
	EXPECT_FALSE(out.answer_requester);
	auto vip = req(GmOp::LoginVip, 86400);
	vip.val1 = 4;
	out = process_gm_request(vip, ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0x2742);
	EXPECT_TRUE(out.answer_requester);
	out = process_gm_request(req(GmOp::ChangeCharSex), ctx_with(99, 0));
	EXPECT_EQ(out.forward->opcode, 0);
	EXPECT_FALSE(out.answer_requester);
}

TEST(Gm, BR_CHAR_055_DefectFixCharServerRechecksRequesterAuthority) {
	// Legacy forwarded whatever the map server sent; the rebuild refuses with code 2.
	EXPECT_EQ(process_gm_request(req(GmOp::LoginBan, 3600), ctx_with(10, 99)).result, GmResult::NotAuthorized);
	EXPECT_EQ(process_gm_request(req(GmOp::LoginBlock), ctx_with(99, 0, /*permitted=*/false)).result,
	          GmResult::NotAuthorized);
	GmContext unknown = ctx_with(99, 0);
	unknown.requester_group_level.reset();
	const auto out = process_gm_request(req(GmOp::LoginBlock), unknown);
	EXPECT_EQ(out.result, GmResult::NotAuthorized);
	EXPECT_FALSE(out.forward.has_value());
}

TEST(Gm, BR_CHAR_055_EqualLevelAllowedServerRequestsBypassPlayerCheck) {
	EXPECT_EQ(process_gm_request(req(GmOp::LoginBlock), ctx_with(60, 60)).result, GmResult::Done);
	auto server = req(GmOp::LoginBlock);
	server.requester_aid = kServerRequester;
	GmContext c;
	c.target = GmTarget{2'000'050, 150'050, 99};
	const auto out = process_gm_request(server, c);
	EXPECT_EQ(out.result, GmResult::Done);
	EXPECT_FALSE(out.answer_requester);
}

TEST(Gm, BR_CHAR_055_NotFoundAndLoginOffline) {
	GmContext none = ctx_with(99, 0);
	none.target.reset();
	EXPECT_EQ(process_gm_request(req(GmOp::LoginBlock), none).result, GmResult::NotFound);
	GmContext off = ctx_with(99, 0);
	off.login_connected = false;
	EXPECT_EQ(process_gm_request(req(GmOp::LoginBlock), off).result, GmResult::LoginOffline);
}
