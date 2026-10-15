#include "rathena/web/repositories.hpp"

namespace rathena::web {

void InMemoryWebStore::put_token(const WebAuthTokenRecord& rec) {
    std::lock_guard lock(mu_);
    tokens_[rec.account_id] = rec;
}
void InMemoryWebStore::put_character(const CharacterRecord& rec) {
    std::lock_guard lock(mu_);
    chars_[rec.char_id] = rec;
}
void InMemoryWebStore::put_guild(const GuildRecord& rec) {
    std::lock_guard lock(mu_);
    guilds_[rec.guild_id] = rec;
}
void InMemoryWebStore::put_party(const PartyRecord& rec) {
    std::lock_guard lock(mu_);
    parties_.push_back(rec);
}

std::size_t InMemoryWebStore::user_config_count() const {
    std::lock_guard lock(mu_);
    return user_configs_.size();
}
std::size_t InMemoryWebStore::char_config_count() const {
    std::lock_guard lock(mu_);
    return char_configs_.size();
}
std::size_t InMemoryWebStore::merchant_config_count() const {
    std::lock_guard lock(mu_);
    return merchant_configs_.size();
}
std::size_t InMemoryWebStore::booking_count() const {
    std::lock_guard lock(mu_);
    return bookings_.size();
}

std::optional<WebAuthTokenRecord> InMemoryWebStore::find_token(AccountId account_id) const {
    std::lock_guard lock(mu_);
    auto it = tokens_.find(account_id);
    if (it == tokens_.end()) return std::nullopt;
    return it->second;
}
std::optional<CharacterRecord> InMemoryWebStore::find_character(CharId char_id) const {
    std::lock_guard lock(mu_);
    auto it = chars_.find(char_id);
    if (it == chars_.end()) return std::nullopt;
    return it->second;
}
std::optional<GuildRecord> InMemoryWebStore::find_guild(GuildId guild_id) const {
    std::lock_guard lock(mu_);
    auto it = guilds_.find(guild_id);
    if (it == guilds_.end()) return std::nullopt;
    return it->second;
}
std::optional<PartyRecord> InMemoryWebStore::find_party_led_by(AccountId account_id, CharId char_id) const {
    std::lock_guard lock(mu_);
    for (const auto& p : parties_) {
        if (p.leader_account_id == account_id && p.leader_char_id == char_id) return p;
    }
    return std::nullopt;
}

std::optional<UserConfig> InMemoryWebStore::find_user_config(AccountId aid, const std::string& world) const {
    std::lock_guard lock(mu_);
    auto it = user_configs_.find({aid, world});
    if (it == user_configs_.end()) return std::nullopt;
    return it->second;
}
void InMemoryWebStore::save_user_config(const UserConfig& cfg) {
    std::lock_guard lock(mu_);
    user_configs_[{cfg.account_id, cfg.world_name}] = cfg;
}

std::optional<CharConfig> InMemoryWebStore::find_char_config(AccountId aid, CharId cid, const std::string& world) const {
    std::lock_guard lock(mu_);
    auto it = char_configs_.find({aid, cid, world});
    if (it == char_configs_.end()) return std::nullopt;
    return it->second;
}
void InMemoryWebStore::save_char_config(const CharConfig& cfg) {
    std::lock_guard lock(mu_);
    char_configs_[{cfg.account_id, cfg.char_id, cfg.world_name}] = cfg;
}

std::optional<MerchantConfig> InMemoryWebStore::find_merchant_config(AccountId aid, CharId cid,
                                                                     const std::string& world,
                                                                     std::uint8_t store_type) const {
    std::lock_guard lock(mu_);
    auto it = merchant_configs_.find({world, aid, cid, store_type});
    if (it == merchant_configs_.end()) return std::nullopt;
    return it->second;
}
void InMemoryWebStore::save_merchant_config(const MerchantConfig& cfg) {
    std::lock_guard lock(mu_);
    merchant_configs_[{cfg.world_name, cfg.account_id, cfg.char_id, cfg.store_type}] = cfg;
}

std::optional<GuildEmblem> InMemoryWebStore::find_emblem(GuildId gid, const std::string& world) const {
    std::lock_guard lock(mu_);
    auto it = emblems_.find({gid, world});
    if (it == emblems_.end()) return std::nullopt;
    return it->second;
}
void InMemoryWebStore::save_emblem(const GuildEmblem& emblem) {
    std::lock_guard lock(mu_);
    emblems_[{emblem.guild_id, emblem.world_name}] = emblem;
}

void InMemoryWebStore::save_booking(const PartyBooking& booking) {
    std::lock_guard lock(mu_);
    bookings_[{booking.world_name, booking.account_id, booking.char_id}] = booking;
}
std::size_t InMemoryWebStore::remove_bookings(const std::string& world, AccountId aid) {
    std::lock_guard lock(mu_);
    return std::erase_if(bookings_, [&](const auto& kv) {
        return std::get<0>(kv.first) == world && std::get<1>(kv.first) == aid;
    });
}
std::vector<PartyBooking> InMemoryWebStore::find_bookings(const std::string& world, AccountId aid) const {
    std::lock_guard lock(mu_);
    std::vector<PartyBooking> out;
    for (const auto& [key, b] : bookings_) {
        if (std::get<0>(key) == world && std::get<1>(key) == aid) out.push_back(b);
    }
    return out;
}
std::vector<PartyBooking> InMemoryWebStore::list_bookings(const std::string& world) const {
    std::lock_guard lock(mu_);
    std::vector<PartyBooking> out;
    for (const auto& [key, b] : bookings_) {
        if (std::get<0>(key) == world) out.push_back(b);
    }
    return out;
}

}  // namespace rathena::web
