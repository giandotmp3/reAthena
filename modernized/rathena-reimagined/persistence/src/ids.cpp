#include "rathena/persistence/ids.hpp"

namespace rathena::persistence {

std::string_view to_string(AccountIdError e) noexcept {
	switch (e) {
	case AccountIdError::None: return "none";
	case AccountIdError::ZeroProhibited: return "account id 0 is prohibited";
	case AccountIdError::AboveMaximum: return "account id above END_ACCOUNT_NUM";
	}
	return "unknown";
}

AccountIdAllocation allocate_account_id(std::optional<std::uint32_t> requested,
                                        std::optional<std::uint32_t> current_max) noexcept {
	std::uint64_t id = 0;
	if (requested) {
		id = *requested; // caller specifies it manually (legacy account.cpp:299-301)
	} else {
		const std::uint64_t next = current_max ? std::uint64_t{*current_max} + 1 : 0;
		id = next < kStartAccountNum ? std::uint64_t{kStartAccountNum} : next;
	}
	if (id == 0) return {std::nullopt, AccountIdError::ZeroProhibited};
	if (id > kEndAccountNum) return {std::nullopt, AccountIdError::AboveMaximum};
	return {AccountId{static_cast<std::uint32_t>(id)}, AccountIdError::None};
}

} // namespace rathena::persistence
