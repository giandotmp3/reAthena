// BR-WEB-009, BR-WEB-010 (inverted), BR-WEB-011, BR-WEB-012 acceptance tests.
#include "rathena/web/config_merge.hpp"
#include "test_support.hpp"

namespace rathena::web::test {
namespace {

using nlohmann::json;

// ------------------------------------------------------------------ BR-WEB-009

TEST(BR_WEB_009_UserConfigMerge, SpecExampleNullDeletes) {
    EXPECT_EQ(merge_user_config(json{{"a", 1}, {"b", 2}}, json{{"b", nullptr}, {"c", 3}}),
              (json{{"a", 1}, {"c", 3}}));
}

TEST(BR_WEB_009_UserConfigMerge, NestedObjectsMergeRecursively) {
    json stored = {{"ui", {{"x", 1}, {"y", 2}}}, {"keep", true}};
    json patch = {{"ui", {{"y", nullptr}, {"z", 3}}}};
    EXPECT_EQ(merge_user_config(stored, patch), (json{{"ui", {{"x", 1}, {"z", 3}}}, {"keep", true}}));
}

TEST(BR_WEB_009_UserConfigMerge, NullsInAFirstSaveAreNotStored) {
    EXPECT_EQ(merge_user_config(json::object(), json{{"a", nullptr}, {"b", 1}}), (json{{"b", 1}}));
}

using BR_WEB_009_Handler = WebApiFixture;

TEST_F(BR_WEB_009_Handler, SaveMergesAndLoadReturnsMergedWithType) {
    ASSERT_EQ(call(as_master("/userconfig/save", {{"data", R"({"a":1,"b":2})"}})).status, kHttpOk);
    auto saved = call(as_master("/userconfig/save", {{"data", R"({"b":null,"c":3})"}}));
    ASSERT_EQ(saved.status, kHttpOk);
    EXPECT_EQ(body_json(saved), (json{{"a", 1}, {"c", 3}}));
    auto loaded = call(as_master("/userconfig/load"));
    EXPECT_EQ(body_json(loaded), (json{{"a", 1}, {"c", 3}, {"Type", 1}}));
}

TEST_F(BR_WEB_009_Handler, InvalidJsonDataIs400AndNothingStored) {
    EXPECT_EQ(call(as_master("/userconfig/save", {{"data", "{not json"}})).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/userconfig/save", {{"data", "[1,2]"}})).status, kHttpBadRequest);
    EXPECT_EQ(store.user_config_count(), 0u);
}

// ----------------------------------------------- BR-WEB-010 (INVERTED, HITL #1)

using BR_WEB_010_LoadRequiresAuth = WebApiFixture;

const std::vector<std::string> kLoadPaths{"/userconfig/load", "/charconfig/load", "/MerchantStore/load"};

TEST_F(BR_WEB_010_LoadRequiresAuth, MissingTokenIs400OnEveryLoadRoute) {
    for (const auto& path : kLoadPaths) {
        auto r = call(req(path, {{"AID", std::to_string(kAidMaster)},
                                 {"WorldName", kWorld},
                                 {"GID", std::to_string(kGidMaster)},
                                 {"Type", "0"}}));
        EXPECT_EQ(r.status, kHttpBadRequest) << path;
        EXPECT_EQ(r.body, "Error") << path;
    }
}

TEST_F(BR_WEB_010_LoadRequiresAuth, InvalidTokenIs400OnEveryLoadRoute) {
    for (const auto& path : kLoadPaths) {
        auto r = call(req(path, {{"AID", std::to_string(kAidMaster)},
                                 {"AuthToken", "FAKETOKENWRONG99"},
                                 {"WorldName", kWorld},
                                 {"GID", std::to_string(kGidMaster)},
                                 {"Type", "0"}}));
        EXPECT_EQ(r.status, kHttpBadRequest) << path;
    }
}

TEST_F(BR_WEB_010_LoadRequiresAuth, AnotherAccountsTokenCannotReadMyConfig) {
    call(as_master("/userconfig/save", {{"data", R"({"secret":1})"}}));
    auto r = call(req("/userconfig/load", {{"AID", std::to_string(kAidMaster)},
                                           {"AuthToken", kTokenMember},
                                           {"WorldName", kWorld}}));
    EXPECT_EQ(r.status, kHttpBadRequest);
}

TEST_F(BR_WEB_010_LoadRequiresAuth, UnknownAccountLoadInsertsNoRow) {
    for (const auto& path : kLoadPaths) {
        auto r = call(as_master(path, {{"GID", std::to_string(kGidMaster)}, {"Type", "0"}}));
        EXPECT_EQ(r.status, kHttpOk) << path;
        EXPECT_EQ(body_json(r), (json{{"Type", 1}})) << path;
    }
    EXPECT_EQ(store.user_config_count(), 0u);
    EXPECT_EQ(store.char_config_count(), 0u);
    EXPECT_EQ(store.merchant_config_count(), 0u);
}

TEST_F(BR_WEB_010_LoadRequiresAuth, UnauthenticatedLoadForUnknownAccountInsertsNoRow) {
    call(req("/userconfig/load", {{"AID", std::to_string(kAidNoToken)}, {"WorldName", kWorld}}));
    EXPECT_EQ(store.user_config_count(), 0u);
}

TEST(BR_WEB_010_LoadRequiresAuth_Pending, ClientCaptureConfirmsTokenIsSentOnLoad) {
    GTEST_SKIP() << "BR-WEB-010 pending: architecture §6 / §10 phase-2 gate. A client-capture "
                    "test must confirm real clients send AuthToken on */load before the inverted "
                    "rule ships (risk to CAP-24). Needs a recorded client capture fixture.";
}

// ------------------------------------------------------------------ BR-WEB-011

TEST(BR_WEB_011_CharConfigMerge, NullPreservesStoredValue) {
    EXPECT_EQ(merge_char_config(json{{"a", 1}, {"b", 2}}, json{{"b", nullptr}, {"c", 3}}),
              (json{{"a", 1}, {"b", 2}, {"c", 3}}));
}

TEST(BR_WEB_011_CharConfigMerge, NestedNullPreserves) {
    EXPECT_EQ(merge_char_config(json{{"ui", {{"x", 1}}}}, json{{"ui", {{"x", nullptr}, {"y", 2}}}}),
              (json{{"ui", {{"x", 1}, {"y", 2}}}}));
}

using BR_WEB_011_Handler = WebApiFixture;

TEST_F(BR_WEB_011_Handler, SaveMergesWithNullPreservingAndLoadAddsType) {
    const auto gid = std::to_string(kGidMaster);
    call(as_master("/charconfig/save", {{"GID", gid}, {"data", R"({"a":1,"b":2})"}}));
    auto saved = call(as_master("/charconfig/save", {{"GID", gid}, {"data", R"({"b":null,"c":3})"}}));
    EXPECT_EQ(body_json(saved), (json{{"a", 1}, {"b", 2}, {"c", 3}}));
    EXPECT_EQ(body_json(call(as_master("/charconfig/load", {{"GID", gid}}))),
              (json{{"a", 1}, {"b", 2}, {"c", 3}, {"Type", 1}}));
}

TEST_F(BR_WEB_011_Handler, SecureDefaultRejectsForeignGid) {
    // Decided 2026-10-15: GID must belong to AID (WEB_VERIFY_CHAR_OWNERSHIP defaults to yes).
    auto r = call(as_master("/charconfig/save", {{"GID", std::to_string(kGidMember)}, {"data", "{}"}}));
    EXPECT_EQ(r.status, kHttpBadRequest);
    EXPECT_EQ(store.char_config_count(), 0u);
}


// ------------------------------------------------------------------ BR-WEB-012

using BR_WEB_012_MerchantStore = WebApiFixture;

TEST_F(BR_WEB_012_MerchantStore, UpsertPerAidGidWorldType) {
    const auto gid = std::to_string(kGidMaster);
    call(as_master("/MerchantStore/save", {{"GID", gid}, {"Type", "0"}, {"data", R"({"v":1})"}}));
    call(as_master("/MerchantStore/save", {{"GID", gid}, {"Type", "0"}, {"data", R"({"v":2})"}}));
    EXPECT_EQ(store.merchant_config_count(), 1u);
    call(as_master("/MerchantStore/save", {{"GID", gid}, {"Type", "1"}, {"data", R"({"v":9})"}}));
    call(as_master("/MerchantStore/save", {{"GID", std::to_string(kGidMasterAlt)}, {"Type", "0"}, {"data", "{}"}}));
    EXPECT_EQ(store.merchant_config_count(), 3u);

    EXPECT_EQ(body_json(call(as_master("/MerchantStore/load", {{"GID", gid}, {"Type", "0"}}))),
              (json{{"v", 2}, {"Type", 1}}));
    EXPECT_EQ(body_json(call(as_master("/MerchantStore/load", {{"GID", gid}, {"Type", "1"}}))),
              (json{{"v", 9}, {"Type", 1}}));
}

TEST_F(BR_WEB_012_MerchantStore, SaveIsAReplaceNotAMerge) {
    const auto gid = std::to_string(kGidMaster);
    call(as_master("/MerchantStore/save", {{"GID", gid}, {"Type", "0"}, {"data", R"({"a":1})"}}));
    call(as_master("/MerchantStore/save", {{"GID", gid}, {"Type", "0"}, {"data", R"({"b":2})"}}));
    EXPECT_EQ(store.find_merchant_config(kAidMaster, kGidMaster, kWorld, 0)->data, (json{{"b", 2}}));
}

TEST_F(BR_WEB_012_MerchantStore, MissingKeyFieldsAre400) {
    EXPECT_EQ(call(as_master("/MerchantStore/save", {{"Type", "0"}})).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/MerchantStore/save", {{"GID", std::to_string(kGidMaster)}})).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/MerchantStore/save", {{"GID", std::to_string(kGidMaster)}, {"Type", "256"}})).status,
              kHttpBadRequest);
}

}  // namespace
}  // namespace rathena::web::test
