#pragma once
#include <stdexcept>
#include <string>

namespace nd {

// Thrown by scaffold stubs. Acceptance tests tagged pending expect this (or any failure).
class NotImplementedError : public std::logic_error {
 public:
  explicit NotImplementedError(const std::string& what) : std::logic_error("not implemented: " + what) {}
};

}  // namespace nd
