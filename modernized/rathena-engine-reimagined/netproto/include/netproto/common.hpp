#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace netproto {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

/// Thrown by stubbed entry points. Pending acceptance tests expect it.
struct NotImplementedError : std::logic_error {
  using std::logic_error::logic_error;
};

/// Millisecond clock injected by the caller (virtual clock in tests).
using Millis = std::int64_t;

[[noreturn]] void not_implemented(const char* what);

}  // namespace netproto
