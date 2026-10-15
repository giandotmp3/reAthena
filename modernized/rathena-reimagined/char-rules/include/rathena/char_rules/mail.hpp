// rules/mail: CAP-11 mail, RODEX only (spec §1.1 drop decision 1: no auction, no pre-RODEX).
// BR-CHAR-046..049; BR-INTER-018 (mail/storage authority sits with the char server).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rathena/char_rules/config.hpp"
#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

enum class MailStatus : std::uint8_t { New = 0, Unread = 1, Read = 2, Deleted = 3 };
enum class MailInboxType : std::uint8_t { Normal = 0, Account = 1, Returned = 2 };

struct MailMessage {
	MailId id = 0;
	CharId send_id = 0;
	std::string send_name;
	CharId dest_id = 0;
	AccountId dest_account_id = 0;
	std::string dest_name;
	std::string title;
	std::string body;
	MailStatus status = MailStatus::New;
	MailInboxType type = MailInboxType::Normal;
	UnixTime timestamp = 0;
	std::int64_t zeny = 0;
	std::array<Item, kMailMaxItem> items{};
};

/// BR-CHAR-046: inbox load returns at most 30 (lowest ids first, status < Deleted); NEW -> UNREAD.
struct InboxLoad {
	std::vector<MailMessage> messages;
	bool full = false;               ///< more than 30 were available
	std::vector<MailId> mark_unread; ///< persistence: UPDATE status NEW -> UNREAD
};
InboxLoad load_inbox(std::vector<MailMessage> stored, CharId owner);

/// BR-CHAR-047 expiry sweep result.
enum class ExpiryAction : std::uint8_t { Keep, ReturnToSender, Delete };
ExpiryAction mail_expiry_action(const MailMessage& m, const MailConfig& cfg, UnixTime now);
/// Build the "RE:" returned copy (sender/recipient swapped, status NEW, type Returned, new timestamp).
MailMessage make_returned_mail(const MailMessage& m, UnixTime now);

/// BR-CHAR-048 / BR-INTER-018: attachment retrieval. The char server's stored copy is the only
/// authority; zeny and items are zeroed in the store *before* the release payload is produced.
enum AttachmentMask : std::uint8_t { kAttachNone = 0, kAttachZeny = 1, kAttachItems = 2, kAttachAll = 3 };
struct AttachmentRelease {
	MailId mail_id = 0;
	CharId char_id = 0;
	std::int64_t zeny = 0;
	std::vector<Item> items;
};
/// Mutates `stored` (zeroing) and returns what is released; nullopt when nothing may be released.
std::optional<AttachmentRelease> retrieve_attachments(MailMessage& stored, CharId requester, std::uint8_t mask,
                                                      const MailConfig& cfg);

/// BR-CHAR-049 (RODEX): recipient resolution. Unknown recipient -> mail is not saved.
struct RecipientInfo {
	CharId char_id = 0;
	AccountId account_id = 0;
	std::string name;
};
/// RODEX allows mail to yourself (int_mail.cpp:640); the pre-RODEX same-account branch is dropped.
enum class SendResult : std::uint8_t { Saved, UnknownRecipient };
struct SendOutcome {
	SendResult result = SendResult::UnknownRecipient;
	std::optional<MailMessage> to_save;
};
SendOutcome prepare_rodex_send(MailMessage draft, const std::optional<RecipientInfo>& recipient, UnixTime now);

} // namespace rathena::char_rules
