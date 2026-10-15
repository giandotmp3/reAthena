// BR-WEB-013, BR-WEB-014, BR-WEB-015 acceptance tests (party booking).
#include <chrono>

#include "rathena/web/auth_rules.hpp"
#include "rathena/web/party_booking_rules.hpp"
#include "test_support.hpp"

namespace rathena::web::test {
namespace {

using namespace std::chrono_literals;
using nlohmann::json;

std::map<std::string, std::string> booking_fields(std::map<std::string, std::string> overrides = {}) {
    std::map<std::string, std::string> f{
        {"GID", std::to_string(kGidMaster)}, {"CharName", "FakeLeader"}, {"Type", "1"},
        {"Assist", "0"}, {"Dealer", "1"}, {"Healer", "0"}, {"Tanker", "0"},
        {"MinLV", "1"}, {"MaxLV", "99"}, {"Memo", "Fake memo"},
    };
    for (auto& [k, v] : overrides) f[k] = v;
    return f;
}

PartyBooking booking(AccountId aid, std::uint16_t min, std::uint16_t max, Timestamp created,
                     std::string memo = "", bool healer = false, bool tanker = false, std::uint16_t purpose = 1) {
    PartyBooking b;
    b.world_name = kWorld;
    b.account_id = aid;
    b.char_id = aid + 1;
    b.char_name = "FakeChar";
    b.purpose = purpose;
    b.healer = healer;
    b.tanker = tanker;
    b.minimum_level = min;
    b.maximum_level = max;
    b.comment = std::move(memo);
    b.created = created;
    return b;
}

const Timestamp kT0{std::chrono::seconds{1'700'000'000}};

// ------------------------------------------------------------------ BR-WEB-013

TEST(BR_WEB_013_PartyBookingRules, FieldLengthLimits) {
    auto b = booking(kAidMaster, 1, 99, kT0);
    EXPECT_EQ(validate_booking(b), BookingVerdict::Ok);
    b.char_name = std::string(kCharNameLength, 'n');
    EXPECT_EQ(validate_booking(b), BookingVerdict::Ok);
    b.char_name.push_back('n');
    EXPECT_EQ(validate_booking(b), BookingVerdict::CharNameTooLong);
    b.char_name = "ok";
    b.comment = std::string(kCommentLength + 1, 'm');
    EXPECT_EQ(validate_booking(b), BookingVerdict::CommentTooLong);
    b.comment.clear();
    b.world_name = std::string(kWorldNameLength + 1, 'w');
    EXPECT_EQ(validate_booking(b), BookingVerdict::WorldNameTooLong);
    b.world_name = kWorld;
    b.purpose = static_cast<std::uint16_t>(BookingPurpose::Max);
    EXPECT_EQ(validate_booking(b), BookingVerdict::InvalidPurpose);
}

TEST(BR_WEB_013_PartyBookingRules, LeaderOnly) {
    const PartyRecord party{1, kAidMaster, kGidMaster};
    EXPECT_TRUE(is_party_leader(kAidMaster, kGidMaster, party));
    EXPECT_FALSE(is_party_leader(kAidMaster, kGidMasterAlt, party));
    EXPECT_FALSE(is_party_leader(kAidMember, kGidMember, party));
    EXPECT_FALSE(is_party_leader(kAidMaster, kGidMaster, std::nullopt));
}

using BR_WEB_013_Handler = WebApiFixture;

TEST_F(BR_WEB_013_Handler, LeaderCanAdd) {
    auto r = call(as_master("/party/add", booking_fields()));
    EXPECT_EQ(r.status, kHttpOk);
    EXPECT_EQ(body_json(r), (json{{"Type", 1}}));
    EXPECT_EQ(store.booking_count(), 1u);
}

TEST_F(BR_WEB_013_Handler, NonLeaderIsRejected) {
    auto r = call(as_member("/party/add", booking_fields({{"GID", std::to_string(kGidMember)}})));
    EXPECT_EQ(r.status, kHttpBadRequest);
    EXPECT_EQ(store.booking_count(), 0u);
}

TEST_F(BR_WEB_013_Handler, LeaderAccountWithNonLeaderCharacterIsRejected) {
    EXPECT_EQ(call(as_master("/party/add", booking_fields({{"GID", std::to_string(kGidMasterAlt)}}))).status,
              kHttpBadRequest);
}

TEST_F(BR_WEB_013_Handler, OversizedFieldsAreRejected) {
    EXPECT_EQ(call(as_master("/party/add", booking_fields({{"CharName", std::string(24, 'n')}}))).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/party/add", booking_fields({{"Memo", std::string(256, 'm')}}))).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/party/add", booking_fields({{"Type", "7"}}))).status, kHttpBadRequest);
    auto long_world = as_master("/party/add", booking_fields());
    long_world.fields["WorldName"] = std::string(33, 'w');
    EXPECT_EQ(call(long_world).status, kHttpBadRequest);
    EXPECT_EQ(store.booking_count(), 0u);
}

TEST_F(BR_WEB_013_Handler, OneBookingPerWorldAidGid) {
    call(as_master("/party/add", booking_fields({{"Memo", "first"}})));
    call(as_master("/party/add", booking_fields({{"Memo", "second"}})));
    ASSERT_EQ(store.booking_count(), 1u);
    EXPECT_EQ(store.find_bookings(kWorld, kAidMaster).front().comment, "second");
}

// ------------------------------------------------------------------ BR-WEB-014

// Decided 2026-10-15 (spec §6 Q4): exact min/max match, as in legacy; no range overlap.
TEST(BR_WEB_014_PartyBookingSearch, ExactLevelMatch) {
    BookingSearch s;
    s.minimum_level = 1;
    s.maximum_level = 99;
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0), s));
    EXPECT_FALSE(booking_matches(booking(1, 1, 98, kT0), s));
    EXPECT_FALSE(booking_matches(booking(1, 2, 99, kT0), s));
}

