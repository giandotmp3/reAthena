#include "mods/loader.hpp"
#include "mods/common.hpp"

namespace mods {
ModLoader::ModLoader(IdMap& ids, ModObserver* observer) : ids_(ids), observer_(observer) {}
void ModLoader::load(const LoadRequest&) { throw NotImplemented("ModLoader::load"); }
void ModLoader::disable(const std::string&) { throw NotImplemented("ModLoader::disable"); }
void ModLoader::enable(const std::string&) { throw NotImplemented("ModLoader::enable"); }
ModState ModLoader::state(const std::string&) const { throw NotImplemented("ModLoader::state"); }
std::vector<std::string> ModLoader::load_order() const {
  throw NotImplemented("ModLoader::load_order");
}
}  // namespace mods
