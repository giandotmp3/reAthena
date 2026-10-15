// cpp-httplib adapter: translates httplib requests into HttpRequest and back.
// All behavior (auth, CORS, POST-only) lives in Router; this file only moves bytes.
#include <cctype>
#include <string>

#include <httplib.h>

#include "rathena/web/config.hpp"
#include "rathena/web/log.hpp"
#include "rathena/web/router.hpp"

namespace rathena::web {
namespace {

HttpRequest to_request(const httplib::Request& in) {
    HttpRequest r;
    r.method = in.method;
    r.path = in.path;
    for (const auto& [k, v] : in.headers) {
        std::string key = k;
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        r.headers.emplace(std::move(key), v);
    }
    for (const auto& [name, file] : in.files) {
        r.fields.emplace(name, file.content);  // multipart parts
    }
    return r;
}

void write_response(const HttpResponse& in, httplib::Response& out) {
    out.status = in.status;
    for (const auto& [k, v] : in.headers) out.set_header(k, v);
    out.set_content(in.body, in.content_type);
}

}  // namespace

int serve_httplib(const Router& router, const WebConfig& config, Logger& log) {
    httplib::Server server;
    auto handler = [&router](const httplib::Request& req, httplib::Response& res) {
        write_response(router.dispatch(to_request(req)), res);
    };
    // Register every method on every path so Router can answer 405 (BR-WEB-016).
    for (const auto& p : Router::paths()) {
        server.Post(p, handler);
        server.Get(p, handler);
        server.Put(p, handler);
        server.Delete(p, handler);
        server.Patch(p, handler);
        server.Options(p, handler);
    }
    log.log(LogLevel::Info, "web-server listening on " + config.bind_ip + ":" + std::to_string(config.port));
    return server.listen(config.bind_ip, config.port) ? 0 : 1;
}

}  // namespace rathena::web
