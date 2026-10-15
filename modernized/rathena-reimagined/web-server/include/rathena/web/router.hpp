// rathena-web-server: route table + cross-cutting policy (BR-WEB-016).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "rathena/web/controllers.hpp"
#include "rathena/web/http.hpp"

namespace rathena::web {

class Router {
public:
    Router(WebApi& api, std::string allowed_origin_cors, Logger& log);

    /// Unknown path -> 404; non-POST -> 405 (Allow: POST); handler exceptions
    /// -> 400 "Error". CORS headers appended only for the exact configured origin.
    HttpResponse dispatch(const HttpRequest& req) const;

    /// The 14 paths of spec §3.4, in registration order.
    static const std::vector<std::string>& paths();

private:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    std::map<std::string, Handler> routes_;
    std::string allowed_origin_;
    Logger& log_;
};

}  // namespace rathena::web
