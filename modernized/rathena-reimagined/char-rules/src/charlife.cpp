// rules/charlife implementation. Legacy references are to src/char/ @ d4b8e7b (read-only).
#include "rathena/char_rules/charlife.hpp"

#include <algorithm>
#include <cctype>

namespace rathena::char_rules {
namespace {

bool is_trim_char(char c) { return kTrimChars.find(c) != std::string_view::npos; }

bool iequals(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}
	return true;
}

constexpr int kPacketverClientSex = 20151001;
constexpr int kPacketverNewStats = 20120307;

} // namespace

// ---- BR-CHAR-001 ---------------------------------------------------------------------------------
std::string normalize_char_name(std::string_view raw) {
	raw = raw.substr(0, std::min(raw.size(), kNameLength - 1));
	if (const auto nul = raw.find('\0'); nul != std::string_view::npos) {
		raw = raw.substr(0, nul);
	}
	std::string out;
	out.reserve(raw.size());
	std::size_t i = 0;
	while (i < raw.size() && is_trim_char(raw[i])) {
		++i;
	}
	bool put_space = false;
	while (i < raw.size()) {
		if (put_space) {
			out.push_back(' ');
		}
		while (i < raw.size() && !is_trim_char(raw[i])) {
			out.push_back(raw[i++]);
		}
		while (i < raw.size() && is_trim_char(raw[i])) {
			++i;
		}
		put_space = true;
	}
	return out;
}

NameResult check_char_name(std::string_view name, const NameConfig& cfg,
                           const std::function<bool(std::string_view)>& name_exists) {
	if (name.empty() || name.size() < cfg.min_length) {
		return NameResult::Invalid;
	}
	for (const char c : name) {
		if (std::iscntrl(static_cast<unsigned char>(c))) {
			return NameResult::Invalid;
		}
	}
	if (iequals(name, cfg.wisp_server_name)) {
		return NameResult::Taken;
	}
	if (name.front() == '#') {
		return NameResult::Invalid;
	}
	switch (cfg.policy) {
	case NameLetterPolicy::AllowOnly:
		for (const char c : name) {
			if (cfg.letters.find(c) == std::string::npos) {
				return NameResult::Invalid;
			}
		}
		break;
	case NameLetterPolicy::Forbid:
		for (const char c : name) {
			if (cfg.letters.find(c) != std::string::npos) {
				return NameResult::Invalid;
			}
		}
		break;
	case NameLetterPolicy::Any:
		break;
	}
	if (name_exists && name_exists(name)) {
		return NameResult::Taken;
	}
	return NameResult::Ok;
}

// ---- BR-CHAR-002..008 ----------------------------------------------------------------------------
CreateResult check_slot(int slot, int char_slots, bool occupied) {
	if (slot < 0 || slot >= char_slots) {
		return CreateResult::InvalidSlot;
	}
	return occupied ? CreateResult::Invalid : CreateResult::Ok;
}

bool may_create_another(int existing_count, int char_slots, bool vip_build) {
	return existing_count < (vip_build ? kMaxChars : char_slots);
}

std::optional<Sex> resolve_new_char_sex(int packetver, std::optional<Sex> client_sex, Sex account_sex) {
	const Sex s = packetver >= kPacketverClientSex ? client_sex.value_or(Sex::Server) : account_sex;
	if (s != Sex::Male && s != Sex::Female) {
		return std::nullopt;
	}
	return s;
}

std::optional<int> validate_start_stats(const Stats& s, int packetver, int start_status_points) {
	if (packetver < kPacketverNewStats) {
		const int v[] = {s.str, s.agi, s.vit, s.int_, s.dex, s.luk};
		if (s.sum() != 30) {
			return std::nullopt;
		}
		for (const int x : v) {
			if (x < 1 || x > 9) {
				return std::nullopt;
			}
		}
		if (s.str + s.int_ != 10 || s.agi + s.luk != 10 || s.vit + s.dex != 10) {
			return std::nullopt;
		}
		return 0;
	}
	// New clients: no stat input; every stat is 1 and the points are granted instead.
	if (s != Stats{}) {
		return std::nullopt;
	}
	return start_status_points;
}

bool is_allowed_start_job(Job job, std::uint8_t flag, int packetver) {
	if (packetver < kPacketverClientSex) {
		return job == Job::Novice;
	}
	return (job == Job::Novice && (flag & 1)) || (job == Job::Summoner && (flag & 2));
}

