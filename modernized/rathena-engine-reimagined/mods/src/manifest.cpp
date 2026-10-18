#include "mods/manifest.hpp"
#include "mods/common.hpp"

namespace mods {

std::string Permission::str() const {
  switch (kind) {
    case PermissionKind::StorageAccount: return "storage.account";
    case PermissionKind::StorageRead: return "storage.read:" + arg;
    case PermissionKind::HttpRoute: return "http.route";
    case PermissionKind::UnboundedLoop: return "unbounded_loop";
    case PermissionKind::AtCommand: return "atcommand";
    case PermissionKind::NetRaw: return "net.raw";
  }
  return {};
}

std::optional<Permission> parse_permission(const std::string& s) {
  static const std::pair<const char*, PermissionKind> plain[] = {
      {"storage.account", PermissionKind::StorageAccount},
      {"http.route", PermissionKind::HttpRoute},
      {"unbounded_loop", PermissionKind::UnboundedLoop},
      {"atcommand", PermissionKind::AtCommand},
      {"net.raw", PermissionKind::NetRaw},
  };
  for (auto& [name, kind] : plain)
    if (s == name) return Permission{kind, {}};
  const std::string prefix = "storage.read:";
  if (s.rfind(prefix, 0) == 0) {
    std::string other = s.substr(prefix.size());
    // validated lazily by callers via is_valid_mod_id; reject empty here
    if (!other.empty()) return Permission{PermissionKind::StorageRead, other};
  }
  return std::nullopt;
}

Manifest parse_manifest(const std::string&) {
  throw NotImplemented("parse_manifest (TOML parsing + validation)");
}

}  // namespace mods
