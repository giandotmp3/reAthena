#pragma once
// Namespaced id registry and persistent id_map (architecture 6.2).
//
// Save data stores the namespaced string id. The client-visible number exists
// only on the wire and in id_map. Rules enforced here:
//   * a definition may declare client_id explicitly (migrated legacy content);
//   * others get a number from the kind's reserved range, stable across
//     restarts (idempotent per id) and the lowest free one;
//   * a number freed by mod removal is TOMBSTONED forever, never reassigned,
//     never claimable;
//   * a collision is a load error naming both mods. The one exception: mods
//     declared mutually exclusive (see set_exclusivity) may keep the same
//     explicit number, because they are never enabled together. This is how
//     ruleset.renewal and ruleset.prerenewal both keep the legacy numbers;
//     the wire lookup then needs the set of active mods.
// Persistence of the table is delegated to the persistence service through
// IdMapStore (one method pair; no dependency on that unit's code).
//
// Row schema for the store, key (kind, client_id, id):
//   kind TEXT, client_id INTEGER, id TEXT NULL (NULL = tombstone), was_id TEXT NULL
// `was_id` is the id a tombstoned number belonged to (diagnostics only).
// A tombstone is the only row for its (kind, client_id). Several live rows may
// share a (kind, client_id) only when their mods are mutually exclusive.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mods/ids.hpp"
#include "mods/manifest.hpp"

namespace mods {

struct IdMapRow {
  std::string kind;
  std::uint32_t client_id;
  std::optional<std::string> id;      // nullopt => tombstoned forever
  std::optional<std::string> was_id;  // former owner of a tombstoned number
};

class IdMapStore {
public:
  virtual ~IdMapStore() = default;
  virtual std::vector<IdMapRow> load() = 0;
  // Replaces the stored table with `rows` (the unit writes it atomically).
  virtual void save(const std::vector<IdMapRow>& rows) = 0;
};

// In-memory store (tests, tools, and the default until persistence lands).
class MemoryIdMapStore final : public IdMapStore {
public:
  std::vector<IdMapRow> load() override { return rows_; }
  void save(const std::vector<IdMapRow>& rows) override { rows_ = rows; ++saves_; }
  unsigned saves() const { return saves_; }
private:
  std::vector<IdMapRow> rows_;
  unsigned saves_ = 0;
};

struct KindRange { std::uint32_t first, last; };  // reserved allocation range (inclusive)

class IdMap {
public:
  explicit IdMap(std::map<std::string, KindRange> ranges = {});

  // Predicate "these two mods can never be enabled together". Used only by
  // claim(); not persisted; pass an empty function to restore strict mode.
  using Exclusivity = std::function<bool(const std::string&, const std::string&)>;
  void set_exclusivity(Exclusivity e) { exclusive_ = std::move(e); }

  // Replaces the table with the stored one. Throws LoadError{"id_map.corrupt"}
  // on duplicate numbers or duplicate live ids.
  void load(IdMapStore& store);
  void save(IdMapStore& store) const;

  // Explicit client_id (migrated legacy content). Idempotent for the same
  // (id, number). Throws LoadError{"id.collision"} naming both owners if the
  // number is taken (live or tombstoned), {"id.client_id_changed"} if the id
  // already has a different number.
  void claim(const NamespacedId& id, std::uint32_t client_id);
  // Stable allocation from the kind's reserved range; idempotent per id.
  // Throws LoadError{"id.exhausted"} when the range is full, {"id.no_range"}
  // when the kind has no reserved range.
  std::uint32_t allocate(const NamespacedId& id);
  // Mod removal: numbers become tombstones, never reassigned.
  void tombstone_mod(const std::string& mod_id);

  std::optional<std::uint32_t> client_id_of(const NamespacedId& id) const;
  // The live id that owns the number; nullopt for tombstones, unknown numbers
  // and numbers shared by exclusive mods (use the overload below).
  std::optional<NamespacedId> id_of(const std::string& kind, std::uint32_t client_id) const;
  // Wire lookup: the owner among the currently active mods.
  std::optional<NamespacedId> id_of(const std::string& kind, std::uint32_t client_id,
                                    const std::set<std::string>& active_mods) const;
  bool is_tombstoned(const std::string& kind, std::uint32_t client_id) const;

  std::vector<NamespacedId> live_ids_of(const std::string& mod_id) const;
  std::set<std::string> mods() const;  // mods owning at least one live id
  std::size_t live_count() const { return by_id_.size(); }
  std::size_t tombstone_count() const;
  const std::map<std::string, KindRange>& ranges() const { return ranges_; }

private:
  void put(IdMapRow row);
  std::map<std::string, KindRange> ranges_;
  Exclusivity exclusive_;
  std::map<std::pair<std::string, std::uint32_t>, std::vector<IdMapRow>> rows_;  // (kind, number)
  std::map<std::string, std::pair<std::string, std::uint32_t>> by_id_;  // full id -> (kind, number)
  // Numbers below the hint are all in use (numbers are never freed), so the
  // lowest-free scan resumes here: allocating n ids costs O(n log n).
  std::map<std::string, std::uint64_t> hint_;
};

// ---- CI range registry -------------------------------------------------------
// Declares which explicit client_id ranges each mod owns, and the reserved
// allocation range per kind. The check fails when two owners overlap (unless
// the two mods are mutually exclusive, e.g. ruleset.renewal and
// ruleset.prerenewal both keep the legacy numbers), when an allocation range
// overlaps an owned range, or when a claim falls outside its owner's ranges.
//
// File format (TOML):
//   [allocation]
//   skill = [10000, 19999]
//   [[range]]
//   owner = "ruleset.prerenewal"
//   kind = "skill"
//   first = 1
//   last = 4999
struct OwnedRange { std::string owner, kind; std::uint32_t first, last; };

class RangeRegistry {
public:
  static RangeRegistry parse(const std::string& toml_text, const std::string& source = {});
  static RangeRegistry load_file(const std::filesystem::path& file);

  const std::vector<OwnedRange>& owned() const { return owned_; }
  const std::map<std::string, KindRange>& allocation() const { return allocation_; }
  void add_owned(OwnedRange r) { owned_.push_back(std::move(r)); }
  void set_allocation(const std::string& kind, KindRange r) { allocation_[kind] = r; }

  // `known` supplies the manifests used to decide mutual exclusion. Throws
  // LoadError{"range.overlap"} naming both owners and the numbers.
  void validate(const std::vector<Manifest>& known = {}) const;
  // Throws LoadError{"range.outside_owner"} (names the mod and id) when `id`
  // is not covered by a range its mod owns for that kind.
  void check_claim(const NamespacedId& id, std::uint32_t client_id) const;

private:
  std::vector<OwnedRange> owned_;
  std::map<std::string, KindRange> allocation_;
};

}  // namespace mods
