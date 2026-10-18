#pragma once
// Domain model of the machine-readable packet specification (proto/*.yaml).
// One semantic packet name maps to several opcodes across (date, flavour);
// one opcode maps to several names across versions (0x0072 is WantToConnection,
// DropItem and UseItem at different times). Both directions are modelled by
// PacketVersion entries that carry a VersionPredicate.
//
// Spec section 4.1; architecture section 4 (netproto) and section 8 (codegen).
#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "netproto/common.hpp"

namespace netproto {

enum class Flavour : std::uint8_t { Main, Re, Zero };

/// Negotiated client identity. `date` is YYYYMMDD (PACKETVER style).
struct ClientVersion {
  std::uint32_t date = 0;
  Flavour flavour = Flavour::Main;
  auto operator<=>(const ClientVersion&) const = default;
  [[nodiscard]] std::string to_string() const;  // "main/20211103"
  static std::optional<ClientVersion> parse(const std::string& s);
};

/// "MAIN>=X || RE>=Y || ZERO>=Z". An empty predicate matches every version.
/// Each term may carry an optional exclusive upper bound ("<").
struct VersionTerm {
  Flavour flavour = Flavour::Main;
  std::uint32_t min_date = 0;
  std::optional<std::uint32_t> before_date;  // exclusive
};
struct VersionPredicate {
  std::vector<VersionTerm> any_of;
  [[nodiscard]] bool matches(const ClientVersion& v) const;
  static std::optional<VersionPredicate> parse(const std::string& expr);
};

/// `Any`: the legacy packet DB lists the opcode and its length but neither a handler nor a typed struct, so
/// the direction is not known (length-only frames). They are accepted by receivers of server-to-client
/// streams (to skip them) and never by the client-to-server frame table (unknown opcode = disconnect).
enum class Direction : std::uint8_t { ClientToServer, ServerToClient, Inter, Any };

/// Session phase that must be reached before a packet is accepted (gating).
enum class Phase : std::uint8_t { PreSession, PreMapLoad, InWorld };

enum class FieldType : std::uint8_t {
  U8, U16, U32, I8, I16, I32, I64,
  FixedString,  // NUL-padded, `width` bytes
  Raw,          // opaque bytes, `width` bytes
  Pos3,         // packed 3-byte x/y/dir
  Pos6,         // packed 6-byte src/dst
  U64,          // carried in an int64 (bit pattern)
  F32,          // IEEE-754 binary32, carried as a double
};

struct FieldSpec {
  std::string name;
  std::uint16_t offset = 0;     // from the start of the packet, or of the row when `row`
  FieldType type = FieldType::U8;
  /// Bytes for FixedString/Raw; implied otherwise. FixedString/Raw with width 0 is a TAIL: it runs from
  /// `offset` to the end of the (variable-length) packet. A string tail is NUL-terminated on the wire.
  std::uint16_t width = 0;
  std::uint16_t array_len = 1;  // fixed repeat count (integer types)
  VersionPredicate present_when;  // empty = always present; otherwise the field exists on matching clients only
  /// Field of the repeat group (LengthRule::repeat): `offset` is relative to the start of the row. The
  /// legacy gateway convention (name starts with "[].") is accepted as well, see is_row_field().
  bool row = false;
  /// Bit-field inside the integer storage of `type` (little-endian, first declared = least significant,
  /// as the compiler lays out the legacy structs). bit_width 0 = the whole integer.
  std::uint8_t bit_shift = 0;
  std::uint8_t bit_width = 0;
};

/// True for repeat-group fields: `row`, or the gateway's "[]." name prefix.
[[nodiscard]] inline bool is_row_field(const FieldSpec& f) { return f.row || f.name.rfind("[].", 0) == 0; }
/// Field name as it appears in DecodedPacket (the "[]." prefix is not part of the key).
[[nodiscard]] inline std::string field_key(const FieldSpec& f) {
  return f.name.rfind("[].", 0) == 0 ? f.name.substr(3) : f.name;
}

enum class LengthKind : std::uint8_t { Fixed, Variable };

struct RepeatGroup {
  std::uint16_t offset = 0;  // first element
  std::uint16_t stride = 0;  // bytes per element
};

struct LengthRule {
  LengthKind kind = LengthKind::Fixed;
  std::uint16_t fixed_size = 0;     // Fixed: whole packet, opcode included
  std::uint16_t prefix_offset = 2;  // Variable: offset of the u16 length
  std::uint16_t header_size = 4;    // Variable: minimum legal declared length
  std::optional<RepeatGroup> repeat;
};

/// Layout of one semantic packet on a range of client versions.
struct PacketVersion {
  VersionPredicate applies;
  std::uint16_t opcode = 0;
  LengthRule length;
  std::vector<FieldSpec> fields;
  /// false: the layout lives in a legacy handler (clif_parse_*), not in a typed struct, so
  /// `fields` is empty by design and no field type is guessed. `handler_offsets` then
  /// carries the legacy pos[] list (offsets the handler reads), whose types are unknown.
  bool fields_known = true;
  std::vector<std::uint16_t> handler_offsets;
  /// Provenance of the layout: "typed-struct" (packets_struct.hpp / packets.hpp), "common-struct"
  /// (src/common/packets.hpp), "handler-derived" (straight RFIFO reads of a clif_parse_* body),
  /// "length-only" (packet DB lists only the length), "" (hand-built / unknown).
  std::string source;
};

struct PacketDef {
  std::string name;      // CZ_REQUEST_MOVE
  Direction direction = Direction::ClientToServer;
  std::string category;  // movement, chat, ...
  Phase gate = Phase::InWorld;  // earliest phase that accepts it
  std::vector<PacketVersion> versions;
};

/// Non-owning view into the PacketCatalog that produced it.
struct ResolvedPacket {
  const PacketDef* def = nullptr;
  const PacketVersion* version = nullptr;
};

/// All packets of a proto set. resolve() builds the per-version view.
class PacketCatalog {
 public:
  void add(PacketDef def);
  [[nodiscard]] const std::vector<PacketDef>& packets() const noexcept { return packets_; }

  /// Every packet that exists on `v`, in declaration order. Throws SpecError
  /// when a name has two matching layouts, or two names of the same direction
  /// collide on one opcode for `v`.
  [[nodiscard]] std::vector<ResolvedPacket> resolve(const ClientVersion& v) const;

 private:
  std::vector<PacketDef> packets_;
};

struct SpecError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/// Reads proto/*.yaml with the built-in YAML-subset reader (netproto/yaml.hpp). Runtime servers can use
/// the tables generated from the same files instead; tests prove the two are identical. Throws SpecError
/// ("yaml line N: ...") on malformed input, unknown keys, bad types or duplicate packet names.
PacketCatalog load_catalog_yaml(const std::string& path);
PacketCatalog load_catalog_yaml_text(const std::string& text);

/// `client_versions:` of a proto file. `keys_env` is the NAME of the environment variable that carries
/// the packet-obfuscation keys of that client (never a value).
struct ClientVersionInfo {
  ClientVersion version;
  bool hypothetical = false;
  std::string keys_env;
};
std::vector<ClientVersionInfo> load_client_versions_yaml(const std::string& path);
std::vector<ClientVersionInfo> load_client_versions_yaml_text(const std::string& text);

}  // namespace netproto
