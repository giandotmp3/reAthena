#include "legacy_npc/parser.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "legacy_npc/package.hpp"

namespace legacy_npc {

std::uint64_t source_hash(std::string_view source) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : source) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

namespace {

DeclKind kind_of(const std::string& s) {
  if (s == "script") return DeclKind::Script;
  if (s == "function") return DeclKind::Function;
  if (s == "duplicate") return DeclKind::Duplicate;
  if (s == "shop") return DeclKind::Shop;
  if (s == "warp") return DeclKind::Warp;
  if (s == "monster") return DeclKind::Monster;
  return DeclKind::Mapflag;
}

Op op_of(const std::string& s) {
  if (s == "command") return Op::Command;
  if (s == "assign") return Op::Assign;
  if (s == "jz") return Op::JumpIfFalse;
  if (s == "jnz") return Op::JumpIfTrue;
  if (s == "jmp") return Op::Jump;
  if (s == "switch") return Op::Switch;
  if (s == "goto") return Op::Goto;
  if (s == "callsub") return Op::Callsub;
  if (s == "callfunc") return Op::Callfunc;
  if (s == "return") return Op::Return;
  if (s == "end") return Op::End;
  if (s == "menu") return Op::Menu;
  return Op::Nop;
}

int to_int(const std::string& s) {
  if (s.empty()) return 0;
  return std::atoi(s.c_str());
}

}  // namespace

// The grammar lives in the Luau package (luau/parser.luau, lower.luau); this function runs it in the package sandbox and converts the
// result: one Npc per declaration, the lowered flat op list as Program::code.
ParsedFile parse_file(std::string_view source, std::string_view file_name) {
  ParsedFile f;
  f.file_name = std::string(file_name);
  f.source_hash = source_hash(source);
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("parse_file", "LEGACY.dump.file(" + long_string(std::string(source)) + ", " + long_string(std::string(file_name)) + ")");
  Npc* cur = nullptr;
  for (auto& r : host.records("@")) {
    const std::string& k = r[0];
    if (k == "@npc" && r.size() >= 9) {
      f.npcs.emplace_back();
      cur = &f.npcs.back();
      cur->kind = kind_of(r[1]);
      cur->name = r[2];
      cur->map = r[3];
      cur->x = to_int(r[4]);
      cur->y = to_int(r[5]);
      cur->dir = to_int(r[6]);
      cur->duplicate_of = r[7];
      cur->program.name_line = static_cast<std::uint32_t>(to_int(r[8]));
      cur->program.source_hash = f.source_hash;
    } else if (k == "@body" && cur) {
      cur->program.body += r.size() > 1 ? r[1] : "";
    } else if (k == "@op" && cur && r.size() >= 5) {
      Instr i;
      i.op = op_of(r[1]);
      i.name = r[2];
      i.target = r[3].empty() ? -1 : to_int(r[3]) - 1;
      i.line = static_cast<std::uint32_t>(to_int(r[4]));
      cur->program.code.push_back(std::move(i));
    } else if (k == "@label" && cur && r.size() >= 3) {
      cur->program.labels[r[1]] = to_int(r[2]) - 1;
    } else if (k == "@used" && cur && r.size() >= 2) {
      cur->program.used_commands.push_back(r[1]);
    } else if (k == "@diag") {
      f.diagnostics.push_back({static_cast<std::uint32_t>(to_int(r.size() > 1 ? r[1] : "")), r.size() > 2 ? r[2] : ""});
    } else if (k == "@end") {
      cur = nullptr;
    }
  }
  return f;
}

// ---- binary cache ------------------------------------------------------------------------------------------------------------
namespace {

constexpr char kMagic[4] = {'L', 'N', 'A', 'C'};
constexpr std::uint32_t kVersion = 2;

struct Writer {
  std::string out;
  void u8(std::uint8_t v) { out.push_back(static_cast<char>(v)); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }
  void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); out += s; }
};

struct Reader {
  std::string_view in;
  std::size_t pos = 0;
  void need(std::size_t n) const { if (pos + n > in.size()) throw std::runtime_error("truncated"); }
  std::uint8_t u8() { need(1); return static_cast<std::uint8_t>(in[pos++]); }
  std::uint32_t u32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(u8()) << (8 * i); return v; }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::uint64_t u64() { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(u8()) << (8 * i); return v; }
  std::string str() {
    auto n = u32();
    if (n > in.size()) throw std::runtime_error("bad length");
    need(n);
    std::string s(in.substr(pos, n));
    pos += n;
    return s;
  }
};

