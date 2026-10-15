// rathena-web-server: repository interfaces + in-memory implementation.
//
// Architecture §3: web reads Account (token), Character, Guild, Party through
// read-only repositories and writes the *_configs, guild_emblems and
// party_bookings tables. MySQL implementations come from the persistence unit;
// InMemoryWebStore is the test fake (architecture m6: interfaces only where a
// fake exists).
#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "rathena/web/domain.hpp"

namespace rathena::web {

// ------------------------------------------------------------- read-only views

class WebTokenReader {
public:
    virtual ~WebTokenReader() = default;
    virtual std::optional<WebAuthTokenRecord> find_token(AccountId account_id) const = 0;
};

class CharacterReader {
public:
    virtual ~CharacterReader() = default;
    virtual std::optional<CharacterRecord> find_character(CharId char_id) const = 0;
};

class GuildReader {
public:
    virtual ~GuildReader() = default;
    virtual std::optional<GuildRecord> find_guild(GuildId guild_id) const = 0;
};

class PartyReader {
public:
    virtual ~PartyReader() = default;
    /// The party whose leader is exactly (account_id, char_id), if any.
    virtual std::optional<PartyRecord> find_party_led_by(AccountId account_id, CharId char_id) const = 0;
};

// ------------------------------------------------------------- owned tables

class UserConfigRepository {
public:
    virtual ~UserConfigRepository() = default;
    virtual std::optional<UserConfig> find_user_config(AccountId aid, const std::string& world) const = 0;
    virtual void save_user_config(const UserConfig& cfg) = 0;
};

class CharConfigRepository {
public:
    virtual ~CharConfigRepository() = default;
    virtual std::optional<CharConfig> find_char_config(AccountId aid, CharId cid, const std::string& world) const = 0;
    virtual void save_char_config(const CharConfig& cfg) = 0;
};

class MerchantConfigRepository {
public:
    virtual ~MerchantConfigRepository() = default;
    virtual std::optional<MerchantConfig> find_merchant_config(AccountId aid, CharId cid, const std::string& world,
                                               std::uint8_t store_type) const = 0;
    /// BR-WEB-012: upsert per (AID, GID, World, Type).
    virtual void save_merchant_config(const MerchantConfig& cfg) = 0;
};

class EmblemRepository {
public:
    virtual ~EmblemRepository() = default;
    virtual std::optional<GuildEmblem> find_emblem(GuildId gid, const std::string& world) const = 0;
    virtual void save_emblem(const GuildEmblem& emblem) = 0;
};

class PartyBookingRepository {
public:
    virtual ~PartyBookingRepository() = default;
    /// BR-WEB-013: one booking per (world, AID, GID); replaces an existing row.
    virtual void save_booking(const PartyBooking& booking) = 0;
    /// BR-WEB-015: delete every booking of `aid` in `world`.
    virtual std::size_t remove_bookings(const std::string& world, AccountId aid) = 0;
    virtual std::vector<PartyBooking> find_bookings(const std::string& world, AccountId aid) const = 0;
    virtual std::vector<PartyBooking> list_bookings(const std::string& world) const = 0;
};

// ------------------------------------------------------------- in-memory fake

class InMemoryWebStore final : public WebTokenReader,
                               public CharacterReader,
                               public GuildReader,
                               public PartyReader,
                               public UserConfigRepository,
                               public CharConfigRepository,
                               public MerchantConfigRepository,
                               public EmblemRepository,
                               public PartyBookingRepository {
public:
    // Seeding (stands in for login/char-owned rows).
    void put_token(const WebAuthTokenRecord& rec);
    void put_character(const CharacterRecord& rec);
    void put_guild(const GuildRecord& rec);
    void put_party(const PartyRecord& rec);

    // Introspection for tests.
    std::size_t user_config_count() const;
    std::size_t char_config_count() const;
    std::size_t merchant_config_count() const;
    std::size_t booking_count() const;

    std::optional<WebAuthTokenRecord> find_token(AccountId account_id) const override;
    std::optional<CharacterRecord> find_character(CharId char_id) const override;
    std::optional<GuildRecord> find_guild(GuildId guild_id) const override;
    std::optional<PartyRecord> find_party_led_by(AccountId account_id, CharId char_id) const override;

    std::optional<UserConfig> find_user_config(AccountId aid, const std::string& world) const override;
    void save_user_config(const UserConfig& cfg) override;

    std::optional<CharConfig> find_char_config(AccountId aid, CharId cid, const std::string& world) const override;
    void save_char_config(const CharConfig& cfg) override;

    std::optional<MerchantConfig> find_merchant_config(AccountId aid, CharId cid, const std::string& world,
                                       std::uint8_t store_type) const override;
    void save_merchant_config(const MerchantConfig& cfg) override;

    std::optional<GuildEmblem> find_emblem(GuildId gid, const std::string& world) const override;
    void save_emblem(const GuildEmblem& emblem) override;

    void save_booking(const PartyBooking& booking) override;
    std::size_t remove_bookings(const std::string& world, AccountId aid) override;
    std::vector<PartyBooking> find_bookings(const std::string& world, AccountId aid) const override;
    std::vector<PartyBooking> list_bookings(const std::string& world) const override;

private:
    mutable std::mutex mu_;
    std::map<AccountId, WebAuthTokenRecord> tokens_;
    std::map<CharId, CharacterRecord> chars_;
    std::map<GuildId, GuildRecord> guilds_;
    std::vector<PartyRecord> parties_;
    std::map<std::tuple<AccountId, std::string>, UserConfig> user_configs_;
    std::map<std::tuple<AccountId, CharId, std::string>, CharConfig> char_configs_;
    std::map<std::tuple<std::string, AccountId, CharId, std::uint8_t>, MerchantConfig> merchant_configs_;
    std::map<std::tuple<GuildId, std::string>, GuildEmblem> emblems_;
    std::map<std::tuple<std::string, AccountId, CharId>, PartyBooking> bookings_;
};

}  // namespace rathena::web
