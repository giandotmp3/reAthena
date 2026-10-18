#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace engine {

// Thrown by API stubs that are scaffolded but not yet implemented.
// Acceptance tests tagged PENDING expect this (or any failure) until the
// feature lands; an unexpected pass is reported so the marker gets removed.
class NotImplemented : public std::logic_error {
public:
  explicit NotImplemented(const std::string& what)
      : std::logic_error("not implemented: " + what) {}
};

struct Point {
  int32_t x = 0;
  int32_t y = 0;
  friend bool operator==(const Point&, const Point&) = default;
};

}  // namespace engine