void write_value(Writer& w, const Value& v) {
  if (auto* i = std::get_if<std::int64_t>(&v)) { w.u8(0); w.u64(static_cast<std::uint64_t>(*i)); }
  else { w.u8(1); w.str(std::get<std::string>(v)); }
}
Value read_value(Reader& r) {
  if (r.u8() == 0) return static_cast<std::int64_t>(r.u64());
  return r.str();
}
void write_expr(Writer& w, const Expr& e) {
  w.u8(static_cast<std::uint8_t>(e.kind));
  w.str(e.text);
  write_value(w, e.literal);
  w.u32(static_cast<std::uint32_t>(e.children.size()));
  for (auto& c : e.children) write_expr(w, c);
}
Expr read_expr(Reader& r, int depth = 0) {
  if (depth > 256) throw std::runtime_error("expression too deep");
  Expr e;
  e.kind = static_cast<Expr::Kind>(r.u8());
  e.text = r.str();
  e.literal = read_value(r);
  auto n = r.u32();
  if (n > r.in.size()) throw std::runtime_error("bad count");
  for (std::uint32_t i = 0; i < n; ++i) e.children.push_back(read_expr(r, depth + 1));
  return e;
}

}  // namespace

std::string serialize_ast(const ParsedFile& f) {
  Writer w;
  w.out.append(kMagic, 4);
  w.u32(kVersion);
  w.u64(f.source_hash);
  w.str(f.file_name);
  w.u32(static_cast<std::uint32_t>(f.diagnostics.size()));
  for (auto& d : f.diagnostics) { w.u32(d.line); w.str(d.message); }
  w.u32(static_cast<std::uint32_t>(f.npcs.size()));
  for (auto& n : f.npcs) {
    w.u8(static_cast<std::uint8_t>(n.kind));
    w.str(n.name); w.str(n.map); w.i32(n.x); w.i32(n.y); w.i32(n.dir); w.str(n.duplicate_of);
    auto& p = n.program;
    w.u64(p.source_hash); w.str(p.body); w.u32(p.name_line);
    w.u32(static_cast<std::uint32_t>(p.used_commands.size()));
    for (auto& c : p.used_commands) w.str(c);
    w.u32(static_cast<std::uint32_t>(p.labels.size()));
    for (auto& [k, v] : p.labels) { w.str(k); w.i32(v); }
    w.u32(static_cast<std::uint32_t>(p.code.size()));
    for (auto& i : p.code) {
      w.u8(static_cast<std::uint8_t>(i.op)); w.str(i.name); w.i32(i.target); w.u32(i.line);
      w.u32(static_cast<std::uint32_t>(i.args.size()));
      for (auto& a : i.args) write_expr(w, a);
    }
  }
  return w.out;
}

// A blob whose recorded hash differs from expected_hash, or one that is not a valid cache, yields an empty ParsedFile (hash 0): the
// caller parses the source again.
ParsedFile deserialize_ast(std::string_view blob, std::uint64_t expected_hash) {
  try {
    Reader r{blob};
    r.need(4);
    if (std::memcmp(blob.data(), kMagic, 4) != 0) return {};
    r.pos = 4;
    if (r.u32() != kVersion) return {};
    ParsedFile f;
    f.source_hash = r.u64();
    if (f.source_hash != expected_hash) return {};
    f.file_name = r.str();
    auto nd = r.u32();
    if (nd > blob.size()) return {};
    for (std::uint32_t i = 0; i < nd; ++i) { Diagnostic d; d.line = r.u32(); d.message = r.str(); f.diagnostics.push_back(std::move(d)); }
    auto nn = r.u32();
    if (nn > blob.size()) return {};
    for (std::uint32_t i = 0; i < nn; ++i) {
      Npc n;
      n.kind = static_cast<DeclKind>(r.u8());
      n.name = r.str(); n.map = r.str(); n.x = r.i32(); n.y = r.i32(); n.dir = r.i32(); n.duplicate_of = r.str();
      auto& p = n.program;
      p.source_hash = r.u64(); p.body = r.str(); p.name_line = r.u32();
      auto nu = r.u32();
      if (nu > blob.size()) return {};
      for (std::uint32_t k = 0; k < nu; ++k) p.used_commands.push_back(r.str());
      auto nl = r.u32();
      if (nl > blob.size()) return {};
      for (std::uint32_t k = 0; k < nl; ++k) { auto key = r.str(); p.labels[key] = r.i32(); }
      auto nc = r.u32();
      if (nc > blob.size()) return {};
      for (std::uint32_t k = 0; k < nc; ++k) {
        Instr ins;
        ins.op = static_cast<Op>(r.u8()); ins.name = r.str(); ins.target = r.i32(); ins.line = r.u32();
        auto na = r.u32();
        if (na > blob.size()) return {};
        for (std::uint32_t a = 0; a < na; ++a) ins.args.push_back(read_expr(r));
        p.code.push_back(std::move(ins));
      }
      f.npcs.push_back(std::move(n));
    }
    return f;
  } catch (const std::exception&) {
    return {};
  }
}

}  // namespace legacy_npc
