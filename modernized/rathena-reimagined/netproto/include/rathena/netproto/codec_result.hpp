#pragma once
// Minimal expected-like result for codecs (std::expected is C++23 and not on
// all four CI toolchains yet).

#include <cassert>
#include <optional>
#include <string_view>
#include <utility>

namespace rathena::netproto {

enum class CodecError {
  kTruncated,            // buffer shorter than the packet
  kWrongOpcode,          // first u16 is not the expected command
  kFieldTooLong,         // a string does not fit its fixed NUL-terminated field
  kNotRepresentable,     // value cannot be encoded at the negotiated protocol version
  kUnsupportedVersion,   // protocol version unknown to this build
};

[[nodiscard]] constexpr std::string_view to_string(CodecError e) noexcept {
  switch (e) {
    case CodecError::kTruncated: return "truncated";
    case CodecError::kWrongOpcode: return "wrong-opcode";
    case CodecError::kFieldTooLong: return "field-too-long";
    case CodecError::kNotRepresentable: return "not-representable";
    case CodecError::kUnsupportedVersion: return "unsupported-version";
  }
  return "unknown";
}

template <class T>
class CodecResult {
 public:
  CodecResult(T value) : value_(std::move(value)) {}            // NOLINT(google-explicit-constructor)
  CodecResult(CodecError error) : error_(error) {}               // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }
  explicit operator bool() const noexcept { return ok(); }

  [[nodiscard]] const T& value() const& { assert(ok()); return *value_; }
  [[nodiscard]] T& value() & { assert(ok()); return *value_; }
  [[nodiscard]] T&& value() && { assert(ok()); return std::move(*value_); }
  [[nodiscard]] CodecError error() const noexcept { return error_; }

 private:
  std::optional<T> value_;
  CodecError error_{CodecError::kTruncated};
};

}  // namespace rathena::netproto
