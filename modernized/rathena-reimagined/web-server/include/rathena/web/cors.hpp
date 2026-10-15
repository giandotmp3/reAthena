// rathena-web-server: CORS and method policy (BR-WEB-016).
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rathena::web {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

/// BR-WEB-016: CORS headers are emitted only when `allowed_origin` is configured
/// (non-empty) and the request Origin equals it exactly (byte-for-byte; no
/// wildcard, no suffix/prefix match, no case folding). Otherwise empty.
HeaderList cors_headers(std::string_view allowed_origin, std::string_view request_origin);

/// BR-WEB-016: the API is POST only.
bool is_allowed_method(std::string_view method) noexcept;

}  // namespace rathena::web
