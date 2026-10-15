#pragma once
// Repository contracts. Per architecture §9 m6 these are interfaces only because a test fake
// exists (in_memory.hpp); production uses the SQL classes (sql_repositories.hpp).
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "rathena/persistence/entities.hpp"
#include "rathena/persistence/ids.hpp"

namespace rathena::persistence {

class AccountRepository {
public:
	virtual ~AccountRepository() = default;
	[[nodiscard]] virtual std::optional<Account> find(AccountId id) const = 0;
	/// BR-LOGIN-038 lookup; `case_sensitive` selects BINARY comparison. More than one match ->
	/// nullopt (deny).
	[[nodiscard]] virtual std::optional<Account> find_by_userid(std::string_view userid,
	                                                            bool case_sensitive) const = 0;
	/// Inserts `account`. If `requested` is set it is the id to use, otherwise BR-LOGIN-008
	/// allocation (max+1, >= 2,000,000, <= 100,000,000). On success `account.id` is updated.
	virtual AccountIdAllocation create(Account& account, std::optional<std::uint32_t> requested) = 0;
	AccountIdAllocation create(Account& account) { return create(account, std::nullopt); }
	virtual void save(const Account& account) = 0;
	/// BR-LOGIN-037: the account row and all of its `##` registry rows are deleted in ONE
	/// transaction. Returns false (and changes nothing) on any failure.
	virtual bool remove(AccountId id) = 0;

	virtual void set_global_reg(AccountId id, const GlobalRegNum& reg) = 0;
	virtual void set_global_reg(AccountId id, const GlobalRegStr& reg) = 0;
	[[nodiscard]] virtual std::vector<GlobalRegNum> global_regs_num(AccountId id) const = 0;
	[[nodiscard]] virtual std::vector<GlobalRegStr> global_regs_str(AccountId id) const = 0;
};

class CharacterRepository {
public:
	virtual ~CharacterRepository() = default;
	[[nodiscard]] virtual std::optional<Character> find(CharId id) const = 0;
	[[nodiscard]] virtual std::vector<Character> list_by_account(AccountId account) const = 0;
	[[nodiscard]] virtual std::optional<Character> find_by_name(std::string_view name) const = 0;
	/// Assigns an id >= kStartCharNum; throws RepositoryError on a duplicate name.
	virtual CharId create(Character character) = 0;
	/// Persists the character and its inventory/cart through the one Item mapping.
	virtual void save(const Character& character) = 0;
	/// BR-CHAR-013: removes the character and all related data in the order of
	/// character_delete_plan() (character_delete.hpp).
	virtual bool remove(CharId id) = 0;
};

class PartyRepository {
public:
	virtual ~PartyRepository() = default;
	[[nodiscard]] virtual std::optional<Party> find(PartyId id) const = 0;
	virtual PartyId create(Party party) = 0;
	virtual void save(const Party& party) = 0;
	virtual bool remove(PartyId id) = 0;
};

class GuildRepository {
public:
	virtual ~GuildRepository() = default;
	[[nodiscard]] virtual std::optional<Guild> find(GuildId id) const = 0;
	virtual GuildId create(Guild guild) = 0;
	virtual void save(const Guild& guild) = 0;
	virtual bool remove(GuildId id) = 0;
};

class MailRepository {
public:
	virtual ~MailRepository() = default;
	[[nodiscard]] virtual std::optional<Mail> find(MailId id) const = 0;
	/// Newest first, at most `limit` (BR-CHAR-046 uses 30; the rule lives in char-rules).
	[[nodiscard]] virtual std::vector<Mail> inbox(CharId dest, std::size_t limit) const = 0;
	/// Throws RepositoryError if attachments exceed kMailMaxItems.
	virtual MailId create(Mail mail) = 0;
	virtual void save(const Mail& mail) = 0;
	virtual bool remove(MailId id) = 0;
};

} // namespace rathena::persistence
