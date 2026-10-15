// BR-WEB-004 .. BR-WEB-008 acceptance tests (guild emblems).
#include "rathena/web/emblem_rules.hpp"
#include "test_support.hpp"

namespace rathena::web::test {
namespace {

std::map<std::string, std::string> upload_fields(const std::string& img, const std::string& type = "BMP") {
    return {{"GDID", std::to_string(kGuild)}, {"ImgType", type}, {"Img", img}};
}

// ------------------------------------------------------------------ BR-WEB-004

using BR_WEB_004_EmblemUploadGuildMasterOnly = WebApiFixture;

TEST_F(BR_WEB_004_EmblemUploadGuildMasterOnly, GuildMasterCanUpload) {
    auto r = call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 0))));
    EXPECT_EQ(r.status, kHttpOk);
    EXPECT_TRUE(store.find_emblem(kGuild, kWorld).has_value());
}

TEST_F(BR_WEB_004_EmblemUploadGuildMasterOnly, AuthenticatedNonMasterIsRejectedAndNothingStored) {
    auto r = call(as_member("/emblem/upload", upload_fields(make_bmp24(24, 24, 0))));
    EXPECT_EQ(r.status, kHttpBadRequest);
    EXPECT_FALSE(store.find_emblem(kGuild, kWorld).has_value());
}

TEST_F(BR_WEB_004_EmblemUploadGuildMasterOnly, MissingGdidIsRejected) {
    auto fields = upload_fields(make_bmp24(24, 24, 0));
    fields.erase("GDID");
    EXPECT_EQ(call(as_master("/emblem/upload", fields)).status, kHttpBadRequest);
}

TEST_F(BR_WEB_004_EmblemUploadGuildMasterOnly, MasterOfAnotherGuildIsRejected) {
    store.put_guild({kGuild + 1, kGidMember});
    auto fields = upload_fields(make_bmp24(24, 24, 0));
    fields["GDID"] = std::to_string(kGuild + 1);
    EXPECT_EQ(call(as_master("/emblem/upload", fields)).status, kHttpBadRequest);
}

// ------------------------------------------------------------------ BR-WEB-005

TEST(BR_WEB_005_EmblemFormat, Bmp60000BytesIsTooLarge) {
    const auto bmp = make_bmp24(24, 24, 0, 60'000 - make_bmp24(24, 24, 0).size());
    ASSERT_EQ(bmp.size(), 60'000u);
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, bmp, {}), EmblemVerdict::TooLarge);
}

TEST(BR_WEB_005_EmblemFormat, Exactly50000BytesIsAccepted) {
    const auto bmp = make_bmp24(24, 24, 0, kMaxEmblemSize - make_bmp24(24, 24, 0).size());
    ASSERT_EQ(bmp.size(), kMaxEmblemSize);
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, bmp, {}), EmblemVerdict::Ok);
    EXPECT_EQ(validate_emblem(EmblemType::Gif, make_gif(kMaxEmblemSize), {}), EmblemVerdict::Ok);
    EXPECT_EQ(validate_emblem(EmblemType::Gif, make_gif(kMaxEmblemSize + 1), {}), EmblemVerdict::TooLarge);
}

TEST(BR_WEB_005_EmblemFormat, BmpHeaderChecks) {
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, "BM12", {}), EmblemVerdict::TooShort);
    auto bad_magic = make_bmp24(24, 24, 0);
    bad_magic[0] = 'X';
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, bad_magic, {}), EmblemVerdict::BadMagic);
    auto size_mismatch = make_bmp24(24, 24, 0);
    size_mismatch.push_back('\0');  // bfSize no longer equals length
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, size_mismatch, {}), EmblemVerdict::SizeMismatch);
}

