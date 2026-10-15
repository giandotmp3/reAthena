// Acceptance tests: RODEX mail (CAP-11). BR-CHAR-046..049; BR-INTER-018.
#include <gtest/gtest.h>

#include "rathena/char_rules/mail.hpp"

using namespace rathena::char_rules;

namespace {
constexpr UnixTime kNow = 1'760'000'000;
constexpr UnixTime kDay = 86'400;
constexpr CharId kOwner = 150'001;

MailMessage mail(MailId id, MailStatus st = MailStatus::New) {
	MailMessage m;
	m.id = id;
	m.send_id = 150'002;
	m.send_name = "Sender";
	m.dest_id = kOwner;
	m.dest_name = "Owner";
	m.title = "Hello";
	m.status = st;
	m.timestamp = kNow;
	return m;
}
} // namespace

// ---- BR-CHAR-046 -----------------------------------------------------------------------------------
TEST(Mail, BR_CHAR_046_InboxLoadsAtMost30AndMarksNewAsUnread) {
	std::vector<MailMessage> stored;
	for (MailId i = 40; i >= 1; --i) stored.push_back(mail(i));
	stored.push_back(mail(100, MailStatus::Deleted));
	auto other = mail(101);
	other.dest_id = 150'777;
	stored.push_back(other);

	const auto inbox = load_inbox(stored, kOwner);
	ASSERT_EQ(inbox.messages.size(), kMailMaxInbox);
	EXPECT_TRUE(inbox.full);
	EXPECT_EQ(inbox.messages.front().id, 1);
	EXPECT_EQ(inbox.messages.back().id, 30);
	EXPECT_EQ(inbox.mark_unread.size(), 30u);
	for (const auto& m : inbox.messages) EXPECT_EQ(m.status, MailStatus::Unread);
}

TEST(Mail, BR_CHAR_046_ReadMailIsNotRemarked) {
	const auto inbox = load_inbox({mail(1, MailStatus::Read), mail(2, MailStatus::Unread)}, kOwner);
	EXPECT_FALSE(inbox.full);
	EXPECT_TRUE(inbox.mark_unread.empty());
	EXPECT_EQ(inbox.messages[0].status, MailStatus::Read);
}

// ---- BR-CHAR-047 -----------------------------------------------------------------------------------
TEST(Mail, BR_CHAR_047_ReturnedAfterReturnDaysAsReAndDeletedAfterDeleteDays) {
	MailConfig cfg; // shipped 15 / 15
	auto m = mail(1);
	m.timestamp = kNow - 15 * kDay;
	EXPECT_EQ(mail_expiry_action(m, cfg, kNow), ExpiryAction::ReturnToSender);
	m.timestamp = kNow - 15 * kDay + 1;
	EXPECT_EQ(mail_expiry_action(m, cfg, kNow), ExpiryAction::Keep);

	const auto r = make_returned_mail(mail(1), kNow);
	EXPECT_EQ(r.title, "RE:Hello");
	EXPECT_EQ(r.dest_id, 150'002u);
	EXPECT_EQ(r.send_id, kOwner);
	EXPECT_EQ(r.type, MailInboxType::Returned);
	EXPECT_EQ(r.status, MailStatus::New);

	auto ret = r;
	ret.timestamp = kNow - 15 * kDay;
	EXPECT_EQ(mail_expiry_action(ret, cfg, kNow), ExpiryAction::Delete);
	cfg.delete_days = 0;
	EXPECT_EQ(mail_expiry_action(ret, cfg, kNow), ExpiryAction::Keep);
}

TEST(Mail, BR_CHAR_047_SweepRunsEveryMinute) {
	GTEST_SKIP() << "PENDING BR-CHAR-047: 60 s mail return/delete timer in the char-server loop delegates to "
	                "mail_expiry_action; integration test once the char delegate lands (phase 3)";
}

// ---- BR-CHAR-048 / BR-INTER-018 --------------------------------------------------------------------
TEST(Mail, BR_CHAR_048_AttachmentsZeroedBeforeReleaseNoDuplication) {
	auto stored = mail(5, MailStatus::Read);
	stored.zeny = 10'000;
	stored.items[0] = Item{501, 3};
	stored.items[2] = Item{607, 1};
	const auto rel = retrieve_attachments(stored, kOwner, kAttachAll, MailConfig{});
	ASSERT_TRUE(rel);
	EXPECT_EQ(rel->zeny, 10'000);
	ASSERT_EQ(rel->items.size(), 2u);
	EXPECT_EQ(stored.zeny, 0);
	for (const auto& it : stored.items) EXPECT_EQ(it.nameid, 0u);

	// Second retrieval (replayed request) releases nothing.
	EXPECT_FALSE(retrieve_attachments(stored, kOwner, kAttachAll, MailConfig{}));
}

TEST(Mail, BR_CHAR_048_PartialRetrievalLeavesOtherAttachment) {
	auto stored = mail(6, MailStatus::Read);
	stored.zeny = 50;
	stored.items[0] = Item{501, 1};
	const auto rel = retrieve_attachments(stored, kOwner, kAttachZeny, MailConfig{});
	ASSERT_TRUE(rel);
	EXPECT_TRUE(rel->items.empty());
	EXPECT_EQ(stored.items[0].nameid, 501u);
}

TEST(Mail, BR_INTER_018_CharServerCopyIsTheAuthority) {
	auto stored = mail(7, MailStatus::Read);
	stored.zeny = 100;
	// Only the addressee may retrieve; the map server cannot claim amounts.
	EXPECT_FALSE(retrieve_attachments(stored, 150'999, kAttachAll, MailConfig{}));
	EXPECT_EQ(stored.zeny, 100);
	MailConfig strict;
	strict.retrieve_unread = false;
	auto unread = mail(8, MailStatus::Unread);
	unread.zeny = 5;
	EXPECT_FALSE(retrieve_attachments(unread, kOwner, kAttachAll, strict));
}

TEST(Mail, BR_INTER_018_StorageAuthorityAlsoSitsWithCharServer) {
	GTEST_SKIP() << "PENDING BR-INTER-018: account/guild storage open-save ownership (char is the system of record) "
	                "is enforced by the storage repositories in the persistence unit";
}

// ---- BR-CHAR-049 -----------------------------------------------------------------------------------
TEST(Mail, BR_CHAR_049_UnknownRecipientMeansMailIsNotSaved) {
	const auto out = prepare_rodex_send(mail(0), std::nullopt, kNow);
	EXPECT_EQ(out.result, SendResult::UnknownRecipient);
	EXPECT_FALSE(out.to_save);
}

TEST(Mail, BR_CHAR_049_RodexResolvesRecipientIncludingSelf) {
	auto draft = mail(0);
	draft.send_id = kOwner;
	const auto out = prepare_rodex_send(draft, RecipientInfo{kOwner, 2'000'001, "Owner"}, kNow);
	ASSERT_EQ(out.result, SendResult::Saved);
	EXPECT_EQ(out.to_save->dest_id, kOwner);
	EXPECT_EQ(out.to_save->status, MailStatus::New);
}
