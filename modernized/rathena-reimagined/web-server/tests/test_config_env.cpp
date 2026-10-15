// Configuration tests (S1: no credential literal is a default; env only).
#include <map>
#include <stdexcept>

#include <gtest/gtest.h>

#include "rathena/web/config.hpp"

namespace rathena::web {
namespace {

EnvLookup env_of(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](std::string_view k) -> std::optional<std::string> {
        auto it = vars.find(std::string(k));
        if (it == vars.end()) return std::nullopt;
        return it->second;
    };
}

TEST(WebConfig_S1, NoDatabaseCredentialDefault) {
    const auto c = load_config(env_of({}));
    EXPECT_TRUE(c.db_url.empty());
}

TEST(WebConfig_S1, DbUrlFromWebDbUrlThenDatabaseUrl) {
    EXPECT_EQ(load_config(env_of({{"DATABASE_URL", "mysql://fake_user:fake_pass@db.invalid/fake"}})).db_url,
              "mysql://fake_user:fake_pass@db.invalid/fake");
    EXPECT_EQ(load_config(env_of({{"DATABASE_URL", "mysql://a.invalid/x"}, {"WEB_DB_URL", "mysql://b.invalid/y"}})).db_url,
              "mysql://b.invalid/y");
}

TEST(WebConfig_Defaults, ShippedValuesAreCodeDefaults) {
    const auto c = load_config(env_of({}));
    EXPECT_EQ(c.port, 8888);
    EXPECT_TRUE(c.allow_gifs);
    EXPECT_TRUE(c.allowed_origin_cors.empty());
    EXPECT_EQ(c.emblem_transparency_limit, 80);
    EXPECT_TRUE(c.verify_char_ownership);
}

TEST(WebConfig_Defaults, MalformedValuesThrow) {
    EXPECT_THROW(load_config(env_of({{"WEB_PORT", "70000"}})), std::invalid_argument);
    EXPECT_THROW(load_config(env_of({{"WEB_ALLOW_GIFS", "maybe"}})), std::invalid_argument);
    EXPECT_THROW(load_config(env_of({{"WEB_EMBLEM_TRANSPARENCY_LIMIT", "101"}})), std::invalid_argument);
}

}  // namespace
}  // namespace rathena::web
