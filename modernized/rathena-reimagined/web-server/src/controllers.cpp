#include "rathena/web/controllers.hpp"

#include <limits>
#include <string>

#include "rathena/web/auth_rules.hpp"
#include "rathena/web/config_merge.hpp"
#include "rathena/web/emblem_rules.hpp"
#include "rathena/web/party_booking_rules.hpp"

namespace rathena::web {
namespace {

/// Optional JSON object field ("data"). Absent -> {}. Unparseable or not an
/// object -> nullopt (legacy threw from nlohmann::json::parse).
std::optional<nlohmann::json> json_object_field(const HttpRequest& req, const std::string& name) {
    const std::string* raw = req.field(name);
    if (raw == nullptr) return nlohmann::json::object();
    auto parsed = nlohmann::json::parse(*raw, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    return parsed;
}

std::optional<std::uint32_t> u32_field(const HttpRequest& req, const std::string& name) {
    const std::string* raw = req.field(name);
    if (raw == nullptr) return std::nullopt;
    return parse_u32(*raw);
}

std::optional<std::uint16_t> u16_field(const HttpRequest& req, const std::string& name) {
    auto v = u32_field(req, name);
    if (!v || *v > std::numeric_limits<std::uint16_t>::max()) return std::nullopt;
    return static_cast<std::uint16_t>(*v);
}

/// Optional 0/1 flag; absent -> false; malformed -> nullopt.
std::optional<bool> flag_field(const HttpRequest& req, const std::string& name, bool required) {
    if (!req.has(name)) {
        if (required) return std::nullopt;
        return false;
    }
    auto v = u32_field(req, name);
    if (!v) return std::nullopt;
    return *v != 0;
}

nlohmann::json with_type(nlohmann::json data) {
    if (!data.is_object()) data = nlohmann::json::object();
    data["Type"] = 1;
    return data;
}

nlohmann::ordered_json type_ok() {
    nlohmann::ordered_json j;
    j["Type"] = 1;
    return j;
}

}  // namespace

WebApi::WebApi(WebServices services) : s_(std::move(services)) {}

std::optional<AccountId> WebApi::authenticate(const HttpRequest& req, bool require_guild_master) {
    auto reject = [&](AuthResult why, std::optional<AccountId> aid) -> std::optional<AccountId> {
        // BR-WEB-017: reason + AID only; the AuthToken value is never formatted.
        s_.log.log(LogLevel::Warning, "web auth rejected: path=" + req.path +
                                          " aid=" + (aid ? std::to_string(*aid) : std::string("?")) +
                                          " reason=" + std::string(to_string(why)));
        return std::nullopt;
    };

    const std::string* token = req.field("AuthToken");
    if (token == nullptr || !req.has("AID")) return reject(AuthResult::MissingCredentials, std::nullopt);
    auto aid = u32_field(req, "AID");
    if (!aid) return reject(AuthResult::MalformedCredentials, std::nullopt);

    const AuthResult tr = check_web_token(s_.tokens.find_token(*aid), *aid, *token, s_.clock.now(),
                                          s_.config.token_logout_grace);
    if (tr != AuthResult::Ok) return reject(tr, aid);

    if (require_guild_master) {
        if (!req.has("GDID")) return reject(AuthResult::MissingCredentials, aid);
        auto gdid = u32_field(req, "GDID");
        if (!gdid) return reject(AuthResult::MalformedCredentials, aid);
        auto guild = s_.guilds.find_guild(*gdid);
        auto master = guild ? s_.characters.find_character(guild->master_char_id) : std::nullopt;
        if (!is_guild_master(*aid, guild, master)) return reject(AuthResult::NotGuildMaster, aid);
    }
    return aid;
}

std::optional<std::string> WebApi::world_name(const HttpRequest& req) const {
    const std::string* w = req.field("WorldName");
    if (w == nullptr || validate_world_name(*w) != BookingVerdict::Ok) return std::nullopt;
    return *w;
}

bool WebApi::char_access_ok(AccountId aid, CharId cid) {
    if (!s_.config.verify_char_ownership) return true;
    if (owns_character(aid, s_.characters.find_character(cid))) return true;
    s_.log.log(LogLevel::Warning, "web char access rejected: aid=" + std::to_string(aid) +
                                      " gid=" + std::to_string(cid) + " reason=not_owner");
    return false;
}

// ------------------------------------------------------------------ userconfig

HttpResponse WebApi::userconfig_load(const HttpRequest& req) {
    auto aid = authenticate(req, false);  // BR-WEB-010 inverted
    if (!aid) return error_response();
    auto world = world_name(req);
    if (!world) return error_response();
    auto row = s_.user_configs.find_user_config(*aid, *world);
    if (!row) return json_response(type_ok());  // no default row inserted
    return json_response(with_type(row->data));
}

HttpResponse WebApi::userconfig_save(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    auto patch = json_object_field(req, "data");
    if (!world || !patch) return error_response();
    auto row = s_.user_configs.find_user_config(*aid, *world);
    UserConfig cfg{*aid, *world, merge_user_config(row ? row->data : nlohmann::json::object(), *patch)};
    s_.user_configs.save_user_config(cfg);
    return json_response(cfg.data);
}

// ------------------------------------------------------------------ charconfig

HttpResponse WebApi::charconfig_load(const HttpRequest& req) {
    auto aid = authenticate(req, false);  // BR-WEB-010 inverted
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gid = u32_field(req, "GID");
    if (!world || !gid || !char_access_ok(*aid, *gid)) return error_response();
    auto row = s_.char_configs.find_char_config(*aid, *gid, *world);
    if (!row) return json_response(type_ok());
    return json_response(with_type(row->data));
}

HttpResponse WebApi::charconfig_save(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gid = u32_field(req, "GID");
    auto patch = json_object_field(req, "data");
    if (!world || !gid || !patch || !char_access_ok(*aid, *gid)) return error_response();
    auto row = s_.char_configs.find_char_config(*aid, *gid, *world);
    CharConfig cfg{*aid, *gid, *world, merge_char_config(row ? row->data : nlohmann::json::object(), *patch)};
    s_.char_configs.save_char_config(cfg);
    return json_response(cfg.data);
}

// --------------------------------------------------------------- MerchantStore

HttpResponse WebApi::merchantstore_load(const HttpRequest& req) {
    auto aid = authenticate(req, false);  // BR-WEB-010 inverted
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gid = u32_field(req, "GID");
    auto type = u32_field(req, "Type");
    if (!world || !gid || !type || *type > 255 || !char_access_ok(*aid, *gid)) return error_response();
    auto row = s_.merchant_configs.find_merchant_config(*aid, *gid, *world, static_cast<std::uint8_t>(*type));
    if (!row) return json_response(type_ok());
    return json_response(with_type(row->data));
}

HttpResponse WebApi::merchantstore_save(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gid = u32_field(req, "GID");
    auto type = u32_field(req, "Type");
    auto data = json_object_field(req, "data");
    if (!world || !gid || !type || *type > 255 || !data || !char_access_ok(*aid, *gid)) {
        return error_response();
    }
    MerchantConfig cfg{*aid, *gid, *world, static_cast<std::uint8_t>(*type), *data};
    s_.merchant_configs.save_merchant_config(cfg);  // BR-WEB-012 upsert
    return json_response(cfg.data);
}

// --------------------------------------------------------------------- emblem

HttpResponse WebApi::emblem_download(const HttpRequest& req) {
    auto aid = authenticate(req, false);  // BR-WEB-008: any authenticated user
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gdid = u32_field(req, "GDID");
    if (!world || !gdid) return error_response();
    auto emblem = s_.emblems.find_emblem(*gdid, *world);
    if (!emblem || emblem->data.size() > kMaxEmblemSize) return error_response(kHttpNotFound);
    HttpResponse r;
    r.content_type = std::string(emblem_content_type(emblem->type));
    r.body = emblem->data;
    return r;
}

HttpResponse WebApi::emblem_upload(const HttpRequest& req) {
    auto aid = authenticate(req, true);  // BR-WEB-004: guild master only
    if (!aid) return error_response();
    auto world = world_name(req);
    auto gdid = u32_field(req, "GDID");
    const std::string* img = req.field("Img");
    const std::string* img_type = req.field("ImgType");
    if (!world || !gdid || img == nullptr || img_type == nullptr) return error_response();
    auto type = parse_emblem_type(*img_type);
    if (!type) return error_response();

    const EmblemPolicy policy{s_.config.allow_gifs, s_.config.emblem_transparency_limit};
    if (auto verdict = validate_emblem(*type, *img, policy); verdict != EmblemVerdict::Ok) {
        s_.log.log(LogLevel::Debug, "emblem rejected: gdid=" + std::to_string(*gdid) +
                                        " verdict=" + std::to_string(static_cast<int>(verdict)));
        return error_response();
    }
    auto existing = s_.emblems.find_emblem(*gdid, *world);
    GuildEmblem e{*gdid, *world,
                  next_emblem_version(existing ? std::optional<std::uint32_t>(existing->version) : std::nullopt),
                  *type, *img};
    s_.emblems.save_emblem(e);
    nlohmann::ordered_json j;
    j["Type"] = 1;
    j["version"] = e.version;
    return json_response(j);
}

// ---------------------------------------------------------------------- party

HttpResponse WebApi::party_add(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto gid = u32_field(req, "GID");
    if (!gid) return error_response();
    if (!is_party_leader(*aid, *gid, s_.parties.find_party_led_by(*aid, *gid))) {
        s_.log.log(LogLevel::Debug, "party booking rejected: aid=" + std::to_string(*aid) + " reason=" +
                                        std::string(to_string(AuthResult::NotPartyLeader)));
        return error_response();
    }
    const std::string* world = req.field("WorldName");
    const std::string* name = req.field("CharName");
    auto purpose = u16_field(req, "Type");
    auto assist = flag_field(req, "Assist", true);
    auto dealer = flag_field(req, "Dealer", true);
    auto healer = flag_field(req, "Healer", true);
    auto tanker = flag_field(req, "Tanker", true);
    auto min_lv = u16_field(req, "MinLV");
    auto max_lv = u16_field(req, "MaxLV");
    const std::string* memo = req.field("Memo");
    if (!world || !name || !purpose || !assist || !dealer || !healer || !tanker || !min_lv || !max_lv) {
        return error_response();
    }
    PartyBooking b;
    b.world_name = *world;
    b.account_id = *aid;
    b.char_id = *gid;
    b.char_name = *name;
    b.purpose = *purpose;
    b.assist = *assist;
    b.damage_dealer = *dealer;
    b.healer = *healer;
    b.tanker = *tanker;
    b.minimum_level = *min_lv;
    b.maximum_level = *max_lv;
    b.comment = memo ? *memo : std::string{};
    b.created = s_.clock.now();
    if (validate_booking(b) != BookingVerdict::Ok) return error_response();
    s_.bookings.save_booking(b);
    return json_response(type_ok());
}

HttpResponse WebApi::party_del(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    if (!world) return error_response();
    s_.bookings.remove_bookings(*world, *aid);
    return json_response(type_ok());
}

HttpResponse WebApi::party_get(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    if (!world) return error_response();
    auto rows = s_.bookings.find_bookings(*world, *aid);
    auto j = type_ok();
    if (!rows.empty()) {
        sort_newest_first(rows);
        j["data"] = booking_to_json(rows.front());
    }
    return json_response(j);
}

HttpResponse WebApi::party_info(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    auto query_aid = u32_field(req, "QueryAID");
    if (!world || !query_aid) return error_response();
    auto rows = s_.bookings.find_bookings(*world, *query_aid);
    auto j = type_ok();
    if (!rows.empty()) {
        sort_newest_first(rows);
        j["data"] = nlohmann::ordered_json::array({booking_to_json(rows.front())});
    }
    return json_response(j);
}

namespace {
nlohmann::ordered_json booking_page(const std::vector<PartyBooking>& rows) {
    auto j = type_ok();
    j["totalPage"] = rows.size();  // legacy: row count, not a page count
    j["data"] = nlohmann::ordered_json::array();
    for (const auto& b : rows) j["data"].push_back(booking_to_json(b));
    return j;
}
}  // namespace

HttpResponse WebApi::party_list(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    if (!world) return error_response();
    auto rows = s_.bookings.list_bookings(*world);
    sort_newest_first(rows);
    return json_response(booking_page(rows));
}

HttpResponse WebApi::party_search(const HttpRequest& req) {
    auto aid = authenticate(req, false);
    if (!aid) return error_response();
    auto world = world_name(req);
    auto min_lv = u16_field(req, "MinLV");
    auto max_lv = u16_field(req, "MaxLV");
    if (!world || !min_lv || !max_lv) return error_response();
    BookingSearch s;
    s.minimum_level = *min_lv;
    s.maximum_level = *max_lv;
    if (req.has("Type")) {
        auto p = u16_field(req, "Type");
        if (!p) return error_response();
        s.purpose = *p;
    }
    auto assist = flag_field(req, "Assist", false);
    auto dealer = flag_field(req, "Dealer", false);
    auto healer = flag_field(req, "Healer", false);
    auto tanker = flag_field(req, "Tanker", false);
    if (!assist || !dealer || !healer || !tanker) return error_response();
    s.assist = *assist;
    s.damage_dealer = *dealer;
    s.healer = *healer;
    s.tanker = *tanker;
    if (const std::string* memo = req.field("Memo")) s.memo = *memo;
    if (validate_search(s) != BookingVerdict::Ok) return error_response();
    return json_response(booking_page(search_bookings(s_.bookings.list_bookings(*world), s)));
}

}  // namespace rathena::web
