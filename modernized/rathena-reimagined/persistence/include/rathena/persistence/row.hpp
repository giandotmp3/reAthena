#pragma once
// Backend-neutral row/statement types. Statements are ALWAYS parameterized ('?' placeholders);
// values are never spliced into SQL text.
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rathena::persistence {

/// Binary data (BLOB / bytea). Kept apart from text so the PostgreSQL backend can send it in
/// binary format; the bytes are never reinterpreted.
struct Blob {
	std::string bytes;
	friend bool operator==(const Blob&, const Blob&) = default;
};

using SqlValue = std::variant<std::monostate, std::int64_t, std::uint64_t, std::string, Blob>;
using Row = std::map<std::string, SqlValue, std::less<>>;

struct Statement {
	std::string sql;
	std::vector<SqlValue> params;
	friend bool operator==(const Statement&, const Statement&) = default;
};

} // namespace rathena::persistence
