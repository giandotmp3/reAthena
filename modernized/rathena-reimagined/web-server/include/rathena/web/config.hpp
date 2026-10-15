// rathena-web-server: runtime configuration.
//
// S1: no credential literal is shipped. The DB connection comes from the
// environment only (${WEB_DB_URL}, falling back to ${DATABASE_URL}).
// Shipped legacy values are the code defaults (spec §5.6 policy).
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "rathena/web/types.hpp"

namespace rathena::web {

struct WebConfig {
    std::string bind_ip{"0.0.0.0"};                 ///< ${WEB_BIND}
    std::uint16_t port{8888};                       ///< ${WEB_PORT}; conf/web_athena.conf web_port
    bool allow_gifs{true};                          ///< ${WEB_ALLOW_GIFS}; shipped: yes
    std::string allowed_origin_cors;                ///< ${WEB_ALLOWED_ORIGIN_CORS}; shipped: empty (CORS off)
    int emblem_transparency_limit{kDefaultEmblemTransparencyLimit};  ///< ${WEB_EMBLEM_TRANSPARENCY_LIMIT}; shipped 80
    std::chrono::seconds token_logout_grace{kTokenLogoutGrace};
    /// BR-WEB-011 SME question: verify GID belongs to AID on char/merchant routes.
    /// Secure default ON; legacy did not verify.
    bool verify_char_ownership{true};               ///< ${WEB_VERIFY_CHAR_OWNERSHIP}
    std::string db_url;                             ///< ${WEB_DB_URL} or ${DATABASE_URL}; never a literal
};

/// Environment lookup; returns nullopt when the variable is unset.
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

/// Reads the process environment.
EnvLookup process_env();

/// Builds the config from environment variables (see field comments).
/// Throws std::invalid_argument on malformed numeric/boolean values.
WebConfig load_config(const EnvLookup& env);

}  // namespace rathena::web
