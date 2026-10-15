#include "rathena/persistence/character_delete.hpp"

#include <array>

namespace rathena::persistence {
namespace {

using S = CleanupStep;
constexpr std::array<CleanupStepInfo, 26> kPlan = {{
    {S::Divorce, "character", "divorce"},
    {S::DeAdopt, "character", "de-adopt"},
    {S::LeaveParty, "party", "leave party"},
    {S::DeleteHatchedPet, "pet", "delete hatched pet"},
    {S::DeleteEggPets, "pet", "delete pets held as eggs (inventory + cart)"},
    {S::DeleteHomunculus, "homunculus", "delete homunculus"},
    {S::DeleteElemental, "elemental", "delete elemental"},
    {S::DeleteMercenaryOwner, "mercenary_owner", "delete mercenary owner data"},
    {S::DeleteOwnFriends, "friends", "delete own friend list"},
    {S::DeleteFromOthersFriends, "friends", "delete from other characters' friend lists"},
    {S::DeleteHotkeys, "hotkey", "delete hotkeys"},
    {S::DeleteInventory, "inventory", "delete inventory"},
    {S::DeleteCart, "cart_inventory", "delete cart"},
    {S::DeleteMemo, "memo", "delete memo points"},
    {S::DeleteCharRegStr, "char_reg_str", "delete char registry (str)"},
    {S::DeleteCharRegNum, "char_reg_num", "delete char registry (num)"},
    {S::DeleteSkills, "skill", "delete skills"},
    {S::DeleteReceivedMailAttachments, "mail_attachments", "delete attachments of received mail"},
    {S::DeleteReceivedMail, "mail", "delete received mail"},
    {S::AnonymizeSentMail, "mail", "mark sent mail as sent by the server"},
    {S::DeleteStatusChanges, "sc_data", "delete saved status changes"},
    {S::DeleteBonusScripts, "bonus_script", "delete bonus scripts"},
    {S::DeleteQuests, "quest", "delete quests"},
    {S::DeleteAchievements, "achievement", "delete achievements"},
    {S::GuildBreakOrLeave, "guild", "break guild (master) or leave guild"},
    {S::DeleteCharacterRow, "character", "delete character row"},
}};

} // namespace

std::span<const CleanupStepInfo> character_delete_plan() noexcept { return kPlan; }

std::string_view to_string(CleanupStep s) noexcept {
	for (const auto& i : kPlan)
		if (i.step == s) return i.name;
	return "unknown";
}

} // namespace rathena::persistence