TEST(BR_WEB_014_PartyBookingSearch, RolesAreOred) {
    BookingSearch s;
    s.minimum_level = 1;
    s.maximum_level = 99;
    s.healer = true;
    s.tanker = true;
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0, "", /*healer=*/true, false), s));
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0, "", false, /*tanker=*/true), s));
    EXPECT_FALSE(booking_matches(booking(1, 1, 99, kT0, "", false, false), s));
}

TEST(BR_WEB_014_PartyBookingSearch, PurposeFilterUnlessAll) {
    BookingSearch s;
    s.minimum_level = 1;
    s.maximum_level = 99;
    s.purpose = 3;
    EXPECT_FALSE(booking_matches(booking(1, 1, 99, kT0, "", false, false, 1), s));
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0, "", false, false, 3), s));
    s.purpose = 0;  // All
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0, "", false, false, 1), s));
}

TEST(BR_WEB_014_PartyBookingSearch, MemoSubstring) {
    BookingSearch s;
    s.minimum_level = 1;
    s.maximum_level = 99;
    s.memo = "glast";
    EXPECT_TRUE(booking_matches(booking(1, 1, 99, kT0, "LF healer for Glast Heim"), s));
    EXPECT_FALSE(booking_matches(booking(1, 1, 99, kT0, "Orc dungeon"), s));
    s.memo = "%";  // treated literally (legacy LIKE did not escape wildcards)
    EXPECT_FALSE(booking_matches(booking(1, 1, 99, kT0, "Orc dungeon"), s));
}

TEST(BR_WEB_014_PartyBookingSearch, NewestFirst) {
    BookingSearch s;
    s.minimum_level = 1;
    s.maximum_level = 99;
    auto out = search_bookings({booking(1, 1, 99, kT0), booking(2, 1, 99, kT0 + 20s), booking(3, 1, 99, kT0 + 10s)}, s);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].account_id, 2u);
    EXPECT_EQ(out[1].account_id, 3u);
    EXPECT_EQ(out[2].account_id, 1u);
}

using BR_WEB_014_Handler = WebApiFixture;

TEST_F(BR_WEB_014_Handler, SearchRouteReturnsMatchingPage) {
    store.save_booking(booking(kAidMember, 1, 99, clock.now(), "fake memo", true));
    store.save_booking(booking(kAidNoToken, 10, 20, clock.now()));
    auto r = call(as_master("/party/search", {{"MinLV", "1"}, {"MaxLV", "99"}, {"Healer", "1"}}));
    ASSERT_EQ(r.status, kHttpOk);
    auto j = body_json(r);
    EXPECT_EQ(j["Type"], 1);
    EXPECT_EQ(j["totalPage"], 1);
    ASSERT_EQ(j["data"].size(), 1u);
    EXPECT_EQ(j["data"][0]["AID"], kAidMember);
}

TEST_F(BR_WEB_014_Handler, SearchRequiresLevels) {
    EXPECT_EQ(call(as_master("/party/search", {{"MinLV", "1"}})).status, kHttpBadRequest);
}


