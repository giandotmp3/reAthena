#include "mods/id_map.hpp"

#include "mods/common.hpp"

namespace mods {
IdMap::IdMap(std::map<std::string, KindRange> ranges) : ranges_(std::move(ranges)) {}
void IdMap::load(IdMapStore&) { throw NotImplemented("IdMap::load"); }
void IdMap::save(IdMapStore&) const { throw NotImplemented("IdMap::save"); }
void IdMap::claim(const NamespacedId&, std::uint32_t) { throw NotImplemented("IdMap::claim"); }
std::uint32_t IdMap::allocate(const NamespacedId&) { throw NotImplemented("IdMap::allocate"); }
void IdMap::tombstone_mod(const std::string&) { throw NotImplemented("IdMap::tombstone_mod"); }
std::optional<std::uint32_t> IdMap::client_id_of(const NamespacedId&) const {
  throw NotImplemented("IdMap::client_id_of");
}
std::optional<NamespacedId> IdMap::id_of(const std::string&, std::uint32_t) const {
  throw NotImplemented("IdMap::id_of");
}
bool IdMap::is_tombstoned(const std::string&, std::uint32_t) const {
  throw NotImplemented("IdMap::is_tombstoned");
}
}  // namespace mods
