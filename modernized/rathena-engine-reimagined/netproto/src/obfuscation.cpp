#include "netproto/obfuscation.hpp"

namespace netproto {
// Stub: the key schedule is generated from proto data (CAP-24).
std::uint16_t OpcodeObfuscator::decode(std::uint16_t) { not_implemented("OpcodeObfuscator::decode"); }
std::uint16_t OpcodeObfuscator::encode(std::uint16_t) { not_implemented("OpcodeObfuscator::encode"); }
}  // namespace netproto
