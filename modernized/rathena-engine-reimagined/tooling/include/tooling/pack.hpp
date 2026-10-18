#pragma once
// rathena-pack: client-side tables for mod-defined skills, items, status icons.
#include <string>
#include <vector>

#include "tooling/common.hpp"

namespace tooling::pack {

struct ClientDefinition {
  NamespacedId id;
  std::string kind;       // skill | item | status
  int client_id = 0;      // stable number from id_map
  std::string display_name;
  std::string icon;
};
struct PackOutput { std::string skillinfolist, item_info, status_icons; };

PackOutput build_client_tables(const std::vector<ClientDefinition>& defs);  // stub

// Tombstoned numbers are never reassigned (architecture section 6.2).
int allocate_client_id(const std::string& kind, const std::vector<int>& used,
                       const std::vector<int>& tombstoned);  // stub

}  // namespace tooling::pack
