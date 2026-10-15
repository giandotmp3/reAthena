#pragma once
// Migration plan of architecture §5, as ordered, forward-only, idempotent steps.
// M5 (clan/elemental table-name defaults, D2) is a code fix in TableNames, not a schema step.
#include <span>
#include <string_view>

namespace rathena::persistence::migrate {

enum class StepId { M0, M1, M6, M6b, M7, M2, M4, M3a, M3b };

enum class StepKind {
	Baseline, ///< M0: fingerprint check, records the baseline
	Report,   ///< M1: read-only, re-run every time (scheduled integrity check until M3b)
	Schema,   ///< DDL/DML, recorded in schema_migrations
};

enum class Gate {
	None,
	CharRefactorReachedMail, ///< M7: phase-3 char refactor has reached mail
	NoLegacyWriters,         ///< M3a: map direct-write paths are on persistence/
	M3aAppliedAndOrphansClean, ///< M3b: M3a applied AND the M1 orphan report is clean
};

struct StepInfo {
	StepId id;
	std::string_view version; ///< row recorded in schema_migrations
	StepKind kind;
	Gate gate;
	std::string_view description;
};

/// The steps in execution order: M0, M1, M6, M6b, M7, M2, M4, M3a, M3b.
std::span<const StepInfo> migration_plan() noexcept;
const StepInfo& step_info(StepId id) noexcept;
std::string_view to_string(StepId id) noexcept;
std::string_view to_string(Gate g) noexcept;

/// Tables of sql-files/logs.sql. They may live in a separate DB (${LOG_DB_URL}) and are
/// excluded from M2.
std::span<const std::string_view> log_tables() noexcept;

} // namespace rathena::persistence::migrate
