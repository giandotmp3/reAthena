#pragma once
// Wire layer of the gateway: the packets of the login, char-select and world-connect phases, for the
// one client netproto generates first (re/20211103).
//
// Everything below the handlers is netproto's, compiled read-only and used as is:
//   * the REAL generated catalog of the client (proto/packets_re_20211103.yaml, 1,186 packets: CA_LOGIN
//     0x0064, CZ_ENTER 0x0436 with its 23-byte RE length, CHARACTER_INFO rows of 175 bytes, ...),
//   * CodecSet: decode / encode over the catalog's FieldSpec model (strings are refused when they do not
//     fit, arrays, signed fields, string and raw tails, repeat groups, the length word of variable packets),
//   * peek_frame framing and limits, the gate table (FrameEntry::gate) with gate_packet, validate_connect.
// PacketCodec is only the seam the handlers and tests talk to: it owns the (process-wide, immutable)
// CodecSet for one client version and turns netproto's CodecError into WireError.
//
// One deliberate difference from the catalog, kept as DATA in wire.cpp (kRawFields) and proved by tests:
// seven fixed `char[N]` members that the legacy code copies as raw, unterminated bytes are typed `raw`
// here, not `string`: CA_EXE_HASHCHECK.hash (a binary digest, a NUL byte would truncate it), the PIN
// fields and the birthdate (4 or 6 bytes with no room for a NUL, so a string encode of a full field is
// refused) and AC_ACK_HASH.salt (loginclif.cpp:366 sends `4 + md5keylen` bytes, no terminator). See the
// gateway README, "netproto findings".
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

/// The reply of the map-side connect refusal. The legacy `clif_parse_WantToConnection` writes opcode 0x006a with
/// `packet_len(0x6a)` = 23 bytes, the error code at byte 2 (src/map/clif.cpp:10685); the catalog lists 0x006a as a
/// length-only frame of that name. `ZC_REFUSE_ENTER` (0x0074, 3 bytes) is not what the legacy server sends.
inline constexpr const char* kConnectRefusal = "OPAQUE_006A";

class PacketCodec {
 public:
  /// Throws WireError for a client version the catalog has no packets for (there is no "closest version").
  explicit PacketCodec(netproto::ClientVersion v = kDefaultClient);
  const netproto::CodecSet& set() const { return impl_->set; }
  const netproto::ClientVersion& version() const { return impl_->set.version(); }
  netproto::FrameResult peek(ByteView buf, const netproto::FrameLimits& lim = {}) const;
  /// Decode one complete client-to-server frame. Throws WireError.
  DecodedPacket decode(ByteView frame) const;
  /// Decode one complete server-to-client frame (used by the sim client / tests to read the gateway's replies).
  DecodedPacket decode_s2c(ByteView frame) const;
  /// Encode a packet by name (either direction; the catalog says which). Throws WireError: unknown packet or
  /// field, a value that does not fit (an over-long string is refused, never cut), a frame over the limit.
  Bytes encode(const DecodedPacket& p) const;
  /// Gate (netproto::Phase) of a client-to-server opcode, nullopt if the opcode is not a client packet.
  std::optional<netproto::Phase> gate_of(std::uint16_t opcode) const;
  bool has_packet(const std::string& name) const;
  /// Layout facts for tests.
  const netproto::ResolvedPacket* resolved(const std::string& name) const;
  const std::vector<netproto::FieldSpec>& fields_of(const std::string& name) const;
  const netproto::PacketCatalog& catalog() const { return impl_->catalog; }

  /// The catalog the codec is built from: netproto's generated catalog with the kRawFields retyping applied.
  static netproto::PacketCatalog gateway_catalog();

 private:
  struct Impl {
    netproto::PacketCatalog catalog;
    netproto::CodecSet set;
    std::vector<netproto::ResolvedPacket> resolved;
  };
  std::shared_ptr<const Impl> impl_;
};

// ---- field helpers (used by handlers and tests) ----------------------------------------------------
std::int64_t get_int(const DecodedPacket& p, const std::string& name);
std::string get_str(const DecodedPacket& p, const std::string& name);
Bytes get_bytes(const DecodedPacket& p, const std::string& name);
void set_int(DecodedPacket& p, const std::string& name, std::int64_t v);
void set_str(DecodedPacket& p, const std::string& name, const std::string& v);
void set_bytes(DecodedPacket& p, const std::string& name, const Bytes& v);
DecodedPacket make(const std::string& name);

/// Display text for a fixed `char[width]` member: cut to width-1 bytes (room for the NUL), like the legacy
/// `safestrncpy` on server names, map names and unblock dates. The codec never does this by itself; the handler
/// decides, here, for text the gateway itself owns (never for client input).
std::string fit(const std::string& s, std::size_t width);

/// The 8 hex-digit-pair string -> raw bytes helper for 16-byte MD5 fields.
std::string hex_of(const Bytes& b);

/// Character-select keypad transport (legacy char_pincode_decrypt): the client sends the PIN digits
/// scrambled with a per-session seed; returns the real digits or an empty string for malformed input.
std::string pin_untransport(std::uint32_t seed, const std::string& wire_digits);
std::string pin_transport(std::uint32_t seed, const std::string& real_digits);  // inverse, for the sim client

/// Legacy login client sex byte: F=0, M=1, S=2.
int sex_to_wire(Sex s);

}  // namespace gateway::wire