TEST(BR_WEB_005_EmblemFormat, GifChecks) {
    EXPECT_EQ(validate_emblem(EmblemType::Gif, make_gif(100), {}), EmblemVerdict::Ok);
    EXPECT_EQ(validate_emblem(EmblemType::Gif, std::string(100, 'x'), {}), EmblemVerdict::BadMagic);
    EmblemPolicy no_gifs;
    no_gifs.allow_gifs = false;
    EXPECT_EQ(validate_emblem(EmblemType::Gif, make_gif(100), no_gifs), EmblemVerdict::GifDisabled);
}

TEST(BR_WEB_005_EmblemFormat, OnlyBmpAndGifTypes) {
    EXPECT_EQ(parse_emblem_type("BMP"), EmblemType::Bmp);
    EXPECT_EQ(parse_emblem_type("GIF"), EmblemType::Gif);
    EXPECT_FALSE(parse_emblem_type("PNG").has_value());
    EXPECT_FALSE(parse_emblem_type("bmp").has_value());
}

using BR_WEB_005_Handler = WebApiFixture;

TEST_F(BR_WEB_005_Handler, Upload60000ByteBmpIs400) {
    const auto bmp = make_bmp24(24, 24, 0, 60'000 - make_bmp24(24, 24, 0).size());
    auto r = call(as_master("/emblem/upload", upload_fields(bmp)));
    EXPECT_EQ(r.status, kHttpBadRequest);
    EXPECT_FALSE(store.find_emblem(kGuild, kWorld).has_value());
}

TEST_F(BR_WEB_005_Handler, UnknownImgTypeIs400) {
    EXPECT_EQ(call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 0), "PNG"))).status,
              kHttpBadRequest);
}

TEST_F(BR_WEB_005_Handler, GifRejectedWhenDisabledByConfig) {
    config.allow_gifs = false;
    rebuild();
    EXPECT_EQ(call(as_master("/emblem/upload", upload_fields(make_gif(200), "GIF"))).status, kHttpBadRequest);
}

// ------------------------------------------------------------------ BR-WEB-006

TEST(BR_WEB_006_EmblemTransparency, PercentOf24BitBitmap) {
    EXPECT_EQ(bmp_transparency_percent(make_bmp24(24, 24, 0)), 0);
    EXPECT_EQ(bmp_transparency_percent(make_bmp24(24, 24, 288)), 50);
    EXPECT_EQ(bmp_transparency_percent(make_bmp24(24, 24, 576)), 100);
}

TEST(BR_WEB_006_EmblemTransparency, PercentOf8BitPalettizedBitmap) {
    EXPECT_EQ(bmp_transparency_percent(make_bmp8(24, 24, 0)), 0);
    EXPECT_EQ(bmp_transparency_percent(make_bmp8(24, 24, 576)), 100);
}

TEST(BR_WEB_006_EmblemTransparency, LimitIsAppliedAtShippedDefault80) {
    EmblemPolicy p;  // transparency_limit = 80 (shipped)
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, make_bmp24(24, 24, 576), p), EmblemVerdict::TooTransparent);
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, make_bmp24(24, 24, 288), p), EmblemVerdict::Ok);
    // 81% > 80 rejected; 80% accepted (strictly greater fails).
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, make_bmp24(10, 10, 81), p), EmblemVerdict::TooTransparent);
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, make_bmp24(10, 10, 80), p), EmblemVerdict::Ok);
}

TEST(BR_WEB_006_EmblemTransparency, LimitOf100DisablesTheCheck) {
    EmblemPolicy p;
    p.transparency_limit = 100;
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, make_bmp24(24, 24, 576), p), EmblemVerdict::Ok);
}

TEST(BR_WEB_006_EmblemTransparency, UnparseableBitmapFailsClosedWhileLimitActive) {
    auto bmp = make_bmp24(24, 24, 0);
    bmp[30] = 3;  // BI_BITFIELDS: not analysed -> reject rather than skip the check
    EXPECT_EQ(validate_emblem(EmblemType::Bmp, bmp, {}), EmblemVerdict::UnsupportedBitmap);
}

