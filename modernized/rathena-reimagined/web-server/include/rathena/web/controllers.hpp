// rathena-web-server: route handlers for spec §3.4 (all POST multipart/form-data).
//
// HITL #1 decision 2 (BR-WEB-010 inverted): every route, including the three
// */load routes, requires the WebToken (AuthToken + AID form fields). A missing
// or invalid token -> 400 "Error". A load for an unknown account returns
// {"Type":1} and inserts nothing.
//
// The spec's OpenAPI fragment still shows `security: []` on the */load routes
// ("LEGACY UNAUTHENTICATED"); that describes the legacy system and is superseded
// by the HITL #1 decision recorded in spec §1.1.
#pragma once

#include <optional>

#include "rathena/web/clock.hpp"
#include "rathena/web/config.hpp"
#include "rathena/web/http.hpp"
#include "rathena/web/log.hpp"
#include "rathena/web/repositories.hpp"

namespace rathena::web {

struct WebServices {
    const WebTokenReader& tokens;
    const CharacterReader& characters;
    const GuildReader& guilds;
    const PartyReader& parties;
    UserConfigRepository& user_configs;
    CharConfigRepository& char_configs;
    MerchantConfigRepository& merchant_configs;
    EmblemRepository& emblems;
    PartyBookingRepository& bookings;
    Logger& log;
    const Clock& clock;
    WebConfig config;
};

class WebApi {
public:
    explicit WebApi(WebServices services);

    // /userconfig/*  (BR-WEB-009, BR-WEB-010)
    HttpResponse userconfig_load(const HttpRequest& req);
    HttpResponse userconfig_save(const HttpRequest& req);
    // /charconfig/*  (BR-WEB-010, BR-WEB-011)
    HttpResponse charconfig_load(const HttpRequest& req);
    HttpResponse charconfig_save(const HttpRequest& req);
    // /MerchantStore/*  (BR-WEB-010, BR-WEB-012)
    HttpResponse merchantstore_load(const HttpRequest& req);
    HttpResponse merchantstore_save(const HttpRequest& req);
    // /emblem/*  (BR-WEB-004..008)
    HttpResponse emblem_download(const HttpRequest& req);
    HttpResponse emblem_upload(const HttpRequest& req);
    // /party/*  (BR-WEB-013..015)
    HttpResponse party_add(const HttpRequest& req);
    HttpResponse party_del(const HttpRequest& req);
    HttpResponse party_get(const HttpRequest& req);
    HttpResponse party_info(const HttpRequest& req);
    HttpResponse party_list(const HttpRequest& req);
    HttpResponse party_search(const HttpRequest& req);

private:
    /// BR-WEB-001/003 (+002 when require_guild_master). Logs the failure reason
    /// and AID only, never the token (BR-WEB-017). Returns the authenticated AID.
    std::optional<AccountId> authenticate(const HttpRequest& req, bool require_guild_master);
    /// World name present and within kWorldNameLength.
    std::optional<std::string> world_name(const HttpRequest& req) const;
    /// BR-WEB-011 ownership gate (config.verify_char_ownership).
    bool char_access_ok(AccountId aid, CharId cid);

    WebServices s_;
};

}  // namespace rathena::web
