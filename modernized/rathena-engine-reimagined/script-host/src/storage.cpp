#include "script_host/storage.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

std::optional<Value> StorageApi::get(Scope, const std::string&, const std::string&, const std::string&, const std::string&) { not_implemented("StorageApi::get"); }
void StorageApi::set(Scope, const std::string&, const std::string&, const std::string&, Value) { not_implemented("StorageApi::set"); }
void StorageApi::set_quota(const std::string&, std::uint64_t) { not_implemented("StorageApi::set_quota"); }
std::uint64_t StorageApi::usage(const std::string&) const { not_implemented("StorageApi::usage"); }
void StorageApi::declare_migrate(const std::string&, std::uint32_t, MigrateFn) { not_implemented("StorageApi::declare_migrate"); }
void StorageApi::check_loadable(const std::string&, std::uint32_t) const { not_implemented("StorageApi::check_loadable"); }
void StorageApi::grant_read(const std::string&, const std::string&) { not_implemented("StorageApi::grant_read"); }

}  // namespace script_host
