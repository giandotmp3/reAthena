#include "netproto/spec.hpp"

#include <map>
#include <sstream>
#include <tuple>

namespace netproto {

namespace {
const char* flavour_name(Flavour f) {
  switch (f) {
    case Flavour::Main: return "main";
    case Flavour::Re: return "re";
    case Flavour::Zero: return "zero";
  }
  return "?";
}
std::optional<Flavour> flavour_from(const std::string& s) {
  if (s == "main" || s == "MAIN") return Flavour::Main;
  if (s == "re" || s == "RE") return Flavour::Re;
  if (s == "zero" || s == "ZERO") return Flavour::Zero;
  return std::nullopt;
}
std::optional<std::uint32_t> parse_date(const std::string& s) {
  if (s.size() != 8) return std::nullopt;
  std::uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + static_cast<std::uint32_t>(c - '0');
  }
  return v;
}
}  // namespace

std::string ClientVersion::to_string() const {
  return std::string(flavour_name(flavour)) + "/" + std::to_string(date);
}

std::optional<ClientVersion> ClientVersion::parse(const std::string& s) {
  const auto slash = s.find('/');
  if (slash == std::string::npos) return std::nullopt;
  const auto f = flavour_from(s.substr(0, slash));
  const auto d = parse_date(s.substr(slash + 1));
  if (!f || !d) return std::nullopt;
  return ClientVersion{*d, *f};
}

bool VersionPredicate::matches(const ClientVersion& v) const {
  if (any_of.empty()) return true;
  for (const auto& t : any_of) {
    if (t.flavour != v.flavour) continue;
    if (v.date < t.min_date) continue;
    if (t.before_date && v.date >= *t.before_date) continue;
    return true;
  }
  return false;
}

// Grammar: term ("||" term)* ; term := FLAVOUR ">=" DATE [ "&&" FLAVOUR "<" DATE ]
std::optional<VersionPredicate> VersionPredicate::parse(const std::string& expr) {
  VersionPredicate p;
  std::string rest = expr;
  auto trim = [](std::string s) {
    const auto b = s.find_first_not_of(' ');
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(' ') - b + 1);
  };
  if (trim(rest).empty()) return p;
  std::size_t pos = 0;
  while (pos <= rest.size()) {
    auto next = rest.find("||", pos);
    std::string term = trim(rest.substr(pos, next == std::string::npos ? next : next - pos));
    VersionTerm t;
    std::string lower = term;
    std::string upper;
    if (const auto amp = term.find("&&"); amp != std::string::npos) {
      lower = trim(term.substr(0, amp));
      upper = trim(term.substr(amp + 2));
    }
    const auto ge = lower.find(">=");
    if (ge == std::string::npos) return std::nullopt;
    const auto f = flavour_from(trim(lower.substr(0, ge)));
    const auto d = parse_date(trim(lower.substr(ge + 2)));
    if (!f || !d) return std::nullopt;
    t.flavour = *f;
    t.min_date = *d;
    if (!upper.empty()) {
      const auto lt = upper.find('<');
      if (lt == std::string::npos) return std::nullopt;
      const auto f2 = flavour_from(trim(upper.substr(0, lt)));
      const auto d2 = parse_date(trim(upper.substr(lt + 1)));
      if (!f2 || *f2 != *f || !d2) return std::nullopt;
      t.before_date = *d2;
    }
    p.any_of.push_back(t);
    if (next == std::string::npos) break;
    pos = next + 2;
  }
  return p;
}

void PacketCatalog::add(PacketDef def) { packets_.push_back(std::move(def)); }

std::vector<ResolvedPacket> PacketCatalog::resolve(const ClientVersion& v) const {
  std::vector<ResolvedPacket> out;
  std::map<std::pair<Direction, std::uint16_t>, std::string> by_opcode;
  for (const auto& def : packets_) {
    const PacketVersion* hit = nullptr;
    for (const auto& pv : def.versions) {
      if (!pv.applies.matches(v)) continue;
      if (hit) throw SpecError("ambiguous layouts for " + def.name + " on " + v.to_string());
      hit = &pv;
    }
    if (!hit) continue;
    const auto key = std::make_pair(def.direction, hit->opcode);
    if (auto [it, fresh] = by_opcode.emplace(key, def.name); !fresh) {
      std::ostringstream o;
      o << "opcode 0x" << std::hex << hit->opcode << " claimed by " << it->second << " and "
        << def.name << " on " << v.to_string();
      throw SpecError(o.str());
    }
    out.push_back({&def, hit});
  }
  return out;
}

}  // namespace netproto
