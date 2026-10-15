#include "rathena/char_rules/mail.hpp"

#include <algorithm>

namespace rathena::char_rules {

InboxLoad load_inbox(std::vector<MailMessage> stored, CharId owner) {
	InboxLoad out;
	std::erase_if(stored, [&](const MailMessage& m) { return m.dest_id != owner || m.status >= MailStatus::Deleted; });
	std::sort(stored.begin(), stored.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	out.full = stored.size() > kMailMaxInbox;
	if (stored.size() > kMailMaxInbox) stored.resize(kMailMaxInbox);
	for (auto& m : stored) {
		if (m.status == MailStatus::New) {
			m.status = MailStatus::Unread;
			out.mark_unread.push_back(m.id);
		}
	}
	out.messages = std::move(stored);
	return out;
}

ExpiryAction mail_expiry_action(const MailMessage& m, const MailConfig& cfg, UnixTime now) {
	constexpr UnixTime kDay = 24 * 60 * 60;
	if (m.type == MailInboxType::Normal && cfg.return_days > 0 && m.timestamp + cfg.return_days * kDay <= now) {
		return ExpiryAction::ReturnToSender;
	}
	if (m.type == MailInboxType::Returned && cfg.delete_days > 0 && m.timestamp + cfg.delete_days * kDay <= now) {
		return ExpiryAction::Delete;
	}
	return ExpiryAction::Keep;
}

MailMessage make_returned_mail(const MailMessage& m, UnixTime now) {
	MailMessage r = m;
	std::swap(r.send_id, r.dest_id);
	std::swap(r.send_name, r.dest_name);
	r.title = ("RE:" + m.title).substr(0, std::min<std::size_t>(m.title.size() + 3, 40));
	r.status = MailStatus::New;
	r.type = MailInboxType::Returned;
	r.timestamp = now;
	return r;
}

std::optional<AttachmentRelease> retrieve_attachments(MailMessage& stored, CharId requester, std::uint8_t mask,
                                                      const MailConfig& cfg) {
	if ((mask & kAttachAll) == 0) return std::nullopt;
	if (stored.dest_id != requester) return std::nullopt;
	if (!cfg.retrieve_unread && stored.status != MailStatus::Read) return std::nullopt;

	AttachmentRelease rel;
	rel.mail_id = stored.id;
	rel.char_id = requester;
	bool any = false;
	if ((mask & kAttachZeny) && stored.zeny > 0) {
		rel.zeny = stored.zeny;
		stored.zeny = 0; // zero before release (no duplication)
		any = true;
	}
	if (mask & kAttachItems) {
		for (auto& it : stored.items) {
			if (it.nameid > 0 && it.amount > 0) {
				rel.items.push_back(it);
				any = true;
			}
			it = Item{}; // attachments deleted before release
		}
	}
	if (!any) return std::nullopt;
	return rel;
}

SendOutcome prepare_rodex_send(MailMessage draft, const std::optional<RecipientInfo>& recipient, UnixTime now) {
	if (!recipient || recipient->char_id == 0) return {SendResult::UnknownRecipient, std::nullopt};
	draft.dest_id = recipient->char_id;
	draft.dest_account_id = recipient->account_id;
	draft.dest_name = recipient->name;
	draft.status = MailStatus::New;
	draft.type = MailInboxType::Normal;
	draft.timestamp = now;
	return {SendResult::Saved, std::move(draft)};
}

} // namespace rathena::char_rules
