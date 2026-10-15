#include "rathena/web/config.hpp"

#include <cstdlib>
#include <stdexcept>

#include "rathena/web/http.hpp"

namespace rathena::web {
namespace {

bool parse_switch(std::string_view key, const std::string& v) {
    if (v == "yes" || v == "on" || v == "true" || v == "1") return true;
    if (v == "no" || v == "off" || v == "false" || v == "0") return false;
    throw std::invalid_argument(std::string(key) + ": expected yes/no");
}

std::uint32_t parse_number(std::string_view key, const std::string& v, std::uint32_t max) {
    auto n = parse_u32(v);
    if (!n || *n > max) throw std::invalid_argument(std::string(key) + ": out of range");
    return *n;
}

}  // namespace

EnvLookup process_env() {
    return [](std::string_view name) -> std::optional<std::string> {
        const char* v = std::getenv(std::string(name).c_str());
        if (v == nullptr) return std::nullopt;
        return std::string(v);
    };
}

WebConfig load_config(const EnvLookup& env) {
    WebConfig c;
    if (auto v = env("WEB_BIND")) c.bind_ip = *v;
    if (auto v = env("WEB_PORT")) c.port = static_cast<std::uint16_t>(parse_number("WEB_PORT", *v, 65535));
    if (auto v = env("WEB_ALLOW_GIFS")) c.allow_gifs = parse_switch("WEB_ALLOW_GIFS", *v);
    if (auto v = env("WEB_ALLOWED_ORIGIN_CORS")) c.allowed_origin_cors = *v;
    if (auto v = env("WEB_EMBLEM_TRANSPARENCY_LIMIT")) {
        c.emblem_transparency_limit = static_cast<int>(parse_number("WEB_EMBLEM_TRANSPARENCY_LIMIT", *v, 100));
    }
    if (auto v = env("WEB_VERIFY_CHAR_OWNERSHIP")) {
        c.verify_char_ownership = parse_switch("WEB_VERIFY_CHAR_OWNERSHIP", *v);
    }
    if (auto v = env("WEB_DB_URL"); v && !v->empty()) {
        c.db_url = *v;
    } else if (auto d = env("DATABASE_URL")) {
        c.db_url = *d;
    }
    return c;
}

}  // namespace rathena::web
