// libFuzzer target for CodecSet::try_decode / encode / framing / SimClient (NETPROTO_FUZZ=ON, clang).
//   clang++ -fsanitize=fuzzer,address,undefined ...   (the runtime libraries must be installed; on a
//   machine without them the deterministic mutation fuzz in tests/test_fuzz.cpp covers the same body).
#include <cstddef>
#include <cstdint>

#include "decode_invariants.hpp"
#include "generated/catalog_packets_re_20211103.hpp"
#include "netproto/sim_client.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace netproto;
  static const CodecSet set = CodecSet::build(generated::packets_re_20211103_catalog(), ClientVersion{20211103, Flavour::Re});
  static const FrameTable c2s = set.frames();
  static const FrameTable s2c = set.server_frames();
  static const std::shared_ptr<const CodecSet> shared = std::make_shared<const CodecSet>(set);
  const ByteView in(data, size);
  fuzz::check_one(set, c2s, s2c, in);
  SimClient c(shared, std::nullopt);
  (void)c.receive(in);
  return 0;
}
