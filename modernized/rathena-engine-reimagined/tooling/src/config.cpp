#include "tooling/config.hpp"

#include <regex>
#include <stdexcept>

namespace tooling {

std::string expand_placeholders(const std::string& text, const Env& env) {
  static const std::regex re(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");
  std::string out;
  auto begin = std::sregex_iterator(text.begin(), text.end(), re);
  size_t last = 0;
  for (auto it = begin; it != std::sregex_iterator(); ++it) {
    out.append(text, last, it->position() - last);
    auto f = env.find((*it)[1].str());
    if (f == env.end()) throw std::runtime_error("unset environment variable: " + (*it)[1].str());
    out += f->second;
    last = it->position() + it->length();
  }
  out.append(text, last, std::string::npos);
  return out;
}

bool has_literal_credential(const std::string& value) {
  static const std::regex url_cred(R"(^[A-Za-z][A-Za-z0-9+.-]*://[^/:@$]+:[^/@$][^/@]*@)");
  static const std::regex kv_cred(R"((password|passwd|pwd)\s*=\s*[^$\s;][^\s;]*)", std::regex::icase);
  return std::regex_search(value, url_cred) || std::regex_search(value, kv_cred);
}

void require_no_literal_credential(const std::string& key, const std::string& value) {
  if (has_literal_credential(value))
    throw std::runtime_error("config key '" + key + "' contains a literal credential; use an env placeholder");
}

}  // namespace tooling
