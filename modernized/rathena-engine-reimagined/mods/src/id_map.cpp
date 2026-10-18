#include "mods/id_map.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "mods/common.hpp"
#include "mods/resolver.hpp"

namespace mods {

IdMap::IdMap(std::map<std::string, KindRange> ranges) : ranges_(std::move(ranges)) {
  for (const auto& [kind, r] : ranges_)
    if (r.first > r.last) throw std::invalid_argument("IdMap: empty range for kind '" + kind + "'");
}

void IdMap::put(IdMapRow row) {
  auto key = std::make_pair(row.kind, row.client_id);
  if (row.id) by_id_[*row.id] = key;
  rows_[key].push_back(std::move(row));
}

void IdMap::load(IdMapStore& store) {
  auto rows = store.load();
  std::map<std::pair<std::string, std::uint32_t>, std::vector<IdMapRow>> nr;
  std::map<std::string, std::pair<std::string, std::uint32_t>> ni;
  for (auto& r : rows) {
    auto key = std::make_pair(r.kind, r.client_id);
    auto& slot = nr[key];
    if (r.id) {
      auto parsed = parse_namespaced_id(*r.id);
      if (!parsed || parsed->kind != r.kind)
        throw LoadError("id_map.corrupt", "id_map row (" + r.kind + ", " + std::to_string(r.client_id) + ") has malformed id '" + *r.id + "'");
      if (ni.count(*r.id))
        throw LoadError("id_map.corrupt", "id_map maps '" + *r.id + "' to two numbers");
      for (const auto& o : slot)
        if (!o.id) throw LoadError("id_map.corrupt", "id_map number " + std::to_string(r.client_id) + " of kind '" + r.kind + "' is both tombstoned and live");
      ni[*r.id] = key;
    } else if (!slot.empty()) {
      throw LoadError("id_map.corrupt", "id_map holds number " + std::to_string(r.client_id) + " of kind '" + r.kind + "' twice (a tombstone must be the only row)");
    }
    slot.push_back(std::move(r));
  }
  rows_ = std::move(nr);
  by_id_ = std::move(ni);
  hint_.clear();
}

void IdMap::save(IdMapStore& store) const {
  std::vector<IdMapRow> rows;
  for (const auto& [k, v] : rows_) {
    (void)k;
    std::vector<IdMapRow> sorted = v;
    std::sort(sorted.begin(), sorted.end(), [](const IdMapRow& a, const IdMapRow& b) {
      return a.id.value_or("") < b.id.value_or("");
    });
    rows.insert(rows.end(), sorted.begin(), sorted.end());  // ordered by (kind, number, id)
  }
  store.save(rows);
}

void IdMap::claim(const NamespacedId& id, std::uint32_t client_id) {
  const std::string full = id.str();
  auto key = std::make_pair(id.kind, client_id);
  if (auto it = by_id_.find(full); it != by_id_.end()) {
    if (it->second == key) return;  // idempotent
    throw LoadError("id.client_id_changed",
                    "'" + full + "' already has client_id " + std::to_string(it->second.second) + "; mod '" + id.mod +
                        "' now declares " + std::to_string(client_id) +
                        " (numbers never change for a live id: tombstone the old one first)");
  }
  if (auto it = rows_.find(key); it != rows_.end()) {
    for (const auto& row : it->second) {
      if (!row.id)
        throw LoadError("id.collision", "client_id " + std::to_string(client_id) + " of kind '" + id.kind + "' requested by '" + full +
                                            "' (mod '" + id.mod + "') is tombstoned" +
                                            (row.was_id ? " (it belonged to '" + *row.was_id + "')" : std::string()) +
                                            " and is never reassigned");
      auto owner = parse_namespaced_id(*row.id);
      const std::string owner_mod = owner ? owner->mod : std::string("?");
      if (owner_mod != id.mod && exclusive_ && exclusive_(owner_mod, id.mod)) continue;  // never enabled together
      throw LoadError("id.collision", "client_id " + std::to_string(client_id) + " of kind '" + id.kind + "' is claimed by both mod '" +
                                          owner_mod + "' ('" + *row.id + "') and mod '" + id.mod + "' ('" + full + "')");
    }
  }
  put({id.kind, client_id, full, std::nullopt});
}

std::uint32_t IdMap::allocate(const NamespacedId& id) {
  const std::string full = id.str();
  if (auto it = by_id_.find(full); it != by_id_.end()) return it->second.second;
  auto rit = ranges_.find(id.kind);
  if (rit == ranges_.end())
    throw LoadError("id.no_range", "no reserved client_id range for kind '" + id.kind + "' (needed by '" + full + "'; declare client_id explicitly or add a range)");
  const KindRange r = rit->second;
  // lowest free number: scan the used numbers of this kind inside the range
  std::uint64_t cand = std::max<std::uint64_t>(r.first, hint_[id.kind]);
  auto it = rows_.lower_bound({id.kind, static_cast<std::uint32_t>(cand)});
  for (; it != rows_.end() && it->first.first == id.kind && it->first.second <= r.last; ++it) {
    if (it->first.second == cand) ++cand;
    else if (it->first.second > cand) break;
  }
  if (cand > r.last)
    throw LoadError("id.exhausted", "reserved range " + std::to_string(r.first) + ".." + std::to_string(r.last) + " of kind '" + id.kind +
                                        "' is full; cannot allocate '" + full + "' (mod '" + id.mod + "')");
  put({id.kind, static_cast<std::uint32_t>(cand), full, std::nullopt});
  hint_[id.kind] = cand + 1;
  return static_cast<std::uint32_t>(cand);
}

void IdMap::tombstone_mod(const std::string& mod_id) {
  for (auto& [key, vec] : rows_) {
    (void)key;
    std::vector<IdMapRow> keep;
    std::optional<std::string> freed;
    for (const auto& row : vec) {
      auto p = row.id ? parse_namespaced_id(*row.id) : std::nullopt;
      if (p && p->mod == mod_id) {
        by_id_.erase(*row.id);
        freed = row.id;
      } else {
        keep.push_back(row);
      }
    }
    if (!freed) continue;
    if (keep.empty()) keep.push_back({key.first, key.second, std::nullopt, freed});  // nobody else owns it: tombstone
    vec = std::move(keep);
  }
}

std::optional<std::uint32_t> IdMap::client_id_of(const NamespacedId& id) const {
  auto it = by_id_.find(id.str());
  if (it == by_id_.end()) return std::nullopt;
  return it->second.second;
}

std::optional<NamespacedId> IdMap::id_of(const std::string& kind, std::uint32_t client_id) const {
  auto it = rows_.find({kind, client_id});
  if (it == rows_.end() || it->second.size() != 1 || !it->second[0].id) return std::nullopt;
  return parse_namespaced_id(*it->second[0].id);
}

std::optional<NamespacedId> IdMap::id_of(const std::string& kind, std::uint32_t client_id,
                                         const std::set<std::string>& active_mods) const {
  auto it = rows_.find({kind, client_id});
  if (it == rows_.end()) return std::nullopt;
  for (const auto& row : it->second) {
    if (!row.id) continue;
    auto p = parse_namespaced_id(*row.id);
    if (p && active_mods.count(p->mod)) return p;
  }
  return std::nullopt;
}

bool IdMap::is_tombstoned(const std::string& kind, std::uint32_t client_id) const {
  auto it = rows_.find({kind, client_id});
  return it != rows_.end() && it->second.size() == 1 && !it->second[0].id;
}

std::vector<NamespacedId> IdMap::live_ids_of(const std::string& mod_id) const {
  std::vector<NamespacedId> out;
  for (const auto& [full, k] : by_id_) {
    (void)k;
    auto p = parse_namespaced_id(full);
    if (p && p->mod == mod_id) out.push_back(*p);
  }
  return out;
}

std::set<std::string> IdMap::mods() const {
  std::set<std::string> out;
  for (const auto& [full, k] : by_id_) {
    (void)k;
    if (auto p = parse_namespaced_id(full)) out.insert(p->mod);
  }
  return out;
}

std::size_t IdMap::tombstone_count() const {
  std::size_t n = 0;
  for (const auto& [k, v] : rows_) { (void)k; if (v.size() == 1 && !v[0].id) ++n; }
  return n;
}

// ---- RangeRegistry ------------------------------------------------------------
namespace {
[[noreturn]] void rbad(const char* code, const std::string& source, const toml::Value& at, const std::string& key, const std::string& why) {
  std::string w = source.empty() ? std::string("id_ranges.toml") : source;
  if (at.line) w += ":" + std::to_string(at.line) + ":" + std::to_string(at.col);
  throw LoadError(code, "range registry (" + w + "): key '" + key + "': " + why);
}
std::uint32_t u32(const toml::Value& v, const std::string& source, const std::string& key) {
  if (!v.is_int()) rbad("range.bad_type", source, v, key, std::string("expected integer, found ") + v.type_name());
  if (v.i < 0 || v.i > 0xFFFFFFFFll) rbad("range.bad_value", source, v, key, "must be between 0 and 4294967295");
  return static_cast<std::uint32_t>(v.i);
}
}  // namespace

RangeRegistry RangeRegistry::parse(const std::string& text, const std::string& source) {
  toml::Value root;
  try {
    root = toml::parse(text, source.empty() ? std::string("id_ranges.toml") : source);
  } catch (const toml::ParseError& e) {
    throw LoadError("range.syntax", std::string("range registry ") + e.what());
  }
  RangeRegistry reg;
  for (const auto& kv : root.t) {
    if (kv.first == "allocation") {
      if (!kv.second.is_table()) rbad("range.bad_type", source, kv.second, "allocation", "expected table");
      for (const auto& a : kv.second.t) {
        const std::string key = "allocation." + a.first;
        if (!a.second.is_array() || a.second.a.size() != 2) rbad("range.bad_type", source, a.second, key, "expected [first, last]");
        KindRange r{u32(a.second.a[0], source, key + "[0]"), u32(a.second.a[1], source, key + "[1]")};
        if (r.first > r.last) rbad("range.bad_value", source, a.second, key, "first is greater than last");
        reg.allocation_[a.first] = r;
      }
    } else if (kv.first == "range") {
      if (!kv.second.is_array()) rbad("range.bad_type", source, kv.second, "range", "expected an array of tables ([[range]])");
      for (std::size_t k = 0; k < kv.second.a.size(); ++k) {
        const auto& t = kv.second.a[k];
        const std::string base = "range[" + std::to_string(k) + "]";
        OwnedRange o{};
        bool has_owner = false, has_kind = false, has_first = false, has_last = false;
        for (const auto& f : t.t) {
          const std::string key = base + "." + f.first;
          if (f.first == "owner" || f.first == "kind") {
            if (!f.second.is_string()) rbad("range.bad_type", source, f.second, key, "expected string");
            (f.first == "owner" ? o.owner : o.kind) = f.second.s;
            (f.first == "owner" ? has_owner : has_kind) = true;
          } else if (f.first == "first") { o.first = u32(f.second, source, key); has_first = true; }
          else if (f.first == "last") { o.last = u32(f.second, source, key); has_last = true; }
          else rbad("range.unknown_key", source, f.second, key, "unknown key (known: owner, kind, first, last)");
        }
        if (!(has_owner && has_kind && has_first && has_last)) rbad("range.missing_key", source, t, base, "needs owner, kind, first and last");
        if (!is_valid_mod_id(o.owner)) rbad("range.bad_value", source, t, base + ".owner", "not a valid mod id");
        if (o.first > o.last) rbad("range.bad_value", source, t, base, "first is greater than last");
        reg.owned_.push_back(o);
      }
    } else {
      rbad("range.unknown_key", source, kv.second, kv.first, "unknown key (known: allocation, range)");
    }
  }
  return reg;
}

RangeRegistry RangeRegistry::load_file(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw LoadError("range.io", "cannot read " + file.string());
  std::ostringstream o;
  o << in.rdbuf();
  return parse(o.str(), file.string());
}

void RangeRegistry::validate(const std::vector<Manifest>& known) const {
  auto manifest_of = [&](const std::string& id) -> const Manifest* {
    for (const auto& m : known) if (m.id == id) return &m;
    return nullptr;
  };
  auto overlap = [](std::uint32_t a1, std::uint32_t a2, std::uint32_t b1, std::uint32_t b2) {
    return std::max(a1, b1) <= std::min(a2, b2);
  };
  for (std::size_t i = 0; i < owned_.size(); ++i)
    for (std::size_t j = i + 1; j < owned_.size(); ++j) {
      const auto& a = owned_[i];
      const auto& b = owned_[j];
      if (a.kind != b.kind || !overlap(a.first, a.last, b.first, b.last)) continue;
      if (a.owner == b.owner)
        throw LoadError("range.overlap", "mod '" + a.owner + "' declares overlapping ranges for kind '" + a.kind + "'");
      const Manifest* ma = manifest_of(a.owner);
      const Manifest* mb = manifest_of(b.owner);
      if (ma && mb && mutually_exclusive(*ma, *mb)) continue;
      throw LoadError("range.overlap", "client_id ranges of kind '" + a.kind + "' overlap: mod '" + a.owner + "' (" + std::to_string(a.first) +
                                           ".." + std::to_string(a.last) + ") and mod '" + b.owner + "' (" + std::to_string(b.first) + ".." +
                                           std::to_string(b.last) + ") and the two mods can be enabled together");
    }
  for (const auto& [kind, r] : allocation_)
    for (const auto& o : owned_)
      if (o.kind == kind && overlap(r.first, r.last, o.first, o.last))
        throw LoadError("range.overlap", "the allocation range " + std::to_string(r.first) + ".." + std::to_string(r.last) + " of kind '" + kind +
                                             "' overlaps the explicit range " + std::to_string(o.first) + ".." + std::to_string(o.last) +
                                             " owned by mod '" + o.owner + "'");
}

void RangeRegistry::check_claim(const NamespacedId& id, std::uint32_t client_id) const {
  bool any = false;
  for (const auto& o : owned_) {
    if (o.owner != id.mod || o.kind != id.kind) continue;
    any = true;
    if (client_id >= o.first && client_id <= o.last) return;
  }
  throw LoadError("range.outside_owner", "mod '" + id.mod + "' declares client_id " + std::to_string(client_id) + " for '" + id.str() + "' " +
                                             (any ? "outside the ranges it owns for kind '" + id.kind + "'"
                                                  : "but owns no explicit range for kind '" + id.kind + "' in the range registry"));
}

}  // namespace mods