TEST(BR_WEB_006_EmblemTransparency, ConfigKeyReachesTheValidator) {
    // [DEFECT] legacy parsed emblem_transparency_limit into the wrong variable.
    auto cfg = load_config([](std::string_view k) -> std::optional<std::string> {
        if (k == "WEB_EMBLEM_TRANSPARENCY_LIMIT") return "10";
        return std::nullopt;
    });
    EXPECT_EQ(cfg.emblem_transparency_limit, 10);
}

using BR_WEB_006_Handler = WebApiFixture;

TEST_F(BR_WEB_006_Handler, ConfiguredLimitIsEnforcedOnUpload) {
    config.emblem_transparency_limit = 10;
    rebuild();
    EXPECT_EQ(call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 288)))).status, kHttpBadRequest);
    EXPECT_EQ(call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 10)))).status, kHttpOk);
}

// ------------------------------------------------------------------ BR-WEB-007

TEST(BR_WEB_007_EmblemVersion, StartsAtOneAndIncrements) {
    EXPECT_EQ(next_emblem_version(std::nullopt), 1u);
    EXPECT_EQ(next_emblem_version(1u), 2u);
    EXPECT_EQ(next_emblem_version(41u), 42u);
}

using BR_WEB_007_Handler = WebApiFixture;

TEST_F(BR_WEB_007_Handler, EachUploadBumpsTheVersionPerGuildAndWorld) {
    auto r1 = call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 0))));
    ASSERT_EQ(r1.status, kHttpOk);
    EXPECT_EQ(body_json(r1), (nlohmann::json{{"Type", 1}, {"version", 1}}));
    auto r2 = call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 1))));
    EXPECT_EQ(body_json(r2)["version"], 2);

    auto other_world = as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 0)));
    other_world.fields["WorldName"] = "OtherFakeWorld";
    EXPECT_EQ(body_json(call(other_world))["version"], 1);
}

TEST_F(BR_WEB_007_Handler, RejectedUploadDoesNotBumpTheVersion) {
    call(as_master("/emblem/upload", upload_fields(make_bmp24(24, 24, 0))));
    call(as_master("/emblem/upload", upload_fields(std::string(100, 'x'))));
    EXPECT_EQ(store.find_emblem(kGuild, kWorld)->version, 1u);
}

// ------------------------------------------------------------------ BR-WEB-008

using BR_WEB_008_EmblemDownload = WebApiFixture;

TEST_F(BR_WEB_008_EmblemDownload, AnyAuthenticatedUserCanDownload) {
    const auto bmp = make_bmp24(24, 24, 0);
    ASSERT_EQ(call(as_master("/emblem/upload", upload_fields(bmp))).status, kHttpOk);
    auto r = call(as_member("/emblem/download", {{"GDID", std::to_string(kGuild)}}));
    EXPECT_EQ(r.status, kHttpOk);
    EXPECT_EQ(r.content_type, "image/bmp");
    EXPECT_EQ(r.body, bmp);
}

TEST_F(BR_WEB_008_EmblemDownload, GifIsServedAsImageGif) {
    ASSERT_EQ(call(as_master("/emblem/upload", upload_fields(make_gif(64), "GIF"))).status, kHttpOk);
    EXPECT_EQ(call(as_member("/emblem/download", {{"GDID", std::to_string(kGuild)}})).content_type, "image/gif");
}

TEST_F(BR_WEB_008_EmblemDownload, AbsentEmblemIs404) {
    EXPECT_EQ(call(as_member("/emblem/download", {{"GDID", "999"}})).status, kHttpNotFound);
}

TEST_F(BR_WEB_008_EmblemDownload, UnauthenticatedIs400) {
    EXPECT_EQ(call(req("/emblem/download", {{"GDID", std::to_string(kGuild)}, {"WorldName", kWorld}})).status,
              kHttpBadRequest);
}

}  // namespace
}  // namespace rathena::web::test
