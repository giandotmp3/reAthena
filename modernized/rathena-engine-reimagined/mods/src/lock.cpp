#include "mods/lock.hpp"
#include "mods/common.hpp"

namespace mods {
ModsLock parse_mods_lock(const std::string&) { throw NotImplemented("parse_mods_lock"); }
void authorize(const Manifest&, const ModsLock&, const SignatureVerifier&, const std::string&) {
  throw NotImplemented("authorize");
}
std::set<Permission> effective_grants(const Manifest&, const ModsLock&) {
  throw NotImplemented("effective_grants");
}
}  // namespace mods
