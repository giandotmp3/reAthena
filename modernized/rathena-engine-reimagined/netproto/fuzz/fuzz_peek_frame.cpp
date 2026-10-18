// libFuzzer target (NETPROTO_FUZZ=ON, clang). Every decoder gets one of these;
// generated codecs add theirs under generated/fuzz/. Invariants: no crash, no
// out-of-bounds read, kComplete implies length <= size.
#include <cstddef>
#include <cstdint>

#include "netproto/framing.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace netproto;
  static const FrameTable table = [] {
    FrameTable t;
    LengthRule fixed;  fixed.fixed_size = 55;
    LengthRule var;    var.kind = LengthKind::Variable; var.header_size = 8;
    t[0x0064] = {"CA_LOGIN", fixed, Phase::PreSession};
    t[0x0b04] = {"CZ_VAR", var, Phase::InWorld};
    return t;
  }();
  const auto r = peek_frame(ByteView(data, size), table);
  if (r.status == FrameStatus::kComplete && r.length > size) __builtin_trap();
  return 0;
}
