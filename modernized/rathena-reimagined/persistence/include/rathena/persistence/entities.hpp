#pragma once
// Persistent aggregates owned by the persistence repositories (spec §2.2, §2.3).
// "0 means none" references are modelled as std::optional; the mapping converts 0 <-> nullopt
// until M3a turns those columns into real NULLs.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rathena/persistence/ids.hpp"
#include "rathena/persistence/item.hpp"
#include "rathena/persistence/zeny.hpp"

namespace rathena::persistence {

inline constexpr std::size_t kNameLength = 24;      ///< NAME_LENGTH incl. NUL (BR-INTER-009)
inline constexpr std::size_t kMaxPartyMembers = 12; ///< MAX_PARTY
inline constexpr std::size_t kMailMaxItems = 5;     ///< RODEX MAIL_MAX_ITEM
inline constexpr std::size_t kMailTitleLength = 40;
inline constexpr std::size_t kMailBodyLength = 500;

enum class Sex : char { Male = 'M', Female = 'F', Server = 'S' };

/// Password-at-rest scheme added by M6 (`login.pass_scheme`).
enum class PassScheme { Plain, Md5, Argon2id };

struct Account {
	AccountId id{};
	std::string userid;
	std::string pass_hash;             ///< opaque; Argon2id PHC string after rehash
	PassScheme pass_scheme = PassScheme::Argon2id;
	Sex sex = Sex::Male;
	std::string email;
	std::int32_t group_id = 0;         ///< widened (BR-INTER-003)
	std::uint32_t state = 0;
	std::int64_t unban_time = 0;
	std::int64_t expiration_time = 0;
	std::uint32_t logincount = 0;
	std::uint8_t character_slots = 0;
	std::int64_t vip_time = 0;
	std::int32_t old_group = 0;
	friend bool operator==(const Account&, const Account&) = default;
};

/// `##` registry entries (global_acc_reg_num / _str), owned by login (BR-LOGIN-036).
struct GlobalRegNum {
	std::string key;
	std::uint32_t index = 0;
	std::int64_t value = 0;
	friend bool operator==(const GlobalRegNum&, const GlobalRegNum&) = default;
};
struct GlobalRegStr {
	std::string key;
	std::uint32_t index = 0;
	std::string value;
	friend bool operator==(const GlobalRegStr&, const GlobalRegStr&) = default;
};

struct Character {
	CharId id{};
	AccountId account{};
	std::uint8_t slot = 0;
	std::string name;
	std::uint32_t job_class = 0;
	std::uint32_t base_level = 1;
	Zeny zeny{};
	std::optional<PartyId> party;
	std::optional<GuildId> guild;
	std::optional<std::uint32_t> clan;
	std::optional<CharId> partner;
	std::optional<CharId> father;
	std::optional<CharId> mother;
	std::optional<std::uint32_t> pet_id;
	std::optional<std::uint32_t> homun_id;
	std::optional<std::uint32_t> elemental_id;
	std::int64_t delete_date = 0;
	std::vector<StoredItem> inventory;
	std::vector<StoredItem> cart;
	friend bool operator==(const Character&, const Character&) = default;
};

struct Party {
	PartyId id{};
	std::string name;
	AccountId leader_account{};
	CharId leader_char{};
	bool exp_share = false;
	friend bool operator==(const Party&, const Party&) = default;
};

struct Guild {
	GuildId id{};
	std::string name;
	CharId master{};
	std::string master_name; ///< denormalized (D5); kept in sync by rename propagation
	std::uint32_t guild_lv = 1;
	std::vector<CharId> members;
	std::vector<StoredItem> storage;
	friend bool operator==(const Guild&, const Guild&) = default;
};

enum class MailStatus : std::uint8_t { New = 0, Unread = 1, Read = 2, Returned = 3 };

struct Mail {
	MailId id{};
	std::optional<CharId> sender; ///< nullopt = sent by the server (legacy send_id 0)
	std::string send_name;
	CharId dest{};
	std::string dest_name;
	std::string title;
	std::string message;
	Zeny zeny{};
	std::vector<Item> attachments; ///< <= kMailMaxItems; mapped through ItemTable::MailAttachments
	MailStatus status = MailStatus::New;
	std::int64_t time = 0;
	friend bool operator==(const Mail&, const Mail&) = default;
};

} // namespace rathena::persistence