std::uint32_t initial_max_hp(int vit) { return static_cast<std::uint32_t>(40 * (100 + vit) / 100); }
std::uint32_t initial_max_sp(int int_) { return static_cast<std::uint32_t>(11 * (100 + int_) / 100); }

std::vector<StartItem> start_items_for(std::span<const StartItem> configured) {
	std::vector<StartItem> out;
	const auto bound = std::min(configured.size(), kMaxStartItems); // fixed: never k == MAX_STARTITEM
	for (std::size_t k = 0; k < bound && configured[k].nameid != 0; ++k) {
		out.push_back(configured[k]);
	}
	return out;
}

std::uint8_t create_error_to_client_code(CreateResult r) {
	switch (r) {
	case CreateResult::NameTaken: return 0x00;
	case CreateResult::Invalid: return 0xFF;
	case CreateResult::Underaged: return 0x01;
	case CreateResult::InvalidSlot: return 0x03;
	case CreateResult::Ok: break;
	}
	return 0xFF;
}

CreateOutcome make_new_char(const CreateRequest& req, const AccountContext& acc, const CreationConfig& ccfg,
                            const NameConfig& ncfg, Rng& rng) {
	if (!ccfg.char_new) {
		return {CreateResult::Invalid, std::nullopt};
	}
	const std::string name = normalize_char_name(req.name);
	switch (check_char_name(name, ncfg, acc.name_exists)) {
	case NameResult::Taken: return {CreateResult::NameTaken, std::nullopt};
	case NameResult::Invalid: return {CreateResult::Invalid, std::nullopt};
	case NameResult::Ok: break;
	}
	if (req.slot < 0 || req.slot >= acc.char_slots) {
		return {CreateResult::InvalidSlot, std::nullopt};
	}
	const auto sex = resolve_new_char_sex(ccfg.client_packetver, req.client_sex, acc.account_sex);
	if (!sex) {
		return {CreateResult::Invalid, std::nullopt};
	}
	const auto points = validate_start_stats(req.stats, ccfg.client_packetver, ccfg.start_status_points);
	if (!points) {
		return {CreateResult::Invalid, std::nullopt};
	}
	if (!may_create_another(acc.existing_char_count, acc.char_slots, ccfg.vip_build)) {
		return {CreateResult::Invalid, std::nullopt};
	}
	if (check_slot(req.slot, acc.char_slots, acc.slot_occupied && acc.slot_occupied(req.slot)) != CreateResult::Ok) {
		return {CreateResult::Invalid, std::nullopt};
	}
	if (!is_allowed_start_job(req.start_job, ccfg.allowed_job_flag, ccfg.client_packetver)) {
		return {CreateResult::Invalid, std::nullopt};
	}

	const bool doram = req.start_job == Job::Summoner;
	const auto& points_list = doram ? ccfg.start_points_doram : ccfg.start_points;
	const auto& items_list = doram ? ccfg.start_items_doram : ccfg.start_items;
	if (points_list.empty()) {
		return {CreateResult::Invalid, std::nullopt};
	}
	const auto count = std::min(points_list.size(), kMaxStartPoints);
	const auto& point = points_list[rng.below(static_cast<std::uint32_t>(count))];

	NewCharacter nc;
	nc.name = name;
	nc.account_id = acc.account_id;
	nc.slot = req.slot;
	nc.job = req.start_job;
	nc.sex = *sex;
	nc.stats = req.stats;
	nc.status_points = *points;
	nc.zeny = ccfg.start_zeny;
	nc.max_hp = nc.hp = initial_max_hp(req.stats.vit);
	nc.max_sp = nc.sp = initial_max_sp(req.stats.int_);
	nc.hair_style = req.hair_style;
	nc.hair_color = req.hair_color;
	nc.last_point = point;
	nc.save_point = point;
	nc.items = start_items_for(items_list);
	return {CreateResult::Ok, std::move(nc)};
}

// ---- BR-CHAR-009..015 ----------------------------------------------------------------------------
DeleteRequestOutcome request_deletion(const CharacterSummary& c, bool owned, const DeletionConfig& cfg, UnixTime now) {
	if (!owned) {
		return {DeleteRequestResult::DatabaseOrNotFound, 0};
	}
	if (c.delete_date != 0) {
		return {DeleteRequestResult::AlreadyQueued, c.delete_date};
	}
	if ((cfg.restriction & kDelRestrictGuild) && c.guild_id != 0) {
		return {DeleteRequestResult::InGuild, 0};
	}
	if ((cfg.restriction & kDelRestrictParty) && c.party_id != 0) {
		return {DeleteRequestResult::InParty, 0};
	}
	return {DeleteRequestResult::Queued, now + cfg.delay_seconds};
}

