#pragma once
// Per-session codec set selected by the negotiated (date, flavour).
// Production codecs are GENERATED from proto/*.yaml by
// tools/codegen/netproto_codegen.py; this interface is what generated code and
// hand-written glue both implement. Generated files are never edited.
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>

#include "netproto/common.hpp"
#include "netproto/framing.hpp"
#include "netproto/pos_codec.hpp"
#include "netproto/spec.hpp"

namespace netproto {

using FieldValue = std::variant<std::int64_t, std::string, Bytes, Pos3, Pos6>;

struct DecodedPacket {
  std::string name;  // semantic name, e.g. CZ_REQUEST_MOVE
  std::map<std::string, FieldValue> fields;
  std::vector<std::map<std::string, FieldValue>> repeats;  // repeat-group rows
};

class CodecSet {
 public:
  /// Builds the set for `v` from the catalog. Throws SpecError on collisions.
  static CodecSet build(const PacketCatalog& catalog, const ClientVersion& v);

  [[nodiscard]] const ClientVersion& version() const noexcept { return version_; }
  [[nodiscard]] const FrameTable& frames() const noexcept { return frames_; }
  [[nodiscard]] std::optional<std::string> name_of(std::uint16_t opcode) const;
  [[nodiscard]] std::optional<std::uint16_t> opcode_of(const std::string& name,
                                                        Direction d) const;

  /// Decode exactly one complete frame (as delimited by peek_frame). Must
  /// validate every length and offset; never read out of bounds. Stub.
  [[nodiscard]] DecodedPacket decode(ByteView frame) const;
  /// Encode a server-to-client (or, for the sim client, client-to-server) packet. Stub.
  [[nodiscard]] Bytes encode(const DecodedPacket& p) const;

 private:
  ClientVersion version_;
  FrameTable frames_;
  std::map<std::pair<Direction, std::string>, std::uint16_t> by_name_;
  std::set<std::string> known_;
};

/// Negotiation result for a connecting client. nullptr = unsupported version
/// (the session is refused; there is no fallback to a "closest" version).
class CodecRegistry {
 public:
  explicit CodecRegistry(PacketCatalog catalog, std::set<ClientVersion> supported)
      : catalog_(std::move(catalog)), supported_(std::move(supported)) {}
  [[nodiscard]] std::shared_ptr<const CodecSet> negotiate(const ClientVersion& v);

 private:
  PacketCatalog catalog_;
  std::set<ClientVersion> supported_;
  std::map<ClientVersion, std::shared_ptr<const CodecSet>> cache_;
};

}  // namespace netproto
