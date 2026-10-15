#pragma once
// Backend-neutral row/statement types. Statements are ALWAYS parameterized ('?' placeholders);
// values are never spliced into SQL text.
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rathena::persistence {

using SqlValue = std::variant<std::monostate, std::int64_t, std::uint64_t, std::string>;
using Row = std::map<std::string, SqlValue, std::less<>>;

struct Statement {
	std::string sql;
	std::vector<SqlValue> params;
	friend bool operator==(const Statement&, const Statement&) = default;
};

} // namespace rathena::persistence