bool birthdate_code_matches(std::string_view stored, std::string_view yymmdd) {
	if (stored.empty() && yymmdd.empty()) {
		return true; // legacy: default (unset) birthdate accepts an empty code
	}
	if (yymmdd.size() != 6 || stored.size() != 10) {
		return false;
	}
	// stored "YYYY-MM-DD" -> compare "YY-MM-DD" with the reformatted client code
	std::string code;
	code.append(yymmdd.substr(0, 2)).append("-").append(yymmdd.substr(2, 2)).append("-").append(yymmdd.substr(4, 2));
	return stored.substr(2) == code;
}

bool delete_code_matches(std::string_view code, std::string_view email, std::string_view birthdate,
                         std::uint8_t option, bool code_is_birthdate_form) {
	if ((option & kDelOptionEmail) && !code_is_birthdate_form) {
		if (iequals(code, email) || (iequals(email, "a@a.com") && code.empty())) {
			return true;
		}
	}
	if (option & kDelOptionBirthdate) {
		if (birthdate_code_matches(birthdate, code)) {
			return true;
		}
	}
	return false;
}

DeleteGate check_delete_gate(const CharacterSummary& c, bool owned, const DeletionConfig& cfg, UnixTime now) {
	if (!owned) {
		return DeleteGate::NotFound;
	}
	if ((cfg.level_limit > 0 && c.base_level >= cfg.level_limit) ||
	    (cfg.level_limit < 0 && c.base_level <= -cfg.level_limit)) {
		return DeleteGate::BaseLevel;
	}
	if ((cfg.restriction & kDelRestrictGuild) && c.guild_id != 0) {
		return DeleteGate::Guild;
	}
	if ((cfg.restriction & kDelRestrictParty) && c.party_id != 0) {
		return DeleteGate::Party;
	}
	if (cfg.delay_seconds > 0 && (c.delete_date == 0 || c.delete_date > now)) {
		return DeleteGate::Time;
	}
	return DeleteGate::Ok;
}

std::vector<CleanupStep> deletion_cleanup_plan(const CharacterSummary& c, bool is_guild_master) {
	std::vector<CleanupStep> plan;
	if (c.partner_id != 0) plan.push_back(CleanupStep::Divorce);
	if (c.father_id != 0 || c.mother_id != 0) plan.push_back(CleanupStep::DeAdoptFromParents);
	if (c.party_id != 0) plan.push_back(CleanupStep::LeaveParty);
	plan.push_back(CleanupStep::DeletePets);
	if (c.homun_id != 0) plan.push_back(CleanupStep::DeleteHomunculus);
	if (c.elemental_id != 0) plan.push_back(CleanupStep::DeleteElemental);
	plan.insert(plan.end(), {CleanupStep::DeleteMercenaryOwner, CleanupStep::DeleteFriendsBothWays,
	                         CleanupStep::DeleteHotkeys, CleanupStep::DeleteInventory, CleanupStep::DeleteCart,
	                         CleanupStep::DeleteMemos, CleanupStep::DeleteRegistries, CleanupStep::DeleteSkills,
	                         CleanupStep::DeleteReceivedMail, CleanupStep::AnonymizeSentMail,
	                         CleanupStep::DeleteStatusChanges, CleanupStep::DeleteBonusScripts,
	                         CleanupStep::DeleteQuests, CleanupStep::DeleteAchievements,
	                         CleanupStep::DeleteCharacterRow});
	if (c.guild_id != 0) plan.push_back(is_guild_master ? CleanupStep::BreakGuild : CleanupStep::LeaveGuild);
	return plan;
}

std::optional<UnixTime> cancel_deletion(const CharacterSummary&, bool owned) {
	if (!owned) {
		return std::nullopt;
	}
	return UnixTime{0};
}

// ---- BR-CHAR-015 / 016 ---------------------------------------------------------------------------
SelectResult check_selectable(const CharacterSummary& c, UnixTime now) {
	if (c.delete_date != 0) {
		return SelectResult::QueuedForDeletion;
	}
	if (c.unban_time > now) {
		return SelectResult::Banned;
	}
	return SelectResult::Ok;
}

