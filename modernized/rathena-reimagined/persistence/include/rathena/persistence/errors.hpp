#pragma once
// Exception types raised by the persistence library.
#include <stdexcept>
#include <string>

namespace rathena::persistence {

/// A stub that has not been implemented yet (e.g. the MySQL backend in this scaffold).
class NotImplemented : public std::logic_error {
public:
	explicit NotImplemented(const std::string& what) : std::logic_error("not implemented: " + what) {}
};

/// A stored row cannot be represented by the domain value (out of range, wrong type, lossy).
class MappingError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

/// A repository operation failed (store unavailable, constraint violation, injected fault).
class RepositoryError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

} // namespace rathena::persistence
