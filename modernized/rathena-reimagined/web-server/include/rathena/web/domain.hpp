// rathena-web-server: domain model (AI_NATIVE_SPEC.md §2.1: web reads login/char/
// guild/party, writes *_configs, guild_emblems and party_bookings).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rathena/web/types.hpp"

namespace rathena::web {

// ---------------------------------------------------------------- read models

/// Projection of `login.web_auth_token*` (owned by login-server, read-only here).
struct WebAuthTokenRecord {
    AccountId account_id{};
    std::string token;               ///< 16 chars; never logged (BR-WEB-017)
    bool enabled{false};             ///< login.web_auth_token_enabled
    std::optional<Timestamp> logged_out_at;  ///< set by login on logout (BR-WEB-003)
};

/// Projection of `char` (owned by char-server).
struct CharacterRecord {
    CharId char_id{};
    AccountId account_id{};
};

/// Projection of `guild` (owned by char-server). `master_char_id` = guild.char_id.
struct GuildRecord {
    GuildId guild_id{};
    CharId master_char_id{};
};

/// Projection of `party` (owned by char-server).
struct PartyRecord {
    PartyId party_id{};
    AccountId leader_account_id{};
    CharId leader_char_id{};
};

// ---------------------------------------------------------- written aggregates

/// `user_configs` row; key (account_id, world_name).
struct UserConfig {
    AccountId account_id{};
    std::string world_name;
    nlohmann::json data = nlohmann::json::object();
};

/// `char_configs` row; key (account_id, char_id, world_name).
struct CharConfig {
    AccountId account_id{};
    CharId char_id{};
    std::string world_name;
    nlohmann::json data = nlohmann::json::object();
};

/// `merchant_configs` row; key (world_name, account_id, char_id, store_type) (BR-WEB-012).
struct MerchantConfig {
    AccountId account_id{};
    CharId char_id{};
    std::string world_name;
    std::uint8_t store_type{};
    nlohmann::json data = nlohmann::json::object();
};

enum class EmblemType { Bmp, Gif };

/// `guild_emblems` row; key (guild_id, world_name).
struct GuildEmblem {
    GuildId guild_id{};
    std::string world_name;
    std::uint32_t version{kEmblemStartVersion};
    EmblemType type{EmblemType::Bmp};
    std::string data;  ///< raw image bytes
};

/// Legacy e_booking_purpose.
enum class BookingPurpose : std::uint16_t {
    All = 0,
    Quest,
    Field,
    Dungeon,
    MemorialDungeon,
    Paradise,
    Other,
    Max
};

/// `party_bookings` row; key (world_name, account_id, char_id) (BR-WEB-013).
struct PartyBooking {
    std::string world_name;
    AccountId account_id{};
    CharId char_id{};
    std::string char_name;
    std::uint16_t purpose{0};
    bool assist{false};
    bool damage_dealer{false};
    bool healer{false};
    bool tanker{false};
    std::uint16_t minimum_level{0};
    std::uint16_t maximum_level{0};
    std::string comment;
    Timestamp created{};
};

}  // namespace rathena::web
