#pragma once
// BR-CHAR-013: character deletion cleans up all related data. The ordered plan below is also the
// cascade order the target schema's foreign keys (foreign_keys.hpp) are verified against.
//
// Target order vs legacy (char.cpp:1615-1751): the legacy code deletes the `char` row BEFORE the
// guild break/leave. Under the target foreign keys every dependent step has to run before the parent row
// goes, so the target moves GuildBreakOrLeave ahead of DeleteCharacterRow. Pet eggs are still
// removed before inventory/cart (they are found through inventory card0 = 256), and received mail
// attachments go before the received mail rows.
#include <span>
#include <string_view>

namespace rathena::persistence {

enum class CleanupStep {
	Divorce,
	DeAdopt,
	LeaveParty,
	DeleteHatchedPet,
	DeleteEggPets,          ///< pets referenced from inventory/cart eggs -- before DeleteInventory
	DeleteHomunculus,
	DeleteElemental,
	DeleteMercenaryOwner,
	DeleteOwnFriends,
	DeleteFromOthersFriends,///< friends in both directions
	DeleteHotkeys,
	DeleteInventory,
	DeleteCart,
	DeleteMemo,
	DeleteCharRegStr,
	DeleteCharRegNum,
	DeleteSkills,
	DeleteReceivedMailAttachments,
	DeleteReceivedMail,
	AnonymizeSentMail,      ///< send_id -> server (NULL after M3a)
	DeleteStatusChanges,
	DeleteBonusScripts,
	DeleteQuests,
	DeleteAchievements,
	GuildBreakOrLeave,      ///< break if master, otherwise leave -- before the parent row
	DeleteCharacterRow,     ///< always last
};

struct CleanupStepInfo {
	CleanupStep step;
	std::string_view logical_table; ///< TableNames field name the step touches
	std::string_view name;
};

/// The ordered BR-CHAR-013 plan (target order, see header comment).
std::span<const CleanupStepInfo> character_delete_plan() noexcept;

std::string_view to_string(CleanupStep s) noexcept;

} // namespace rathena::persistence
