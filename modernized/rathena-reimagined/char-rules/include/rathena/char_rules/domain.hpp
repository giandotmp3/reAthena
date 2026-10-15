// Char-side domain value types (spec §2.2, §2.3; architecture §2 `domain/`).
//
// Pure values only: no IO, no globals. Constants mirror common/mmo.hpp so delegating legacy
// call sites keep their behavior (BR-INTER-009).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rathena::char_rules {

// ---- Identifiers -----------------------------------------------------------------------------
using AccountId = std::uint32_t;
using CharId = std::uint32_t;
using PartyId = std::uint32_t;
using GuildId = std::uint32_t;
using ClanId = std::uint32_t;
using MailId = std::int64_t;
using ServerId = std::int32_t;
using UnixTime = std::int64_t; ///< seconds since epoch; injected, never read from a clock here

// ---- Limits (common/mmo.hpp, char/char.hpp) --------------------------------------------------
inline constexpr std::uint32_t kStartAccountNum = 2'000'000; ///< BR-INTER-009
inline constexpr std::uint32_t kEndAccountNum = 100'000'000;
inline constexpr std::uint32_t kStartCharNum = 150'000;      ///< BR-INTER-009
inline constexpr std::size_t kNameLength = 24;               ///< 23 chars + NUL (BR-INTER-009)
inline constexpr int kMaxChars = 15;                         ///< VIP build
inline constexpr int kMinChars = 9;                          ///< MAX_CHARS - VIP - billing
inline constexpr std::size_t kMaxStartItems = 32;            ///< MAX_STARTITEM
inline constexpr std::size_t kMaxStartPoints = 5;            ///< MAX_STARTPOINT
inline constexpr std::size_t kPincodeLength = 4;
inline constexpr std::size_t kMaxParty = 12;
inline constexpr int kGuildBaseCapacity = 16;
inline constexpr int kGuildCapacityPerExtension = 6;
inline constexpr int kMaxGuildExtensionLevel = 10;
inline constexpr int kMaxGuild = kGuildBaseCapacity + kMaxGuildExtensionLevel * kGuildCapacityPerExtension; // 76
inline constexpr int kMaxGuildLevel = 50;
inline constexpr std::size_t kMaxGuildExpulsion = 32;
inline constexpr std::size_t kMaxGuildAlliance = 16;
inline constexpr int kMaxGuildStorageNonOfficial = 600;
inline constexpr std::size_t kMaxFameList = 10;
inline constexpr std::size_t kMaxSkillCooldown = 20;
inline constexpr std::size_t kMailMaxInbox = 30;
inline constexpr std::size_t kMailMaxItem = 5; ///< RODEX (PACKETVER >= 20150513)
inline constexpr std::int64_t kZenyMax = 2'147'483'647;

// ---- Enumerations ----------------------------------------------------------------------------
enum class Sex : std::uint8_t { Female = 0, Male = 1, Server = 2 };

/// Subset of e_job used by char rules (values verified against common/mmo.hpp).
enum class Job : std::int32_t {
	Novice = 0,
	Bard = 19,
	Dancer = 20,
	Clown = 4020,
	Gypsy = 4021,
	BabyBard = 4042,
	BabyDancer = 4043,
	Minstrel = 4068,
	Wanderer = 4069,
	MinstrelT = 4075,
	WandererT = 4076,
	BabyMinstrel = 4104,
	BabyWanderer = 4105,
	Kagerou = 4211,
	Oboro = 4212,
	Summoner = 4218,
	BabyKagerou = 4223,
	BabyOboro = 4224,
	Troubadour = 4263,
	Trouvere = 4264,
	Shinkiro = 4304,
	Shiranui = 4305,
};

struct Stats {
	int str = 1, agi = 1, vit = 1, int_ = 1, dex = 1, luk = 1;
	constexpr int sum() const { return str + agi + vit + int_ + dex + luk; }
	friend constexpr bool operator==(const Stats&, const Stats&) = default;
};

/// One item value type with one persistence mapping (spec §2.2, D6). Trimmed to the fields
/// char rules read; persistence/ owns the full column mapping.
struct Item {
	std::uint32_t nameid = 0;
	std::int32_t amount = 0;
	std::uint32_t equip = 0; ///< equip position bitmask
	bool identify = true;
	std::array<std::uint32_t, 4> card{};
	std::uint64_t unique_id = 0;
	friend bool operator==(const Item&, const Item&) = default;
};

struct StartItem {
	std::uint32_t nameid = 0;
	std::uint16_t amount = 0;
	std::uint32_t equip_pos = 0;
	friend bool operator==(const StartItem&, const StartItem&) = default;
};

struct MapPoint {
	std::string map;
	std::int16_t x = 0;
	std::int16_t y = 0;
	friend bool operator==(const MapPoint&, const MapPoint&) = default;
};

// ---- Character aggregate (the fields char rules read) ----------------------------------------
struct CharacterSummary {
	CharId char_id = 0;
	AccountId account_id = 0;
	int slot = 0;
	std::string name;
	Job job = Job::Novice;
	Sex sex = Sex::Male;
	int base_level = 1;
	PartyId party_id = 0; ///< 0 = none (legacy convention until migration M3a)
	GuildId guild_id = 0;
	ClanId clan_id = 0;
	CharId partner_id = 0;
	CharId father_id = 0;
	CharId mother_id = 0;
	CharId child_id = 0;
	std::uint32_t pet_id = 0;
	std::uint32_t homun_id = 0;
	std::uint32_t mercenary_id = 0;
	std::uint32_t elemental_id = 0;
	UnixTime delete_date = 0; ///< 0 = not queued
	UnixTime unban_time = 0;  ///< 0 = not banned
	int rename_count = 0;
	int slot_moves = 0;
	bool online = false;
	UnixTime last_login = 0;
};

/// The visual/look fields a sex change clears (BR-CHAR-052).
struct CharacterLook {
	std::int32_t body = 0;
	std::int32_t weapon = 0;
	std::int32_t shield = 0;
	std::int32_t head_top = 0;
	std::int32_t head_mid = 0;
	std::int32_t head_bottom = 0;
	std::int32_t robe = 0;
	friend bool operator==(const CharacterLook&, const CharacterLook&) = default;
};

// ---- Session ---------------------------------------------------------------------------------
/// Online state (BR-CHAR-027; mirrors BR-LOGIN-033): server >= 0 on a map server,
/// -1 on the char server (selecting), -2 "unknown server" (its map server was lost).
inline constexpr ServerId kServerCharSelect = -1;
inline constexpr ServerId kServerUnknown = -2;

struct OnlineEntry {
	AccountId account_id = 0;
	CharId char_id = 0;   ///< 0 = no character selected yet
	ServerId server = kServerCharSelect;
	int fd = -1;          ///< -1 = no client socket on the char server
	bool pincode_success = false;
};

/// One-time auth ticket handed login -> char -> map (CAP-04, BR-CHAR-031, BR-INTER-008).
struct AuthTicket {
	AccountId account_id = 0;
	CharId char_id = 0;
	std::uint32_t login_id1 = 0;
	std::uint32_t login_id2 = 0;
	Sex sex = Sex::Male;
	UnixTime issued_at = 0;
};

} // namespace rathena::char_rules
