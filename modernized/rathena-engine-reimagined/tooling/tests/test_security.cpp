// Security invariants: no credential literal in config or fixtures.
#include <fstream>
#include <sstream>

#include "harness.hpp"
#include "tooling/config.hpp"
#include "tooling/migrate_saves.hpp"

using namespace tooling;

TEST("TL-SEC-001", env_placeholders_expand_and_missing_variable_fails) {
  Env env{{"LEGACY_DATABASE_URL", "mysql://reader@db.example.invalid/ragnarok"}};
  CHECK(expand_placeholders("${LEGACY_DATABASE_URL}", env) == env.at("LEGACY_DATABASE_URL"));
  bool threw = false;
  try { expand_placeholders("${NOPE}", env); } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
}

TEST("TL-SEC-001", literal_credentials_are_detected_placeholders_are_not) {
  CHECK(has_literal_credential("mysql://user:s3cret@host/db"));
  CHECK(has_literal_credential("host=h;password=abc123"));
  CHECK(!has_literal_credential("${LEGACY_DATABASE_URL}"));
  CHECK(!has_literal_credential("mysql://user@host/db"));
}

TEST("TL-SEC-001", legacy_db_constructor_refuses_literal_credential) {
  bool threw = false;
  try { saves::ReadOnlyLegacyDb db("mysql://u:hunter2@host/db"); } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
}

TEST("TL-SEC-002", shipped_config_has_no_literal_credentials) {
  std::ifstream f(std::string(TOOLING_SOURCE_DIR) + "/config/tooling.example.toml");
  CHECK(f.good());
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line[0] == '#') continue;
    CHECK(!has_literal_credential(line));
  }
}
