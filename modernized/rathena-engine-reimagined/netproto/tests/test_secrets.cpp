// SEC-S1: no credential literal in shipped config or proto data.
#include <fstream>
#include <regex>
#include <sstream>

#include "harness.hpp"

namespace {
std::string slurp(const std::string& rel) {
  std::ifstream f(std::string(NETPROTO_SOURCE_DIR) + "/" + rel);
  if (!f) throw CheckFailure("cannot open " + rel);
  std::ostringstream o; o << f.rdbuf(); return o.str();
}
}  // namespace

TEST("SEC-S1", example_config_has_no_literal_secret_values) {
  const std::regex bad(R"((pass|secret|token|key|user)\w*\s*=\s*"[^$"][^"]*")", std::regex::icase);
  CHECK(!std::regex_search(slurp("config/netproto.example.toml"), bad));
}

TEST("SEC-S1", proto_obfuscation_keys_are_env_references_not_literals) {
  const auto y = slurp("proto/client_packets.yaml");
  CHECK(y.find("keys_env:") != std::string::npos);
  CHECK(!std::regex_search(y, std::regex(R"(keys\s*:\s*\[?\s*(0x)?[0-9a-fA-F]{6,})")));
}
