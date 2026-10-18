#include "legacy_npc/variables.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace legacy_npc {

std::string_view scope_name(Scope s) {
  switch (s) {
    case Scope::Character: return "char";
    case Scope::CharacterTemp: return "char_temp";
    case Scope::World: return "world";
    case Scope::WorldTemp: return "world_temp";
    case Scope::Account: return "account";
    case Scope::AccountGlobal: return "account_global";
    case Scope::Npc: return "npc";
    case Scope::CallFrame: return "frame";
    case Scope::Instance: return "instance";
  }
  return "char";
}

std::optional<VarRef> parse_var(std::string_view s) {
  VarRef r;
  auto lb = s.find('[');
  if (lb != std::string_view::npos) {
    if (s.back() != ']' || s.size() < lb + 3) return std::nullopt;
    long long n = 0;
    for (char c : s.substr(lb + 1, s.size() - lb - 2)) {
      if (c < '0' || c > '9') return std::nullopt;
      n = n * 10 + (c - '0');
      if (n > INT32_MAX) return std::nullopt;
    }
    r.index = static_cast<std::int32_t>(n);
    s = s.substr(0, lb);
  }
  if (s.empty() || s.size() > kMaxKeyLen) return std::nullopt;
  std::size_t plen = 0;
  auto starts = [&](std::string_view p) { return s.substr(0, p.size()) == p; };
  if (starts("$@")) { r.scope = Scope::WorldTemp; plen = 2; }
  else if (starts("$")) { r.scope = Scope::World; plen = 1; }
  else if (starts("##")) { r.scope = Scope::AccountGlobal; plen = 2; }
  else if (starts("#")) { r.scope = Scope::Account; plen = 1; }
  else if (starts("@")) { r.scope = Scope::CharacterTemp; plen = 1; }
  else if (starts(".@")) { r.scope = Scope::CallFrame; plen = 2; }
  else if (starts(".")) { r.scope = Scope::Npc; plen = 1; }
  else if (starts("'")) { r.scope = Scope::Instance; plen = 1; }
  auto body = s.substr(plen);
  if (!body.empty() && body.back() == '$') {
    r.is_string = true;
    body.remove_suffix(1);
  }
  if (body.empty()) return std::nullopt;
  for (char c : body)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return std::nullopt;
  r.name = std::string(body);
  return r;
}

std::string storage_key(const VarRef& v) {
  return std::string(kModId) + ":" + std::string(scope_name(v.scope)) + "/" + v.name +
         (v.is_string ? "$" : "");
}

void SparseArray::set(std::int32_t idx, const Value& v) {
  if (auto* i = std::get_if<std::int64_t>(&v)) {
    if (*i == 0) { items_.erase(idx); return; }
    items_[idx] = v;
  } else {
    auto s = std::get<std::string>(v);
    if (s.empty()) { items_.erase(idx); return; }
    if (s.size() > kMaxStringValueLen) s.resize(kMaxStringValueLen);
    items_[idx] = s;
  }
}

std::int64_t SparseArray::get_int(std::int32_t idx) const {
  auto it = items_.find(idx);
  if (it == items_.end()) return 0;
  if (auto* i = std::get_if<std::int64_t>(&it->second)) return *i;
  return 0;
}

std::string SparseArray::get_str(std::int32_t idx) const {
  auto it = items_.find(idx);
  if (it == items_.end()) return {};
  if (auto* s = std::get_if<std::string>(&it->second)) return *s;
  return {};
}

void InMemoryVariableStore::set(const VarRef& v, const Value& val) {
  arrays_[storage_key(v)].set(v.index.value_or(0), val);
}

Value InMemoryVariableStore::get(const VarRef& v) const {
  auto it = arrays_.find(storage_key(v));
  auto idx = v.index.value_or(0);
  if (v.is_string) return it == arrays_.end() ? std::string{} : it->second.get_str(idx);
  return it == arrays_.end() ? std::int64_t{0} : it->second.get_int(idx);
}

std::size_t InMemoryVariableStore::stored_count() const {
  std::size_t n = 0;
  for (auto& [k, a] : arrays_) n += a.stored_count();
  return n;
}

static std::string lower(std::string_view s) {
  std::string o(s);
  std::transform(o.begin(), o.end(), o.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return o;
}

bool is_param_variable(std::string_view name) {
  static const std::set<std::string> k = {
      "zeny", "baselevel", "joblevel", "baseexp", "jobexp", "hp", "maxhp", "sp", "maxsp",
      "statuspoint", "skillpoint", "karma", "manner", "weight", "maxweight", "str", "agi",
      "vit", "int", "dex", "luk", "sex", "class", "upper"};
  return k.count(lower(name)) != 0;
}

std::string param_attribute_id(std::string_view name) {
  return std::string(kModId) + ":param/" + lower(name);
}

std::int64_t ParamShim::read(std::string_view name) const {
  if (!is_param_variable(name)) throw std::invalid_argument("not a param variable");
  return backend_.get(param_attribute_id(name));
}

void ParamShim::write(std::string_view name, std::int64_t v) {
  if (!is_param_variable(name)) throw std::invalid_argument("not a param variable");
  backend_.set(param_attribute_id(name), v);
}

}  // namespace legacy_npc
