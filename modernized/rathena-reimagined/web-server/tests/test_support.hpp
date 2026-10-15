// Shared fixture for BR-WEB acceptance tests.
//
// All identifiers and tokens are FAKE, same-shape values (16-char tokens,
// numeric AIDs/GIDs). No value is copied from legacy code or config.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rathena/web/clock.hpp"
#include "rathena/web/config.hpp"
#include "rathena/web/controllers.hpp"
#include "rathena/web/log.hpp"
#include "rathena/web/repositories.hpp"
#include "rathena/web/router.hpp"

namespace rathena::web::test {

inline constexpr AccountId kAidMaster = 2000001;   // guild master, party leader
inline constexpr AccountId kAidMember = 2000002;   // plain member
inline constexpr AccountId kAidNoToken = 2000003;  // has no token row
inline constexpr CharId kGidMaster = 150001;
inline constexpr CharId kGidMasterAlt = 150002;
inline constexpr CharId kGidMember = 150003;
inline constexpr GuildId kGuild = 10;
inline constexpr const char* kWorld = "FakeWorld";
inline const std::string kTokenMaster = "FAKETOKENMASTR01";  // 16 chars
inline const std::string kTokenMember = "FAKETOKENMEMBR02";  // 16 chars

class WebApiFixture : public ::testing::Test {
protected:
    void SetUp() override {
        store.put_token({kAidMaster, kTokenMaster, true, std::nullopt});
        store.put_token({kAidMember, kTokenMember, true, std::nullopt});
        store.put_character({kGidMaster, kAidMaster});
        store.put_character({kGidMasterAlt, kAidMaster});
        store.put_character({kGidMember, kAidMember});
        store.put_guild({kGuild, kGidMaster});
        store.put_party({1, kAidMaster, kGidMaster});
        rebuild();
    }

    /// Recreate the API after changing `config`.
    void rebuild() {
        api = std::make_unique<WebApi>(
            WebServices{store, store, store, store, store, store, store, store, store, log, clock, config});
        router = std::make_unique<Router>(*api, config.allowed_origin_cors, log);
    }

    static HttpRequest req(const std::string& path, std::map<std::string, std::string> fields) {
        HttpRequest r;
        r.method = "POST";
        r.path = path;
        r.fields = std::move(fields);
        return r;
    }

    /// Authenticated request as the master account.
    static HttpRequest as_master(const std::string& path, std::map<std::string, std::string> fields = {}) {
        fields.emplace("AID", std::to_string(kAidMaster));
        fields.emplace("AuthToken", kTokenMaster);
        fields.emplace("WorldName", kWorld);
        return req(path, std::move(fields));
    }

    /// Authenticated request as the member account.
    static HttpRequest as_member(const std::string& path, std::map<std::string, std::string> fields = {}) {
        fields.emplace("AID", std::to_string(kAidMember));
        fields.emplace("AuthToken", kTokenMember);
        fields.emplace("WorldName", kWorld);
        return req(path, std::move(fields));
    }

    HttpResponse call(const HttpRequest& r) { return router->dispatch(r); }

    static nlohmann::json body_json(const HttpResponse& r) { return nlohmann::json::parse(r.body); }

    InMemoryWebStore store;
    ManualClock clock;
    CapturingLogger log;
    WebConfig config;
    std::unique_ptr<WebApi> api;
    std::unique_ptr<Router> router;
};

/// Uncompressed 24-bpp BMP (BITMAPINFOHEADER) with the first `magenta` pixels
/// pure magenta (FF00FF) and the rest black. Optional trailing padding.
std::string make_bmp24(int width, int height, int magenta, std::size_t trailing_pad = 0);

/// 8-bpp palettized BMP; palette[1] is magenta, palette[0] black.
std::string make_bmp8(int width, int height, int magenta);

/// Minimal GIF-signature payload of `size` bytes.
std::string make_gif(std::size_t size);

}  // namespace rathena::web::test
