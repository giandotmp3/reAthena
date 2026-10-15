#pragma once
// Strong identifier types and the id ranges of BR-INTER-009 / BR-LOGIN-008.
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rathena::persistence {

template <class Tag, class Rep>
struct StrongId {
	Rep value{};
	constexpr StrongId() = default;
	constexpr explicit StrongId(Rep v) : value(v) {}
	friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;
};

using AccountId = StrongId<struct AccountIdTag, std::uint32_t>;
using CharId = StrongId<struct CharIdTag, std::uint32_t>;
using PartyId = StrongId<struct PartyIdTag, std::uint32_t>;
using GuildId = StrongId<struct GuildIdTag, std::uint32_t>;
using MailId = StrongId<struct MailIdTag, std::uint64_t>;

/// Legacy `START_ACCOUNT_NUM` / `END_ACCOUNT_NUM` / `START_CHAR_NUM` (common/mmo.hpp:176-178).
inline constexpr std::uint32_t kStartAccountNum = 2'000'000;
inline constexpr std::uint32_t kEndAccountNum = 100'000'000;
inline constexpr std::uint32_t kStartCharNum = 150'000;

enum class AccountIdError {
	None,
	ZeroProhibited, ///< resulting id is 0
	AboveMaximum,   ///< resulting id > kEndAccountNum
};

std::string_view to_string(AccountIdError e) noexcept;

struct AccountIdAllocation {
	std::optional<AccountId> id;
	AccountIdError error = AccountIdError::None;
	[[nodiscard]] bool ok() const noexcept { return id.has_value(); }
};

/// BR-LOGIN-008: choose the id for a new account.
///  - `requested` set (caller chose the id, e.g. a server account): it is used as-is.
///  - otherwise: `current_max` + 1 (empty table -> nothing), raised to at least kStartAccountNum.
///  - The result is refused if it is 0 or greater than kEndAccountNum.
/// Arithmetic is widened so `current_max == UINT32_MAX` is refused rather than wrapping to 0.
AccountIdAllocation allocate_account_id(std::optional<std::uint32_t> requested,
                                        std::optional<std::uint32_t> current_max) noexcept;

} // namespace rathena::persistence
