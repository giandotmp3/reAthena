// rathena-web-server: pure authorization rules (BR-WEB-001, 002, 003, 013 leader check).
//
// No IO: callers resolve records through read-only repositories and pass them in.
#pragma once

#include <chrono>
#include <optional>
#include <string_view>

#include "rathena/web/domain.hpp"
#include "rathena/web/types.hpp"

namespace rathena::web {

enum class AuthResult {
    Ok,
    MissingCredentials,  ///< AuthToken or AID field absent
    MalformedCredentials,///< AID not a positive integer / token wrong shape
    UnknownToken,        ///< no token row for AID, or token mismatch
    TokenDisabled,       ///< web_auth_token_enabled = 0
    TokenExpired,        ///< logged out longer than the grace window (BR-WEB-003)
    NotGuildMaster,      ///< BR-WEB-002 / BR-WEB-004
    NotPartyLeader,      ///< BR-WEB-013
};

/// Stable, token-free reason string for logs (BR-WEB-017).
std::string_view to_string(AuthResult r) noexcept;

/// Constant-time equality for secrets (length difference still returns early;
/// token length is public: always kWebTokenLength).
bool constant_time_equals(std::string_view a, std::string_view b) noexcept;

/// BR-WEB-001 / BR-WEB-003: the presented AuthToken is accepted iff a token row
/// exists for `account_id`, the token matches, it is enabled, and the account has
/// not been logged out for `grace` or longer. The expiry check is defense in depth
/// for a login-server that failed to flip `web_auth_token_enabled` after logout.
AuthResult check_web_token(const std::optional<WebAuthTokenRecord>& record,
                           AccountId account_id,
                           std::string_view presented_token,
                           Timestamp now,
                           std::chrono::seconds grace = kTokenLogoutGrace) noexcept;

/// BR-WEB-002: `account_id` is the master of the guild iff guild.char_id belongs to
/// one of the account's characters. `master_character` is the character row for
/// guild.char_id (nullopt when the guild or the character is unknown).
bool is_guild_master(AccountId account_id,
                     const std::optional<GuildRecord>& guild,
                     const std::optional<CharacterRecord>& master_character) noexcept;

/// BR-WEB-013: the (AID, GID) pair is the leader of `party`.
bool is_party_leader(AccountId account_id, CharId char_id,
                     const std::optional<PartyRecord>& party) noexcept;

/// BR-WEB-011 (SME open question): the character belongs to the account.
bool owns_character(AccountId account_id,
                    const std::optional<CharacterRecord>& character) noexcept;

}  // namespace rathena::web
