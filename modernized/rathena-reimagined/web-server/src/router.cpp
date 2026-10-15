#include "rathena/web/router.hpp"

#include <exception>

namespace rathena::web {

const std::vector<std::string>& Router::paths() {
    static const std::vector<std::string> kPaths{
        "/userconfig/load", "/userconfig/save", "/charconfig/load", "/charconfig/save",
        "/MerchantStore/load", "/MerchantStore/save", "/emblem/download", "/emblem/upload",
        "/party/add", "/party/del", "/party/get", "/party/info", "/party/list", "/party/search",
    };
    return kPaths;
}

Router::Router(WebApi& api, std::string allowed_origin_cors, Logger& log)
    : allowed_origin_(std::move(allowed_origin_cors)), log_(log) {
    auto bind = [&api](HttpResponse (WebApi::*fn)(const HttpRequest&)) {
        return [&api, fn](const HttpRequest& r) { return (api.*fn)(r); };
    };
    routes_["/userconfig/load"] = bind(&WebApi::userconfig_load);
    routes_["/userconfig/save"] = bind(&WebApi::userconfig_save);
    routes_["/charconfig/load"] = bind(&WebApi::charconfig_load);
    routes_["/charconfig/save"] = bind(&WebApi::charconfig_save);
    routes_["/MerchantStore/load"] = bind(&WebApi::merchantstore_load);
    routes_["/MerchantStore/save"] = bind(&WebApi::merchantstore_save);
    routes_["/emblem/download"] = bind(&WebApi::emblem_download);
    routes_["/emblem/upload"] = bind(&WebApi::emblem_upload);
    routes_["/party/add"] = bind(&WebApi::party_add);
    routes_["/party/del"] = bind(&WebApi::party_del);
    routes_["/party/get"] = bind(&WebApi::party_get);
    routes_["/party/info"] = bind(&WebApi::party_info);
    routes_["/party/list"] = bind(&WebApi::party_list);
    routes_["/party/search"] = bind(&WebApi::party_search);
}

HttpResponse Router::dispatch(const HttpRequest& req) const {
    HttpResponse res;
    auto it = routes_.find(req.path);
    if (it == routes_.end()) {
        res = error_response(kHttpNotFound);
    } else if (!is_allowed_method(req.method)) {
        res = error_response(kHttpMethodNotAllowed);
        res.headers.emplace_back("Allow", "POST");
    } else {
        try {
            res = it->second(req);
        } catch (const std::exception& e) {
            // Exception text may echo input; redaction applies in Logger::log.
            log_.log(LogLevel::Error, "handler failed: path=" + req.path + " what=" + e.what());
            res = error_response();
        }
    }
    for (auto& h : cors_headers(allowed_origin_, req.header("origin"))) {
        res.headers.push_back(std::move(h));
    }
    return res;
}

}  // namespace rathena::web
