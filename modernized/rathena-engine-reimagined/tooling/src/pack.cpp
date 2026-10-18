#include "tooling/pack.hpp"
namespace tooling::pack {
PackOutput build_client_tables(const std::vector<ClientDefinition>&) { throw NotImplemented("client table emitter"); }
int allocate_client_id(const std::string&, const std::vector<int>&, const std::vector<int>&) { throw NotImplemented("client id allocation"); }
}  // namespace tooling::pack
