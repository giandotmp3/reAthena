#include "tooling/codegen.hpp"
#include "tooling/common.hpp"
namespace tooling::codegen {
std::vector<GeneratedFile> generate_codecs(const std::string&) { throw NotImplemented("codec generator"); }
std::vector<GeneratedFile> generate_api(const std::string&) { throw NotImplemented("api generator"); }
bool check_up_to_date(const std::vector<GeneratedFile>&, const std::string&) { throw NotImplemented("generated freshness check"); }
}  // namespace tooling::codegen
