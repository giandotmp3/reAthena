#include "rathena/web/cors.hpp"

namespace rathena::web {

HeaderList cors_headers(std::string_view allowed_origin, std::string_view request_origin) {
    if (allowed_origin.empty() || request_origin.empty() || request_origin != allowed_origin) {
        return {};
    }
    return {
        {"Access-Control-Allow-Origin", std::string(request_origin)},
        {"Vary", "Origin"},
        {"Access-Control-Allow-Methods", "POST"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
    };
}

bool is_allowed_method(std::string_view method) noexcept {
    return method == "POST";
}

}  // namespace rathena::web
