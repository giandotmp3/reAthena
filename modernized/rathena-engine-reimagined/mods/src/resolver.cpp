#include "mods/resolver.hpp"
#include "mods/common.hpp"

namespace mods {
std::vector<Manifest> resolve_load_order(const std::vector<Manifest>&, const ResolveOptions&) {
  throw NotImplemented("resolve_load_order");
}
}  // namespace mods
