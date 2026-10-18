// load_catalog_yaml: proto/*.yaml -> PacketCatalog (CAP-E02). The YAML reader is netproto/yaml.hpp.
// Unknown keys are errors: a typo in a spec file must not silently turn into a default.
#include <fstream>
#include <set>
#include <sstream>

#include "netproto/spec.hpp"
#include "netproto/yaml.hpp"

namespace netproto {

namespace {

using yaml::Node;

[[noreturn]] void bad(const Node& n, const std::string& what) {
  throw SpecError("yaml line " + std::to_string(n.line) + ": " + what);
}

void only_keys(const Node& n, std::initializer_list<const char*> allowed, const char* where) {
  if (n.kind != Node::Kind::Map) bad(n, std::string(where) + " must be a mapping");
  std::set<std::string> ok(allowed.begin(), allowed.end());
  for (const auto& k : n.keys)
    if (!ok.count(k)) bad(n, std::string("unknown key '") + k + "' in " + where);
}

std::int64_t get_int(const Node& n, const char* key, std::int64_t lo, std::int64_t hi) {
  const Node& v = n.at(key);
  if (v.kind != Node::Kind::Int) bad(v, std::string("'") + key + "' must be an integer");
  if (v.i < lo || v.i > hi) bad(v, std::string("'") + key + "' out of range");
  return v.i;
}
std::int64_t opt_int(const Node& n, const char* key, std::int64_t dflt, std::int64_t lo, std::int64_t hi) {
  return n.find(key) ? get_int(n, key, lo, hi) : dflt;
}
std::string get_str(const Node& n, const char* key) {
  const Node& v = n.at(key);
  if (v.kind != Node::Kind::Str) bad(v, std::string("'") + key + "' must be a string");
  return v.s;
}
std::string opt_str(const Node& n, const char* key, const std::string& dflt) {
  const Node* v = n.find(key);
  if (!v || v->is_null()) return dflt;
  if (v->kind != Node::Kind::Str) bad(*v, std::string("'") + key + "' must be a string");
  return v->s;
}

VersionPredicate predicate(const Node& n, const char* key) {
  const std::string s = opt_str(n, key, "");
  auto p = VersionPredicate::parse(s);
  if (!p) bad(n, "bad version predicate '" + s + "'");
  return *p;
}

FieldType field_type(const Node& v, const std::string& t) {
  static const std::pair<const char*, FieldType> kTypes[] = {
      {"u8", FieldType::U8},   {"u16", FieldType::U16}, {"u32", FieldType::U32}, {"u64", FieldType::U64},
      {"i8", FieldType::I8},   {"i16", FieldType::I16}, {"i32", FieldType::I32}, {"i64", FieldType::I64},
      {"f32", FieldType::F32}, {"string", FieldType::FixedString}, {"raw", FieldType::Raw},
      {"pos3", FieldType::Pos3}, {"pos6", FieldType::Pos6}};
  for (const auto& [name, ft] : kTypes)
    if (t == name) return ft;
  bad(v, "unknown field type '" + t + "'");
}

FieldSpec field(const Node& n) {
  only_keys(n, {"name", "offset", "type", "width", "array_len", "row", "bit_shift", "bit_width", "present_when"}, "field");
  FieldSpec f;
  f.name = get_str(n, "name");
  f.offset = static_cast<std::uint16_t>(get_int(n, "offset", 0, 0xFFFF));
  f.type = field_type(n.at("type"), get_str(n, "type"));
  f.width = static_cast<std::uint16_t>(opt_int(n, "width", 0, 0, 0xFFFF));
  f.array_len = static_cast<std::uint16_t>(opt_int(n, "array_len", 1, 1, 0xFFFF));
  if (const Node* r = n.find("row")) {
    if (r->kind != Node::Kind::Bool) bad(*r, "'row' must be true or false");
    f.row = r->b;
  }
  f.bit_shift = static_cast<std::uint8_t>(opt_int(n, "bit_shift", 0, 0, 63));
  f.bit_width = static_cast<std::uint8_t>(opt_int(n, "bit_width", 0, 0, 64));
  f.present_when = predicate(n, "present_when");
  return f;
}

PacketVersion version(const Node& n) {
  only_keys(n, {"applies", "opcode", "length", "handler_offsets", "fields", "source", "also_accepted_by_legacy", "also_named_by_legacy",
             "legacy_db_length"}, "version");
  PacketVersion v;
  v.applies = predicate(n, "applies");
  v.opcode = static_cast<std::uint16_t>(get_int(n, "opcode", 0, 0xFFFF));
  v.source = opt_str(n, "source", "");
  const Node& len = n.at("length");
  only_keys(len, {"fixed", "variable"}, "length");
  if ((len.find("fixed") != nullptr) == (len.find("variable") != nullptr)) bad(len, "length must be fixed xor variable");
  if (len.find("fixed")) {
    v.length.kind = LengthKind::Fixed;
    v.length.fixed_size = static_cast<std::uint16_t>(get_int(len, "fixed", 2, 0xFFFF));
  } else {
    const Node& var = len.at("variable");
    only_keys(var, {"prefix_offset", "header", "repeat"}, "length.variable");
    v.length.kind = LengthKind::Variable;
    v.length.prefix_offset = static_cast<std::uint16_t>(opt_int(var, "prefix_offset", 2, 0, 0xFFFD));
    v.length.header_size = static_cast<std::uint16_t>(opt_int(var, "header", 4, 4, 0xFFFF));
    if (const Node* r = var.find("repeat")) {
      only_keys(*r, {"offset", "stride"}, "repeat");
      v.length.repeat = RepeatGroup{static_cast<std::uint16_t>(get_int(*r, "offset", 0, 0xFFFF)),
                                    static_cast<std::uint16_t>(get_int(*r, "stride", 1, 0xFFFF))};
    }
  }
  if (const Node* ho = n.find("handler_offsets")) {
    if (ho->kind != Node::Kind::Seq) bad(*ho, "'handler_offsets' must be a list");
    for (const Node& o : ho->seq) {
      if (o.kind != Node::Kind::Int || o.i < 0 || o.i > 0xFFFF) bad(o, "handler offset must be an integer");
      v.handler_offsets.push_back(static_cast<std::uint16_t>(o.i));
    }
  }
  // verified legacy data that the codec does not use: aliases it deliberately does not accept, other struct
  // names of the same opcode, the packet DB length when it disagrees with the struct
  for (const char* key : {"also_accepted_by_legacy", "also_named_by_legacy"})
    if (const Node* al = n.find(key); al && al->kind != Node::Kind::Seq) bad(*al, std::string("'") + key + "' must be a list");
  const Node* fl = n.find("fields");
  if (!fl || (fl->kind == Node::Kind::Str && fl->s == "unknown")) {
    v.fields_known = false;
  } else if (fl->kind == Node::Kind::Seq) {
    for (const Node& f : fl->seq) v.fields.push_back(field(f));
  } else {
    bad(*fl, "'fields' must be a list or the word 'unknown'");
  }
  return v;
}

PacketDef packet(const Node& n) {
  only_keys(n, {"name", "direction", "category", "gate", "legacy_handler", "struct", "versions"}, "packet");
  PacketDef d;
  d.name = get_str(n, "name");
  const std::string dir = get_str(n, "direction");
  if (dir == "cz") d.direction = Direction::ClientToServer;
  else if (dir == "zc") d.direction = Direction::ServerToClient;
  else if (dir == "inter") d.direction = Direction::Inter;
  else if (dir == "any") d.direction = Direction::Any;
  else bad(n.at("direction"), "bad direction '" + dir + "'");
  d.category = opt_str(n, "category", "");
  const std::string gate = opt_str(n, "gate", "in_world");
  if (gate == "pre_session") d.gate = Phase::PreSession;
  else if (gate == "pre_map_load") d.gate = Phase::PreMapLoad;
  else if (gate == "in_world") d.gate = Phase::InWorld;
  else bad(n.at("gate"), "bad gate '" + gate + "'");
  const Node& vs = n.at("versions");
  if (vs.kind != Node::Kind::Seq || vs.seq.empty()) bad(vs, "'versions' must be a non-empty list");
  for (const Node& v : vs.seq) d.versions.push_back(version(v));
  return d;
}

std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw SpecError("cannot open " + path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

}  // namespace

PacketCatalog load_catalog_yaml_text(const std::string& text) {
  const Node root = yaml::parse(text);
  if (root.kind != Node::Kind::Map) throw SpecError("yaml: the document must be a mapping");
  if (get_int(root, "schema", 0, 1 << 20) != 1) throw SpecError("unsupported schema version");
  const Node& pk = root.at("packets");
  if (pk.kind != Node::Kind::Seq) bad(pk, "'packets' must be a list");
  PacketCatalog c;
  std::set<std::string> names;
  for (const Node& p : pk.seq) {
    PacketDef d = packet(p);
    if (!names.insert(d.name).second) bad(p, "duplicate packet '" + d.name + "'");
    c.add(std::move(d));
  }
  return c;
}

PacketCatalog load_catalog_yaml(const std::string& path) { return load_catalog_yaml_text(slurp(path)); }

std::vector<ClientVersionInfo> load_client_versions_yaml_text(const std::string& text) {
  const Node root = yaml::parse(text);
  std::vector<ClientVersionInfo> out;
  const Node* cv = root.find("client_versions");
  if (!cv) return out;
  if (cv->kind != Node::Kind::Seq) bad(*cv, "'client_versions' must be a list");
  for (const Node& n : cv->seq) {
    only_keys(n, {"id", "flavour", "date", "hypothetical", "obfuscation"}, "client_versions entry");
    ClientVersionInfo i;
    const auto v = ClientVersion::parse(get_str(n, "id"));
    if (!v) bad(n, "bad client version id");
    i.version = *v;
    if (const Node* h = n.find("hypothetical")) i.hypothetical = h->kind == Node::Kind::Bool && h->b;
    if (const Node* o = n.find("obfuscation")) {
      only_keys(*o, {"keys_env"}, "obfuscation");  // key VALUES are never in a spec file (SEC-S1)
      i.keys_env = get_str(*o, "keys_env");
    }
    out.push_back(std::move(i));
  }
  return out;
}

std::vector<ClientVersionInfo> load_client_versions_yaml(const std::string& path) {
  return load_client_versions_yaml_text(slurp(path));
}

}  // namespace netproto