CharBanOutcome adjust_char_ban(UnixTime current, std::int64_t delta, UnixTime now) {
	if (delta < 0 && current == 0) {
		return {false, 0, false};
	}
	UnixTime t = std::max(current, now) + delta;
	if (t < now) {
		t = 0;
	}
	return {true, t, t > now};
}

// ---- BR-CHAR-017 / 018 ---------------------------------------------------------------------------
RenameOutcome rename_char(const CharacterSummary& c, std::string_view requested, const RenameConfig& rcfg,
                          const NameConfig& ncfg, const std::function<bool(std::string_view)>& name_exists) {
	const std::string name = normalize_char_name(requested);
	if (name.empty()) {
		return {RenameResult::NotReady, c.rename_count, {}};
	}
	if (name == c.name) {
		return {RenameResult::Ok, c.rename_count, name};
	}
	if (c.rename_count <= 0) {
		return {RenameResult::NoRenamesLeft, c.rename_count, {}};
	}
	if (!rcfg.allow_in_party && c.party_id != 0) {
		return {RenameResult::InParty, c.rename_count, {}};
	}
	if (!rcfg.allow_in_guild && c.guild_id != 0) {
		return {RenameResult::InGuild, c.rename_count, {}};
	}
	switch (check_char_name(name, ncfg, name_exists)) {
	case NameResult::Ok: break;
	case NameResult::Taken: return {RenameResult::NameTaken, c.rename_count, {}};
	case NameResult::Invalid: return {RenameResult::InvalidName, c.rename_count, {}};
	}
	return {RenameResult::Ok, c.rename_count - 1, name};
}

SlotMoveOutcome move_char_slot(int from, int to, int char_slots, int moves_left, const SlotMoveConfig& cfg,
                               const std::function<CharId(int)>& occupant_of) {
	const SlotMoveOutcome fail{false, false, moves_left};
	if (from < 0 || from >= kMaxChars) return fail;
	if (!cfg.enabled || (!cfg.unlimited && moves_left <= 0)) return fail;
	if (occupant_of(from) == 0) return fail;
	if (to < 0 || to >= kMaxChars || to >= char_slots) return fail;
	bool swapped = false;
	if (occupant_of(to) != 0) {
		if (!cfg.move_to_used) return fail;
		swapped = true;
	}
	return {true, swapped, cfg.unlimited ? moves_left : moves_left - 1};
}

// ---- BR-CHAR-052 / 053 ---------------------------------------------------------------------------
Job gendered_counterpart(Job job) {
	switch (job) {
	case Job::Bard: return Job::Dancer;
	case Job::Dancer: return Job::Bard;
	case Job::Clown: return Job::Gypsy;
	case Job::Gypsy: return Job::Clown;
	case Job::BabyBard: return Job::BabyDancer;
	case Job::BabyDancer: return Job::BabyBard;
	case Job::Minstrel: return Job::Wanderer;
	case Job::Wanderer: return Job::Minstrel;
	case Job::MinstrelT: return Job::WandererT;
	case Job::WandererT: return Job::MinstrelT;
	case Job::BabyMinstrel: return Job::BabyWanderer;
	case Job::BabyWanderer: return Job::BabyMinstrel;
	case Job::Kagerou: return Job::Oboro;
	case Job::Oboro: return Job::Kagerou;
	case Job::BabyKagerou: return Job::BabyOboro;
	case Job::BabyOboro: return Job::BabyKagerou;
	case Job::Troubadour: return Job::Trouvere;
	case Job::Trouvere: return Job::Troubadour;
	case Job::Shinkiro: return Job::Shiranui;
	case Job::Shiranui: return Job::Shinkiro;
	default: return job;
	}
}

SexChangeOutcome apply_sex_change(Job job, Sex new_sex, GuildId guild_id) {
	SexChangeOutcome out;
	out.job = gendered_counterpart(job);
	out.sex = new_sex;
	out.look = CharacterLook{};
	out.look.body = static_cast<std::int32_t>(out.job);
	out.unequip_all = true;
	out.update_guild_member = guild_id != 0;
	return out;
}

std::vector<FameEntry> build_fame_list(std::vector<FameEntry> candidates, std::size_t list_size) {
	const auto n = std::min(list_size, kMaxFameList);
	std::erase_if(candidates, [](const FameEntry& e) { return e.fame <= 0; });
	std::stable_sort(candidates.begin(), candidates.end(),
	                 [](const FameEntry& a, const FameEntry& b) { return a.fame > b.fame; });
	if (candidates.size() > n) {
		candidates.resize(n);
	}
	return candidates;
}

} // namespace rathena::char_rules