// ------------------------------------------------------------------ BR-WEB-015

using BR_WEB_015_PartyBookingCrud = WebApiFixture;

TEST_F(BR_WEB_015_PartyBookingCrud, DeleteRemovesOwnBookingsInThatWorldOnly) {
    call(as_master("/party/add", booking_fields()));
    auto other = as_master("/party/add", booking_fields());
    other.fields["WorldName"] = "OtherFakeWorld";
    call(other);
    store.save_booking(booking(kAidMember, 1, 99, clock.now()));
    ASSERT_EQ(store.booking_count(), 3u);

    EXPECT_EQ(body_json(call(as_master("/party/del"))), (json{{"Type", 1}}));
    EXPECT_TRUE(store.find_bookings(kWorld, kAidMaster).empty());
    EXPECT_EQ(store.find_bookings("OtherFakeWorld", kAidMaster).size(), 1u);
    EXPECT_EQ(store.find_bookings(kWorld, kAidMember).size(), 1u);
}

TEST_F(BR_WEB_015_PartyBookingCrud, GetReturnsOwnBookingAsObject) {
    EXPECT_EQ(body_json(call(as_master("/party/get"))), (json{{"Type", 1}}));
    call(as_master("/party/add", booking_fields()));
    auto j = body_json(call(as_master("/party/get")));
    ASSERT_TRUE(j["data"].is_object());
    EXPECT_EQ(j["data"]["AID"], kAidMaster);
    EXPECT_EQ(j["data"]["GID"], kGidMaster);
    EXPECT_EQ(j["data"]["CharName"], "FakeLeader");
    EXPECT_EQ(j["data"]["WorldName"], kWorld);
    EXPECT_EQ(j["data"]["Dealer"], 1);
    EXPECT_EQ(j["data"]["Healer"], 0);
    EXPECT_EQ(j["data"]["MinLV"], 1);
    EXPECT_EQ(j["data"]["MaxLV"], 99);
    EXPECT_EQ(j["data"]["Memo"], "Fake memo");
    EXPECT_EQ(j["data"]["Type"], 1);
}

TEST_F(BR_WEB_015_PartyBookingCrud, InfoLooksUpQueryAidAndReturnsArray) {
    call(as_master("/party/add", booking_fields()));
    auto j = body_json(call(as_member("/party/info", {{"QueryAID", std::to_string(kAidMaster)}})));
    ASSERT_TRUE(j["data"].is_array());
    ASSERT_EQ(j["data"].size(), 1u);
    EXPECT_EQ(j["data"][0]["AID"], kAidMaster);
    EXPECT_EQ(body_json(call(as_member("/party/info", {{"QueryAID", "1"}}))), (json{{"Type", 1}}));
    EXPECT_EQ(call(as_member("/party/info")).status, kHttpBadRequest);
}

TEST_F(BR_WEB_015_PartyBookingCrud, ListIsNewestFirstWithTotal) {
    store.save_booking(booking(kAidMember, 1, 99, clock.now()));
    clock.advance(30s);
    call(as_master("/party/add", booking_fields()));
    auto j = body_json(call(as_member("/party/list")));
    EXPECT_EQ(j["Type"], 1);
    EXPECT_EQ(j["totalPage"], 2);
    ASSERT_EQ(j["data"].size(), 2u);
    EXPECT_EQ(j["data"][0]["AID"], kAidMaster);
    EXPECT_EQ(j["data"][1]["AID"], kAidMember);
}

TEST_F(BR_WEB_015_PartyBookingCrud, SerializerEscapesUserText) {
    // S3: legacy concatenated strings, so a quote in Memo broke the JSON.
    call(as_master("/party/add", booking_fields({{"Memo", "he said \"hi\" \\ </script>"}, {"CharName", "Fake\"Name"}})));
    auto r = call(as_master("/party/list"));
    auto j = json::parse(r.body);  // throws if malformed
    EXPECT_EQ(j["data"][0]["Memo"], "he said \"hi\" \\ </script>");
    EXPECT_EQ(j["data"][0]["CharName"], "Fake\"Name");
}

TEST_F(BR_WEB_015_PartyBookingCrud, AllPartyRoutesRequireAuth) {
    for (const std::string path : {"/party/add", "/party/del", "/party/get", "/party/info", "/party/list", "/party/search"}) {
        EXPECT_EQ(call(req(path, {{"WorldName", kWorld}})).status, kHttpBadRequest) << path;
    }
}

}  // namespace
}  // namespace rathena::web::test
