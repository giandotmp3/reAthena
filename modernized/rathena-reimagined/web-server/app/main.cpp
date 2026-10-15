// rathena web-server entry point.
//
// Configuration comes from the environment only (see conf/web-server.env.example).
// Until the persistence unit provides MySQL repositories, the process runs only
// with RATHENA_WEB_DEV_INMEMORY=1 (empty in-memory store, for local smoke tests).
#include <cstdlib>
#include <exception>
#include <iostream>

#include "rathena/web/clock.hpp"
#include "rathena/web/config.hpp"
#include "rathena/web/controllers.hpp"
#include "rathena/web/log.hpp"
#include "rathena/web/repositories.hpp"
#include "rathena/web/router.hpp"

#if defined(RATHENA_WEB_WITH_HTTPLIB)
namespace rathena::web {
int serve_httplib(const Router& router, const WebConfig& config, Logger& log);
}
#endif

int main() {
    using namespace rathena::web;
    StderrLogger log;
    WebConfig config;
    try {
        config = load_config(process_env());
    } catch (const std::exception& e) {
        log.log(LogLevel::Error, std::string("invalid configuration: ") + e.what());
        return 2;
    }

    const char* dev = std::getenv("RATHENA_WEB_DEV_INMEMORY");
    if (dev == nullptr || std::string(dev) != "1") {
        // TODO(persistence unit): MySQL repositories over config.db_url (${WEB_DB_URL}).
        log.log(LogLevel::Error,
                "no persistent repositories wired yet; set RATHENA_WEB_DEV_INMEMORY=1 for a dev run");
        return 3;
    }

    InMemoryWebStore store;
    SystemClock clock;
    WebApi api(WebServices{store, store, store, store, store, store, store, store, store, log, clock, config});
    Router router(api, config.allowed_origin_cors, log);

#if defined(RATHENA_WEB_WITH_HTTPLIB)
    return serve_httplib(router, config, log);
#else
    for (const auto& p : Router::paths()) std::cout << "POST " << p << '\n';
    log.log(LogLevel::Info, "built without RATHENA_WEB_WITH_HTTPLIB; route table printed, not serving");
    return 0;
#endif
}
