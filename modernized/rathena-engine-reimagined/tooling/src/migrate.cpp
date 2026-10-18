#include "tooling/migrate.hpp"

namespace tooling::migrate {

namespace {
const Value* index_of(const Value& v) {
  const Map* m = v.map();
  if (!m) return nullptr;
  auto it = m->find("Index");
  return it == m->end() ? nullptr : &it->second;
}
bool is_indexed_list(const List& l) {
  if (l.empty()) return false;
  for (const auto& e : l) if (!index_of(e)) return false;
  return true;
}
}  // namespace

Value merge_fields(const Value& base, const Value& overlay) {
  if (const Map* bm = base.map()) {
    if (const Map* om = overlay.map()) {
      Map out = *bm;
      for (const auto& [k, v] : *om) {
        auto it = out.find(k);
        out[k] = it == out.end() ? v : merge_fields(it->second, v);
      }
      return Value(std::move(out));
    }
  }
  if (const List* bl = base.list()) {
    if (const List* ol = overlay.list()) {
      if (is_indexed_list(*bl) && is_indexed_list(*ol)) {
        List out = *bl;
        for (const auto& e : *ol) {
          bool merged = false;
          for (auto& existing : out) {
            if (*index_of(existing) == *index_of(e)) {
              existing = merge_fields(existing, e);
              merged = true;
              break;
            }
          }
          if (!merged) out.push_back(e);
        }
        return Value(std::move(out));
      }
    }
  }
  return overlay;
}

Map merge_records(const std::vector<List>& files, const std::string& key_field) {
  Map out;
  for (const auto& file : files) {
    for (const auto& rec : file) {
      const Map* m = rec.map();
      if (!m) continue;
      auto k = m->find(key_field);
      if (k == m->end()) continue;
      const auto* s = std::get_if<std::string>(&k->second.v);
      std::string key = s ? *s : std::to_string(std::get<int64_t>(k->second.v));
      auto it = out.find(key);
      out[key] = it == out.end() ? rec : merge_fields(it->second, rec);
    }
  }
  return out;
}

Value merge_mode(const Value&, const Value&) { throw NotImplemented("Mode import merge"); }
std::vector<List> load_db_chain(const std::string&, const std::string&, DbSet) { throw NotImplemented("YAML db loader"); }
std::string emit_luau_module(const std::string&, const std::string&, const Map&) { throw NotImplemented("Luau emitter"); }
DiffReport diff_rulesets(const Map&, const Map&) { throw NotImplemented("shared-by-diff report"); }
EmbeddedScriptRoute route_embedded_script(const std::string&, const std::string&) { throw NotImplemented("embedded script routing"); }

}  // namespace tooling::migrate
