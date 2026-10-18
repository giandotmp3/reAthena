#pragma once
// The invariants every decoder input must keep, shared by the libFuzzer target (fuzz_decode.cpp, clang
// -fsanitize=fuzzer) and the deterministic mutation fuzz in tests/test_fuzz.cpp (any compiler, ASan+UBSan).
//   1. try_decode never crashes, never reads out of bounds, whatever the bytes (the sanitizers watch).
//   2. A frame the decoder accepts re-encodes (or is refused as non-canonical, kBadValue) and the
//      re-encoded frame decodes to the same packet: decode o encode is the identity on decoded packets.
//   3. peek_frame and the codec agree: kComplete with length n means the first n bytes are exactly the
//      frame the codec accepts or rejects with a typed error; never a different length.
//   4. SimClient::receive on the same bytes never throws and never delivers more bytes than it was given.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <variant>

#include "netproto/codec_set.hpp"
#include "netproto/framing.hpp"

namespace netproto::fuzz {

inline bool has_nan(const DecodedPacket& p) {
  auto bad = [](const std::map<std::string, FieldValue>& m) {
    for (const auto& kv : m)
      if (const double* d = std::get_if<double>(&kv.second); d && std::isnan(*d)) return true;
    return false;
  };
  if (bad(p.fields)) return true;
  for (const auto& r : p.repeats) if (bad(r)) return true;
  return false;
}

[[noreturn]] inline void violated(const char* what) {
  std::fprintf(stderr, "decode invariant violated: %s\n", what);
  std::abort();
}

inline void check_one(const CodecSet& set, const FrameTable& c2s, const FrameTable& s2c, ByteView data) {
  for (const Direction d : {Direction::ClientToServer, Direction::ServerToClient}) {
    DecodedPacket p;
    const CodecErrc e = set.try_decode(data, d, p);
    const FrameTable& t = d == Direction::ClientToServer ? c2s : s2c;
    const FrameResult fr = peek_frame(data, t);
    if (e == CodecErrc::kOk) {
      if (fr.status != FrameStatus::kComplete || fr.length != data.size()) violated("decoded a frame that peek_frame does not call complete");
      if (has_nan(p)) continue;
      Bytes again;
      try {
        again = set.encode(p);
      } catch (const CodecError& ce) {
        if (ce.code != CodecErrc::kBadValue) violated("re-encode failed with an unexpected error");
        continue;  // non-canonical input (e.g. an unterminated fixed string): refused, not mangled
      }
      DecodedPacket q;
      if (set.try_decode(again, d, q) != CodecErrc::kOk) violated("re-encoded frame does not decode");
      if (!(q.name == p.name && q.fields == p.fields && q.repeats == p.repeats)) violated("decode(encode(x)) != x");
    } else if (fr.status == FrameStatus::kComplete && fr.length == data.size()) {
      // a frame of the right length that the codec still refuses: only for reasons the framing cannot see
      if (e != CodecErrc::kBadLength && e != CodecErrc::kRepeatMismatch && e != CodecErrc::kFieldOutOfBounds && e != CodecErrc::kLengthMismatch)
        violated("complete frame refused with an unexpected error");
    }
  }
}

}  // namespace netproto::fuzz
