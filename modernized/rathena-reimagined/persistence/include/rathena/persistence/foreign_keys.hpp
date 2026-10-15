#pragma once
// Foreign keys the clean target schema carries once the character-side repositories are ported
// (DATABASE_DECISION.md increment 3; formerly the in-place MySQL step M3b). CASCADE / SET NULL
// actions must agree with the BR-CHAR-013 cleanup plan (character_delete_plan()); see
// character_delete_test.cpp.
#include <span>
#include <string_view>

#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence {

enum class OnDelete { Cascade, SetNull, Restrict };

struct ForeignKeySpec {
	std::string_view child_table;  ///< TableNames field
	std::string_view column;
	std::string_view parent_table; ///< TableNames field
	std::string_view parent_column;
	OnDelete on_delete;
};

std::span<const ForeignKeySpec> target_foreign_keys() noexcept;

/// Resolves a TableNames field name ("character", "inventory", ...) to the configured table name.
/// Throws std::invalid_argument for an unknown field.
std::string_view resolve_table(const TableNames& names, std::string_view logical_table);

} // namespace rathena::persistence
