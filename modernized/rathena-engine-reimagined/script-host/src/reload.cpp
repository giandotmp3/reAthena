#include "script_host/reload.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

ReloadResult ReloadManager::reload(const std::string&, const std::string&) { not_implemented("ReloadManager::reload"); }
std::uint64_t ReloadManager::active_generation(const std::string&) const { not_implemented("ReloadManager::active_generation"); }
std::size_t ReloadManager::live_registrations(const std::string&, std::uint64_t) const { not_implemented("ReloadManager::live_registrations"); }
void ReloadManager::advance_game_time(std::uint64_t) { not_implemented("ReloadManager::advance_game_time"); }

}  // namespace script_host
