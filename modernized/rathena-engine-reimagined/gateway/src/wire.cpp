#include "gateway/wire.hpp"

#include <array>
#include <map>
#include <mutex>

#include "generated/catalog_packets_re_20211103.hpp"

namespace gateway::wire {

using namespace netproto;

namespace {

// Members the legacy code copies as raw, unterminated bytes, typed `raw` instead of netproto's `string`
// (see wire.hpp). Each one is applied exactly once or the catalog changed under us and the build is refused.
struct RawField {
  const char* packet;
  const char* field;
};
constexpr RawField kRawFields[] = {
    {"CA_EXE_HASHCHECK", "hash"},           // char hash[16]: an MD5 digest, any byte value
    {"CH_SECOND_PASSWD_ACK", "pin"},        // char pin[4]: four transported digits, no NUL
    {"CH_MAKE_SECOND_PASSWD", "pin"},
    {"CH_EDIT_SECOND_PASSWD", "old_pin"},
    {"CH_EDIT_SECOND_PASSWD", "new_pin"},
    {"CH_DELETE_CHAR3", "birthdate"},       // char birthdate[6]: six digits, no NUL (memcpy'd by the char server)
    {"AC_ACK_HASH", "salt"},                // char salt[] tail: 4 + md5keylen bytes, no terminator (loginclif.cpp:366)
};

}  // namespace

PacketCatalog PacketCodec::gateway_catalog() {
  const PacketCatalog real = generated::packets_re_20211103_catalog();
  PacketCatalog out;
  std::size_t applied = 0;
  for (PacketDef d : real.packets()) {
    for (const RawField& rf : kRawFields) {
      if (d.name != rf.packet) continue;
      for (PacketVersion& v : d.versions)
        for (FieldSpec& f : v.fields)
          if (f.name == rf.field && f.type == FieldType::FixedString) {
            f.type = FieldType::Raw;  // same offset and width; a width-0 tail stays a tail
            ++applied;
          }
    }
    out.add(std::move(d));
  }
  if (applied != std::size(kRawFields)) throw WireError("netproto catalog changed: a raw member of the gateway layout is gone");
  return out;
}

PacketCodec::PacketCodec(netproto::ClientVersion v) {
  // One immutable codec set per client version for the whole process: the catalog has 1,186 packets and the
  // tests build a codec for nearly every case.
  static std::mutex mu;
  static std::map<netproto::ClientVersion, std::shared_ptr<const Impl>> cache;
  std::lock_guard<std::mutex> lock(mu);
  if (auto it = cache.find(v); it != cache.end()) {
    impl_ = it->second;
    return;
  }
  PacketCatalog cat = gateway_catalog();
  CodecSet set = CodecSet::build(cat, v);
  if (set.packet_count() == 0) throw WireError("unsupported client version " + v.to_string());
  auto impl = std::make_shared<Impl>(Impl{std::move(cat), std::move(set), {}});
  impl->resolved = impl->catalog.resolve(v);  // pointers into impl->catalog, which is never moved again
  cache.emplace(v, impl);
  impl_ = std::move(impl);
}

netproto::FrameResult PacketCodec::peek(ByteView buf, const FrameLimits& lim) const {
  return peek_frame(buf, impl_->set.frames(), lim);
}

const ResolvedPacket* PacketCodec::resolved(const std::string& name) const {
  for (const auto& r : impl_->resolved) if (r.def->name == name) return &r;
  return nullptr;
}

bool PacketCodec::has_packet(const std::string& name) const { return impl_->set.direction_of(name).has_value(); }

const std::vector<FieldSpec>& PacketCodec::fields_of(const std::string& name) const {
  const ResolvedPacket* r = resolved(name);
  if (!r) throw WireError("unknown packet " + name);
  return r->version->fields;
}

std::optional<Phase> PacketCodec::gate_of(std::uint16_t opcode) const {
  const auto it = impl_->set.frames().find(opcode);
  if (it == impl_->set.frames().end()) return std::nullopt;
  return it->second.gate;
}

DecodedPacket PacketCodec::decode(ByteView frame) const {
  try {
    return impl_->set.decode(frame);
  } catch (const CodecError& e) {
    throw WireError(e.what());
  }
}

DecodedPacket PacketCodec::decode_s2c(ByteView frame) const {
  try {
    return impl_->set.decode(frame, Direction::ServerToClient);
  } catch (const CodecError& e) {
    throw WireError(e.what());
  }
}

Bytes PacketCodec::encode(const DecodedPacket& p) const {
  try {
    return impl_->set.encode(p);
  } catch (const CodecError& e) {
    throw WireError(std::string("encode: ") + e.what());
  }
}

// ---- helpers --------------------------------------------------------------------------------------
std::int64_t get_int(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<std::int64_t>(&it->second);
  if (!v) throw WireError("field " + name + " is not an integer");
  return *v;
}
std::string get_str(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<std::string>(&it->second);
  if (!v) throw WireError("field " + name + " is not a string");
  return *v;
}
Bytes get_bytes(const DecodedPacket& p, const std::string& name) {
  const auto it = p.fields.find(name);
  if (it == p.fields.end()) throw WireError("missing field " + name);
  const auto* v = std::get_if<Bytes>(&it->second);
  if (!v) throw WireError("field " + name + " is not bytes");
  return *v;
}
void set_int(DecodedPacket& p, const std::string& n, std::int64_t v) { p.fields[n] = v; }
void set_str(DecodedPacket& p, const std::string& n, const std::string& v) { p.fields[n] = v; }
void set_bytes(DecodedPacket& p, const std::string& n, const Bytes& v) { p.fields[n] = v; }
DecodedPacket make(const std::string& name) {
  DecodedPacket p;
  p.name = name;
  return p;
}

std::string fit(const std::string& s, std::size_t width) {
  return width == 0 ? std::string() : s.substr(0, width - 1);
}

std::string hex_of(const Bytes& b) {
  static const char hex[] = "0123456789abcdef";
  std::string s;
  for (std::uint8_t c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 15]); }
  return s;
}

namespace {
std::array<int, 10> pin_tab(std::uint32_t seed) {
  std::array<int, 10> tab = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 1; i < 10; ++i) {
    seed = 0x881234u + seed * 0x3498u;
    const int pos = static_cast<int>(seed % static_cast<std::uint32_t>(i + 1));
    if (i != pos) std::swap(tab[static_cast<std::size_t>(i)], tab[static_cast<std::size_t>(pos)]);
  }
  return tab;
}
}  // namespace

std::string pin_untransport(std::uint32_t seed, const std::string& wire) {
  if (wire.size() != 4) return {};
  const auto tab = pin_tab(seed);
  std::string out;
  for (char c : wire) {
    if (c < '0' || c > '9') return {};
    out.push_back(static_cast<char>('0' + tab[static_cast<std::size_t>(c - '0')]));
  }
  return out;
}

std::string pin_transport(std::uint32_t seed, const std::string& real) {
  if (real.size() != 4) return {};
  const auto tab = pin_tab(seed);
  std::array<int, 10> inv{};
  for (int i = 0; i < 10; ++i) inv[static_cast<std::size_t>(tab[static_cast<std::size_t>(i)])] = i;
  std::string out;
  for (char c : real) {
    if (c < '0' || c > '9') return {};
    out.push_back(static_cast<char>('0' + inv[static_cast<std::size_t>(c - '0')]));
  }
  return out;
}

int sex_to_wire(Sex s) {
  switch (s) {
    case Sex::Female: return 0;
    case Sex::Male: return 1;
    case Sex::Server: return 2;
  }
  return 1;
}

}  // namespace gateway::wire
