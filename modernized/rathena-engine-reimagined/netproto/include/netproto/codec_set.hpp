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
#include <unordered_map>
#include <variant>
#include <vector>

#include "netproto/common.hpp"
#include "netproto/framing.hpp"
#include "netproto/pos_codec.hpp"
#include "netproto/spec.hpp"

namespace netproto {

/// Value of one field. Integers of every width (u64 as its bit pattern) are int64; FixedString is a
/// string (cut at the first NUL); Raw and 1-byte arrays are Bytes; wider integer arrays are a vector;
/// F32 is a double. New alternatives are only ever appended, so existing std::get_if users keep working.
using FieldValue = std::variant<std::int64_t, std::string, Bytes, Pos3, Pos6, std::vector<std::int64_t>, double>;

struct DecodedPacket {
  std::string name;  // semantic name, e.g. CZ_REQUEST_MOVE
  /// Named values. A packet whose layout is `fields: unknown` carries its bytes after the header
  /// (after the opcode, or after the length word of a variable packet) as Bytes under the key "payload".
  std::map<std::string, FieldValue> fields;
  std::vector<std::map<std::string, FieldValue>> repeats;  // repeat-group rows
};

enum class CodecErrc : std::uint8_t {
  kOk = 0,
  kShortFrame,        // fewer than 2 bytes, or fewer than the length word needs
  kUnknownOpcode,     // not in the table of the requested direction
  kBadLength,         // declared length below the header or above the limit
  kLengthMismatch,    // the buffer is not exactly one frame (declared/fixed length != buffer size)
  kFieldOutOfBounds,  // a field (or a row) would be read past the end of the frame
  kRepeatMismatch,    // the repeat group does not tile the frame
  kUnknownPacket,     // encode: no packet of this name for the client version
  kUnknownField,      // encode: a value whose name is not in the layout
  kBadValue,          // encode: wrong value type, out of range, over-long string, ...
  kTooLarge,          // encode: the frame would exceed the variable-length limit
  kWrongDirection,    // encode: packet exists only in the other direction
};
[[nodiscard]] const char* to_string(CodecErrc e) noexcept;

/// Typed failure of decode/encode. Never thrown for a well-formed frame; all offsets and lengths are
/// validated before any byte is read (BR-INTER-013).
struct CodecError : std::runtime_error {
  CodecError(CodecErrc c, const std::string& what);
  CodecErrc code;
};

class CodecSet {
 public:
  /// Builds the set for `v` from the catalog: resolves the packets, drops fields whose `present_when`
  /// does not match `v`, and checks every layout (fields inside the packet, one tail, rows tile).
  /// Throws SpecError on collisions or an inconsistent layout.
  static CodecSet build(const PacketCatalog& catalog, const ClientVersion& v);

  [[nodiscard]] const ClientVersion& version() const noexcept { return version_; }
  /// Client-to-server frames only (what peek_frame/gating need).
  [[nodiscard]] const FrameTable& frames() const noexcept { return frames_; }
  /// Frame table of the server-to-client stream (typed ZC/HC/AC packets and length-only frames).
  [[nodiscard]] FrameTable server_frames() const;
  [[nodiscard]] std::optional<std::string> name_of(std::uint16_t opcode) const;
  [[nodiscard]] std::optional<std::uint16_t> opcode_of(const std::string& name,
                                                        Direction d) const;
  /// Opcode and direction of a packet by name (names are unique per catalog).
  [[nodiscard]] std::optional<std::uint16_t> opcode_of(const std::string& name) const;
  [[nodiscard]] std::optional<Direction> direction_of(const std::string& name) const;
  [[nodiscard]] std::optional<Phase> gate_of(const std::string& name) const;
  /// Layout facts: fields_known = false means decode returns only "payload".
  [[nodiscard]] bool fields_known(const std::string& name) const;
  [[nodiscard]] std::size_t packet_count() const noexcept { return layouts_.size(); }

  /// Decode exactly one complete frame of the client-to-server stream (as delimited by peek_frame).
  /// Validates every length and offset first; throws CodecError, never reads out of bounds.
  [[nodiscard]] DecodedPacket decode(ByteView frame) const;
  /// Same for either direction (S2C also matches length-only `Any` frames).
  [[nodiscard]] DecodedPacket decode(ByteView frame, Direction d) const;
  /// Non-throwing form: returns kOk and fills `out`, or the reason.
  [[nodiscard]] CodecErrc try_decode(ByteView frame, Direction d, DecodedPacket& out) const noexcept;
  /// Encode a packet by name (either direction; the catalog says which). Throws CodecError.
  /// Missing values are zero/empty; values that do not fit their field are an error, not a truncation.
  [[nodiscard]] Bytes encode(const DecodedPacket& p) const;

 private:
  struct Layout {
    std::string name;
    Direction dir = Direction::ClientToServer;
    Phase gate = Phase::InWorld;
    std::uint16_t opcode = 0;
    LengthRule length;
    bool fields_known = true;
    std::vector<FieldSpec> head;  // present on this version, not rows, not the tail
    std::vector<FieldSpec> row;
    std::optional<FieldSpec> tail;
  };
  [[nodiscard]] const Layout* find_op(std::uint16_t opcode, Direction d) const;
  [[nodiscard]] const Layout* find_name(const std::string& name) const;
  [[nodiscard]] CodecErrc decode_layout(const Layout& l, ByteView frame, DecodedPacket& out) const;

  ClientVersion version_;
  FrameTable frames_;
  std::map<std::pair<Direction, std::string>, std::uint16_t> by_name_;
  std::set<std::string> known_;
  std::vector<Layout> layouts_;
  std::unordered_map<std::uint16_t, std::size_t> c2s_, s2c_, any_, inter_;
  std::unordered_map<std::string, std::size_t> by_name_idx_;
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
