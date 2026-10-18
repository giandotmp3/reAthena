#pragma once
// Persistent id_map: namespaced id <-> client number, with tombstones
// (architecture 6.2). Persistence of the table itself is delegated to the
// persistence service through IdMapStore; this unit owns only the rules.
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mods/ids.hpp"

namespace mods {

struct IdMapRow {
  std::string kind;
  std::uint32_t client_id;
  std::optional<std::string> id;  // nullopt => tombstoned forever
};

class IdMapStore {
public:
  virtual ~IdMapStore() = default;
  virtual std::vector<IdMapRow> load() = 0;
  virtual void save(const std::vector<IdMapRow>& rows) = 0;
};

struct KindRange { std::uint32_t first, last; };  // reserved allocation range

class IdMap {
public:
  explicit IdMap(std::map<std::string, KindRange> ranges);

  void load(IdMapStore& store);
  void save(IdMapStore& store) const;

  // Explicit client_id (migrated legacy content). Throws
  // LoadError{"id.collision"} naming both owners if taken (live or tombstoned).
  void claim(const NamespacedId& id, std::uint32_t client_id);
  // Stable allocation from the kind's reserved range; idempotent per id.
  // Throws LoadError{"id.exhausted"} when the range is full.
  std::uint32_t allocate(const NamespacedId& id);
  // Mod removal: numbers become tombstones, never reassigned.
  void tombstone_mod(const std::string& mod_id);

  std::optional<std::uint32_t> client_id_of(const NamespacedId& id) const;
  std::optional<NamespacedId> id_of(const std::string& kind, std::uint32_t client_id) const;
  bool is_tombstoned(const std::string& kind, std::uint32_t client_id) const;

private:
  std::map<std::string, KindRange> ranges_;
  std::vector<IdMapRow> rows_;
};

}  // namespace mods
