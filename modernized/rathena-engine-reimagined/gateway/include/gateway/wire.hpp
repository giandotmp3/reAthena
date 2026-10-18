#pragma once
// Wire layer of the gateway: the packets of the login, char-select and world-connect phases, for the
// one client netproto generates first (re/20211103).
//
// What comes from netproto (compiled read-only, nothing copied): the catalog model (PacketCatalog,
// PacketDef, FieldSpec), the REAL generated catalog (CA_LOGIN 0x0064, CZ_ENTER 0x0436 with its 23-byte
// RE length), CodecSet (opcode<->name, frame table), peek_frame framing and limits, gate_packet phase
// gating, validate_connect (BR-MAP-060).
//
// What netproto does NOT provide yet: CodecSet::decode / CodecSet::encode are stubs there
// (NotImplementedError) and its generated catalog has only 9 packets and no field types for
// handler-parsed packets. So this file (a) adds the login/char-phase packets as gateway-owned
// PacketDefs whose layouts are taken from src/common/packets.hpp (PACKETVER 20211103, RE), and (b)
// interprets netproto's FieldSpec model (offset, type, width) in PacketCodec::decode/encode. Where the
// real catalog has `fields_known = false` (CA_LOGIN, CZ_ENTER) the field types come from an overlay
// whose offsets are tested against netproto's handler_offsets. When netproto implements its codec the
// two interpreters should be merged; the layouts here are already in netproto's data model.
//
// Conventions of the interpreter: Raw with width 0 = "the rest of the packet"; field names starting
// with "[]." belong to the repeat group and carry offsets relative to the row start.
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gateway/types.hpp"
#include "netproto/codec_set.hpp"
#include "netproto/connect_check.hpp"
#include "netproto/framing.hpp"
#include "netproto/gating.hpp"

namespace gateway::wire {

using netproto::ByteView;
using netproto::Bytes;
using netproto::DecodedPacket;
using netproto::FieldValue;

struct WireError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline constexpr netproto::ClientVersion kDefaultClient{20211103, netproto::Flavour::Re};
inline constexpr std::size_t kCharInfoSize = 175;  // sizeof(CHARACTER_INFO) at RE 20211103 (g++ on packets.hpp)

class PacketCodec {
 public:
  explicit PacketCodec(netproto::ClientVersion v = kDefaultClient);
  const netproto::CodecSet& set() const { return *set_; }
  const netproto::ClientVersion& version() const { return version_; }
  netproto::FrameResult peek(ByteView buf, const netproto::FrameLimits& lim = {}) const;
  /// Decode one complete client-to-server frame. Throws WireError.
  DecodedPacket decode(ByteView frame) const;
  /// Decode one complete server-to-client frame (used by the sim client / tests to read the gateway's replies).
  DecodedPacket decode_s2c(ByteView frame) const;
  /// Encode a packet (direction taken from the catalog: server-to-client first, then client-to-server).
  Bytes encode(const DecodedPacket& p) const;
  /// Gate (netproto::Phase) of a client-to-server opcode, nullopt if unknown.
  std::optional<netproto::Phase> gate_of(std::uint16_t opcode) const;
  bool has_packet(const std::string& name) const;
  /// Layout facts for tests.
  const netproto::ResolvedPacket* resolved(const std::string& name) const;
  const std::vector<netproto::FieldSpec>& fields_of(const std::string& name) const;
  const netproto::PacketCatalog& catalog() const { return *catalog_; }

 private:
  DecodedPacket decode_with(const netproto::ResolvedPacket& r, ByteView frame) const;
  netproto::ClientVersion version_;
  std::shared_ptr<const netproto::PacketCatalog> catalog_;
  std::shared_ptr<const netproto::CodecSet> set_;
  std::vector<netproto::ResolvedPacket> resolved_;
};

// ---- field helpers (used by handlers and tests) ----------------------------------------------------
std::int64_t get_int(const DecodedPacket& p, const std::string& name);
std::string get_str(const DecodedPacket& p, const std::string& name);
Bytes get_bytes(const DecodedPacket& p, const std::string& name);
void set_int(DecodedPacket& p, const std::string& name, std::int64_t v);
void set_str(DecodedPacket& p, const std::string& name, const std::string& v);
void set_bytes(DecodedPacket& p, const std::string& name, const Bytes& v);
DecodedPacket make(const std::string& name);

/// The 8 hex-digit-pair string -> raw bytes helper for 16-byte MD5 fields.
std::string hex_of(const Bytes& b);

/// Character-select keypad transport (legacy char_pincode_decrypt): the client sends the PIN digits
/// scrambled with a per-session seed; returns the real digits or an empty string for malformed input.
std::string pin_untransport(std::uint32_t seed, const std::string& wire_digits);
std::string pin_transport(std::uint32_t seed, const std::string& real_digits);  // inverse, for the sim client

/// Legacy login client sex byte: F=0, M=1, S=2.
int sex_to_wire(Sex s);

}  // namespace gateway::wire
