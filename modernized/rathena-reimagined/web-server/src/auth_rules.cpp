#include "rathena/web/auth_rules.hpp"

namespace rathena::web {

std::string_view to_string(AuthResult r) noexcept {
    switch (r) {
        case AuthResult::Ok: return "ok";
        case AuthResult::MissingCredentials: return "missing_credentials";
        case AuthResult::MalformedCredentials: return "malformed_credentials";
        case AuthResult::UnknownToken: return "unknown_token";
        case AuthResult::TokenDisabled: return "token_disabled";
        case AuthResult::TokenExpired: return "token_expired";
        case AuthResult::NotGuildMaster: return "not_guild_master";
        case AuthResult::NotPartyLeader: return "not_party_leader";
    }
    return "unknown";
}

bool constant_time_equals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return diff == 0;
}

AuthResult check_web_token(const std::optional<WebAuthTokenRecord>& record,
                           AccountId account_id,
                           std::string_view presented_token,
                           Timestamp now,
                           std::chrono::seconds grace) noexcept {
    if (account_id == 0) {
        return AuthResult::MalformedCredentials;
    }
    if (presented_token.size() != kWebTokenLength) {
        return AuthResult::MalformedCredentials;
    }
    if (!record || record->account_id != account_id || record->token.empty()) {
        return AuthResult::UnknownToken;
    }
    if (!constant_time_equals(record->token, presented_token)) {
        return AuthResult::UnknownToken;
    }
    if (!record->enabled) {
        return AuthResult::TokenDisabled;
    }
    if (record->logged_out_at && now - *record->logged_out_at >= grace) {
        return AuthResult::TokenExpired;
    }
    return AuthResult::Ok;
}

bool is_guild_master(AccountId account_id,
                     const std::optional<GuildRecord>& guild,
                     const std::optional<CharacterRecord>& master_character) noexcept {
    if (!guild || !master_character || account_id == 0) {
        return false;
    }
    return master_character->char_id == guild->master_char_id &&
           master_character->account_id == account_id;
}

bool is_party_leader(AccountId account_id, CharId char_id,
                     const std::optional<PartyRecord>& party) noexcept {
    return party && account_id != 0 && party->leader_account_id == account_id &&
           party->leader_char_id == char_id;
}

bool owns_character(AccountId account_id,
                    const std::optional<CharacterRecord>& character) noexcept {
    return character && account_id != 0 && character->account_id == account_id;
}

}  // namespace rathena::web
