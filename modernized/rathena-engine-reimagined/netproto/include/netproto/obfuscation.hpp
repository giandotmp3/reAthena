#pragma once
// Rolling opcode obfuscation (spec 4.1), the legacy PACKET_OBFUSCATION scheme of src/map/clif.cpp
// (clif_parse, clif_parse_WantToConnection) for the client-to-server stream. Decoding is STATEFUL.
//
//   f(x)       = (x * k1 + k2) mod 2^32                       (k0, k1, k2 = clif_cryptKey[0..2])
//   connect    : plain = wire ^ ((f(k0) >> 16) & 0x7FFF)      (no session yet: the first packet)
//   then       : key = f(f(k0))
//   every next : plain = wire ^ ((key >> 16) & 0x7FFF); key = f(key)   (only once the frame is valid)
//
// The three key words are per-deployment data. They come from the environment (the name is the
// `obfuscation.keys_env` of the proto file), never from a literal in code, config or proto data; see
// parse_keys / keys_from_env. Tests use FAKE keys only.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "netproto/common.hpp"

namespace netproto {

struct ObfuscationKeys {
  std::array<std::uint32_t, 3> k{};  // legacy clif_cryptKey[0], [1], [2]
  bool operator==(const ObfuscationKeys&) const = default;
};

/// "0xAAAAAAAA,0xBBBBBBBB,0xCCCCCCCC" (hex needs the 0x prefix, otherwise decimal; commas or spaces).
/// nullopt on anything else (wrong count, junk, overflow). Never logs or echoes the input.
[[nodiscard]] std::optional<ObfuscationKeys> parse_keys(std::string_view text);
/// Reads the keys from the environment variable `env_name`. nullopt when unset or malformed.
[[nodiscard]] std::optional<ObfuscationKeys> keys_from_env(const std::string& env_name);

/// One per session and direction. Server side uses decode(), the sim client encode(); both walk the
/// same schedule, so a client and a server built from equal keys stay in lockstep.
class OpcodeObfuscator {
 public:
  explicit OpcodeObfuscator(ObfuscationKeys keys) : keys_(keys) { reset(); }

  /// Recover the plain opcode from the wire opcode WITHOUT advancing: used while the frame is still
  /// being validated (legacy advances the key only for a valid, complete frame).
  [[nodiscard]] std::uint16_t peek_decode(std::uint16_t wire_opcode) const noexcept;
  /// Advance after a valid frame was consumed.
  void commit() noexcept;
  /// peek_decode + commit (the common case).
  [[nodiscard]] std::uint16_t decode(std::uint16_t wire_opcode) noexcept;
  /// Client side: the wire opcode for `plain_opcode`; advances.
  [[nodiscard]] std::uint16_t encode(std::uint16_t plain_opcode) noexcept;
  void reset() noexcept;

  /// Mask the next packet will use, and how many packets were committed (0 = the connect packet is next).
  [[nodiscard]] std::uint16_t next_mask() const noexcept;
  [[nodiscard]] std::uint64_t committed() const noexcept { return count_; }
  /// Raw schedule state (the session key; before the first commit this is f(k0)'s successor).
  [[nodiscard]] std::uint32_t state() const noexcept { return key_; }

 private:
  [[nodiscard]] std::uint32_t step(std::uint32_t x) const noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * keys_.k[1] + keys_.k[2]);
  }
  ObfuscationKeys keys_;
  std::uint32_t key_ = 0;      // session key (valid after the connect packet)
  std::uint64_t count_ = 0;
};


// ---- definitions (inline: framing.cpp uses them and must link without obfuscation.cpp) -----------------
inline void OpcodeObfuscator::reset() noexcept {
  count_ = 0;
  key_ = step(step(keys_.k[0]));  // clif_parse_WantToConnection: sd->cryptKey
}

inline std::uint16_t OpcodeObfuscator::next_mask() const noexcept {
  if (count_ == 0) return static_cast<std::uint16_t>((step(keys_.k[0]) >> 16) & 0x7FFF);  // clif_parse, no session
  return static_cast<std::uint16_t>((key_ >> 16) & 0x7FFF);
}

inline std::uint16_t OpcodeObfuscator::peek_decode(std::uint16_t wire_opcode) const noexcept {
  return static_cast<std::uint16_t>(wire_opcode ^ next_mask());
}

inline void OpcodeObfuscator::commit() noexcept {
  // The connect packet leaves the session key as clif_parse_WantToConnection set it; later packets
  // advance it after they were accepted.
  if (count_ != 0) key_ = step(key_);
  ++count_;
}

inline std::uint16_t OpcodeObfuscator::decode(std::uint16_t wire_opcode) noexcept {
  const std::uint16_t plain = peek_decode(wire_opcode);
  commit();
  return plain;
}

inline std::uint16_t OpcodeObfuscator::encode(std::uint16_t plain_opcode) noexcept {
  const std::uint16_t wire = static_cast<std::uint16_t>(plain_opcode ^ next_mask());
  commit();
  return wire;
}

}  // namespace netproto
