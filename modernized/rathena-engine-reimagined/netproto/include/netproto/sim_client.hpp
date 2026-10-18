#pragma once
// Headless client for rathena-sim: speaks codec-level packets, no sockets.
// A SimPlayer sits on top of this. Uses the same generated codecs as the
// server, so a codec bug shows up on both sides identically; the golden
// byte vectors in tests/ are the independent oracle.
#include <memory>

#include "netproto/codec_set.hpp"
#include "netproto/obfuscation.hpp"

namespace netproto {

class SimClient {
 public:
  SimClient(std::shared_ptr<const CodecSet> codecs, std::optional<ObfuscationKeys> keys)
      : codecs_(std::move(codecs)), obf_(keys ? std::optional<OpcodeObfuscator>(*keys) : std::nullopt) {}

  /// Serialize one client-to-server packet, obfuscating the opcode if keyed. Stub.
  [[nodiscard]] Bytes send(const DecodedPacket& p);
  /// Parse a server byte stream into packets. Stub.
  [[nodiscard]] std::vector<DecodedPacket> receive(ByteView stream);

 private:
  std::shared_ptr<const CodecSet> codecs_;
  std::optional<OpcodeObfuscator> obf_;
};

}  // namespace netproto
