#pragma once
// Rolling opcode obfuscation (spec 4.1). Decoding is STATEFUL: each call
// advances the per-session key, so a decoder must see every frame in order.
// The three key words come from the per-(date, flavour) proto data, never from
// a literal in code or config defaults.
#include <array>
#include <cstdint>

#include "netproto/common.hpp"

namespace netproto {

struct ObfuscationKeys {
  std::array<std::uint32_t, 3> k{};  // seed, multiplier, addend
  bool operator==(const ObfuscationKeys&) const = default;
};

class OpcodeObfuscator {
 public:
  explicit OpcodeObfuscator(ObfuscationKeys keys) : keys_(keys), state_(keys.k[0]) {}
  /// Server side: recover the plain opcode from the wire opcode; advances state.
  [[nodiscard]] std::uint16_t decode(std::uint16_t wire_opcode);
  /// Client side (sim client): produce the wire opcode; advances state.
  [[nodiscard]] std::uint16_t encode(std::uint16_t plain_opcode);
  void reset() noexcept { state_ = keys_.k[0]; }
  [[nodiscard]] std::uint32_t state() const noexcept { return state_; }

 private:
  ObfuscationKeys keys_;
  std::uint32_t state_;
};

}  // namespace netproto
