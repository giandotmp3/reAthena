// Security invariants (architecture and spec section 4.1). Active.
#include <fstream>
#include <sstream>

#include "fixture.hpp"

using namespace gt;

static std::string slurp(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

TEST("SEC-S1", config_defaults_are_env_placeholders_not_literals) {
  GatewayConfig c;
  CHECK(c.database_url == "${DATABASE_URL}");
  CHECK(c.login_bind == "${LOGIN_BIND}");
  std::string toml = slurp(std::string(GATEWAY_SOURCE_DIR) + "/config/gateway.example.toml");
  CHECK(!toml.empty());
  CHECK(toml.find("${DATABASE_URL}") != std::string::npos);
  CHECK(toml.find("passwd") == std::string::npos && toml.find("password") == std::string::npos);
  CHECK(toml.find("://") == std::string::npos);  // no literal connection strings
}

TEST("SEC-S1", expand_env_reads_environment_only_and_unset_is_empty) {
  setenv("GATEWAY_TEST_FAKE_VAR", "fake-value", 1);
  CHECK(expand_env("pre-${GATEWAY_TEST_FAKE_VAR}-post") == "pre-fake-value-post");
  unsetenv("GATEWAY_TEST_FAKE_VAR");
  CHECK(expand_env("${GATEWAY_TEST_FAKE_VAR}").empty());
  CHECK(expand_env("no placeholders") == "no placeholders");
}

TEST("SEC-S4", declared_length_must_cover_header_and_stay_in_bounds) {
  CHECK(!rules::declared_length_ok(0, 4));
  CHECK(!rules::declared_length_ok(3, 4));
  CHECK(rules::declared_length_ok(4, 4));
  CHECK(rules::declared_length_ok(32768, 4));
  CHECK(!rules::declared_length_ok(32769, 4));
}

TEST("SEC-S5", unauthenticated_session_never_reaches_char_handlers) {
  using rules::CharPacket;
  for (auto p : {CharPacket::Ping, CharPacket::PinCheck, CharPacket::PinSet, CharPacket::CharList,
                 CharPacket::SelectChar, CharPacket::MakeChar, CharPacket::DeleteChar, CharPacket::Other})
    CHECK(!rules::char_packet_allowed(rules::SessionPhase::Unauthenticated, p));
  CHECK(rules::char_packet_allowed(rules::SessionPhase::Unauthenticated, CharPacket::Connect));
}

TEST("SEC-S6", fixture_hasher_never_stores_plaintext_for_argon2id_accounts) {
  Env e;
  auto& a = e.add_account();
  CHECK(a.scheme == PasswordScheme::Argon2id);
  CHECK(a.stored_secret != kFakePass);
}

PENDING_TEST("SEC-S6", real_argon2id_hasher_verifies_and_rejects_wrong_password) {
  // Production hasher (bounded pool, libargon2) is not wired in the skeleton.
  throw NotImplemented("Argon2idPoolHasher");
}

PENDING_TEST("SEC-OVERLOAD", gateway_hash_queue_is_bounded_and_refuses_beyond_cap) {
  throw NotImplemented("Argon2id pool queue cap (failure-mode table: gateway overload)");
}
