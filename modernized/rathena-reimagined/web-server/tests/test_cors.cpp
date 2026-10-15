// BR-WEB-016 acceptance tests (CORS exact origin, POST only).
#include "rathena/web/cors.hpp"
#include "test_support.hpp"

namespace rathena::web::test {
namespace {

const std::string kOrigin = "https://fake-origin.example.invalid:8080";

bool has_header(const HeaderList& h, const std::string& k, const std::string& v) {
    for (const auto& [hk, hv] : h) {
        if (hk == k && hv == v) return true;
    }
    return false;
}

TEST(BR_WEB_016_Cors, ExactOriginGetsHeaders) {
    auto h = cors_headers(kOrigin, kOrigin);
    EXPECT_TRUE(has_header(h, "Access-Control-Allow-Origin", kOrigin));
    EXPECT_TRUE(has_header(h, "Vary", "Origin"));
    EXPECT_TRUE(has_header(h, "Access-Control-Allow-Methods", "POST"));
    EXPECT_TRUE(has_header(h, "Access-Control-Allow-Headers", "Content-Type, Authorization"));
}

TEST(BR_WEB_016_Cors, NearMissOriginsGetNothing) {
    for (const std::string o : {"https://fake-origin.example.invalid:8081", "https://fake-origin.example.invalid:8080/",
                                "HTTPS://FAKE-ORIGIN.EXAMPLE.INVALID:8080", "http://fake-origin.example.invalid:8080",
                                "https://evil.fake-origin.example.invalid:8080", "*", "null", ""}) {
        EXPECT_TRUE(cors_headers(kOrigin, o).empty()) << o;
    }
}

TEST(BR_WEB_016_Cors, UnconfiguredOriginDisablesCors) {
    EXPECT_TRUE(cors_headers("", kOrigin).empty());
    EXPECT_TRUE(cors_headers("", "").empty());
}

TEST(BR_WEB_016_Cors, PostOnly) {
    EXPECT_TRUE(is_allowed_method("POST"));
    for (auto m : {"GET", "PUT", "DELETE", "PATCH", "OPTIONS", "HEAD", "post"}) {
        EXPECT_FALSE(is_allowed_method(m)) << m;
    }
}

using BR_WEB_016_Router = WebApiFixture;

TEST_F(BR_WEB_016_Router, RouterAppliesCorsOnlyForConfiguredOrigin) {
    config.allowed_origin_cors = kOrigin;
    rebuild();
    auto good = as_master("/userconfig/load");
    good.headers["origin"] = kOrigin;
    EXPECT_TRUE(has_header(call(good).headers, "Access-Control-Allow-Origin", kOrigin));

    auto bad = as_master("/userconfig/load");
    bad.headers["origin"] = "https://other.example.invalid";
    EXPECT_TRUE(call(bad).headers.empty());
}

TEST_F(BR_WEB_016_Router, NonPostIs405OnEveryRoute) {
    for (const auto& path : Router::paths()) {
        auto r = as_master(path);
        r.method = "GET";
        auto res = call(r);
        EXPECT_EQ(res.status, kHttpMethodNotAllowed) << path;
        EXPECT_TRUE(has_header(res.headers, "Allow", "POST")) << path;
    }
}

TEST_F(BR_WEB_016_Router, UnknownPathIs404) {
    EXPECT_EQ(call(as_master("/admin/whatever")).status, kHttpNotFound);
}

TEST_F(BR_WEB_016_Router, ExposesExactlyTheSpecRoutes) {
    EXPECT_EQ(Router::paths().size(), 14u);
}

TEST(BR_WEB_016_Cors_Pending, HttplibTransportEndToEnd) {
    GTEST_SKIP() << "BR-WEB-016 pending: end-to-end check through the cpp-httplib adapter "
                    "(RATHENA_WEB_WITH_HTTPLIB=ON) on a loopback socket, asserting CORS headers and "
                    "405 for non-POST on the wire. Router-level behavior is covered above.";
}

}  // namespace
}  // namespace rathena::web::test
