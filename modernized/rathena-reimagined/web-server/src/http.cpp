#include "rathena/web/http.hpp"

#include <limits>

namespace rathena::web {

const std::string* HttpRequest::field(const std::string& name) const {
    auto it = fields.find(name);
    return it == fields.end() ? nullptr : &it->second;
}

std::string HttpRequest::header(std::string_view lower_name) const {
    auto it = headers.find(std::string(lower_name));
    return it == headers.end() ? std::string{} : it->second;
}

HttpResponse error_response(int status) {
    HttpResponse r;
    r.status = status;
    r.content_type = "text/plain";
    r.body = "Error";
    return r;
}

HttpResponse json_response(const nlohmann::json& body) {
    HttpResponse r;
    r.content_type = "application/json";
    r.body = body.dump();
    return r;
}

HttpResponse json_response(const nlohmann::ordered_json& body) {
    HttpResponse r;
    r.content_type = "application/json";
    r.body = body.dump();
    return r;
}

std::optional<std::uint32_t> parse_u32(std::string_view s) noexcept {
    if (s.empty() || s.size() > 10) return std::nullopt;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (v > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(v);
}

}  // namespace rathena::web
