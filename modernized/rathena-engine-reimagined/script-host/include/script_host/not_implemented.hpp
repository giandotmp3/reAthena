#pragma once
#include <stdexcept>
#include <string>

namespace script_host {

struct NotImplemented : std::logic_error {
  explicit NotImplemented(const std::string& what) : std::logic_error("not implemented: " + what) {}
};

[[noreturn]] inline void not_implemented(const char* what) { throw NotImplemented(what); }

// Typed load-time failure (ordering conflicts, duplicate replace, bad schema).
struct LoadError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

}  // namespace script_host
