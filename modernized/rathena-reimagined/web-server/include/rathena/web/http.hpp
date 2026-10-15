// rathena-web-server: transport-neutral HTTP request/response (spec §3.4).
//
// The client sends every parameter as a multipart/form-data part; `fields` holds
// part name -> part content (binary-safe). The httplib adapter fills these.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "rathena/web/cors.hpp"

namespace rathena::web {

inline constexpr int kHttpOk = 200;
inline constexpr int kHttpBadRequest = 400;
inline constexpr int kHttpNotFound = 404;
inline constexpr int kHttpMethodNotAllowed = 405;

struct HttpRequest {
    std::string method{"POST"};
    std::string path;
    std::map<std::string, std::string> headers;  ///< keys lower-cased
    std::map<std::string, std::string> fields;   ///< multipart parts

    bool has(const std::string& name) const { return fields.count(name) != 0; }
    const std::string* field(const std::string& name) const;
    std::string header(std::string_view lower_name) const;
};

struct HttpResponse {
    int status{kHttpOk};
    std::string content_type{"text/plain"};
    std::string body;
    HeaderList headers;
};

/// Legacy error shape: status + text/plain "Error" (spec §3.4 components.schemas.Error).
HttpResponse error_response(int status = kHttpBadRequest);

/// S3: all JSON bodies are produced by nlohmann/json.
HttpResponse json_response(const nlohmann::json& body);
HttpResponse json_response(const nlohmann::ordered_json& body);

/// Strict decimal parse: digits only, no sign/whitespace, fits in uint32.
std::optional<std::uint32_t> parse_u32(std::string_view s) noexcept;

}  // namespace rathena::web
