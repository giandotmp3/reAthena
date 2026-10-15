#pragma once
// rathena-migrate core: runs migration_plan() against a MigrationTarget.
//
// Semantics:
//  - Forward-only: no down migrations. A schema_migrations row the tool does not know, or a
//    recorded history that is not a prefix-consistent subset of the plan, refuses the run.
//  - Idempotent: steps already recorded are skipped.
//  - Ordered: steps run in plan order; the first step whose gate is closed blocks every later
//    step (reported NotReached).
//  - M0 refuses the whole run if the live schema's fingerprint is not a known baseline.
//  - Dry run (the default) computes statements and reports WouldApply without executing.
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "rathena/persistence/entities.hpp"
#include "rathena/persistence/migrate/plan.hpp"
#include "rathena/persistence/migrate/schema.hpp"
#include "rathena/persistence/row.hpp"
#include "rathena/persistence/table_names.hpp"

namespace rathena::persistence::migrate {

struct OrphanFinding {
	std::string child_table;
	std::string column;
	std::string parent_table;
	std::uint64_t count = 0;
};

struct OrphanReport {
	std::vector<OrphanFinding> findings;
	[[nodiscard]] bool clean() const noexcept;
};

/// The database being migrated. MySQL implementation pending; tests use a fake.
class MigrationTarget {
public:
	virtual ~MigrationTarget() = default;
	virtual SchemaSnapshot inspect_schema() = 0;
	/// Versions recorded in schema_migrations (empty if the table does not exist yet).
	virtual std::vector<std::string> applied_versions() = 0;
	virtual OrphanReport orphan_report() = 0;
	/// Executes `statements` and records `version` in schema_migrations atomically where the
	/// engine allows it. Throws on failure (the step is then not recorded).
	virtual void apply(std::string_view version, const std::vector<Statement>& statements) = 0;
};

struct MigrationOptions {
	bool dry_run = true;
	/// Fingerprints (schema_fingerprint()) of known legacy baselines. Phase E generates this list
	/// from sql-files/upgrades/; an empty list means every database is "unknown drift".
	std::set<std::string> known_baselines;
	/// M6: scheme of the existing `login.user_pass` values.
	PassScheme legacy_pass_scheme = PassScheme::Plain;
	/// M6b: MD5 challenge shim (BR-LOGIN-003) -- opt-in, default OFF.
	bool md5_challenge_shim = false;
	/// M6b with the shim off: accounts still on plain/md5 are disabled after this many days and
	/// need an operator/GM reset.
	std::uint32_t legacy_password_grace_days = 90;
	/// Operator-asserted gates (never inferred).
	bool gate_char_refactor_reached_mail = false; ///< M7
	bool gate_no_legacy_writers = false;          ///< M3a
	TableNames tables{};
};

enum class StepOutcome { Applied, WouldApply, AlreadyApplied, ReportOnly, BlockedByGate, Refused, NotReached };

std::string_view to_string(StepOutcome o) noexcept;

struct StepResult {
	StepId id;
	StepOutcome outcome;
	std::string detail;
	std::vector<Statement> statements; ///< what was (or would be) executed
};

struct MigrationRun {
	std::vector<StepResult> steps;
	bool refused = false;
	std::string refusal_reason;
	OrphanReport orphans;
	[[nodiscard]] const StepResult* find(StepId id) const noexcept;
};

/// A `0`-as-none reference column converted to NULL by M3a.
struct NullableRef {
	std::string_view logical_table; ///< TableNames field
	std::string_view column;
};
std::span<const NullableRef> m3a_nullable_refs() noexcept;

enum class OnDelete { Cascade, SetNull, Restrict };

/// A foreign key added by M3b. CASCADE/SET NULL actions must agree with the BR-CHAR-013 cleanup
/// plan (character_delete_plan()); see character_delete_test.cpp.
struct ForeignKeySpec {
	std::string_view child_table;  ///< TableNames field
	std::string_view column;
	std::string_view parent_table; ///< TableNames field
	std::string_view parent_column;
	OnDelete on_delete;
};
std::span<const ForeignKeySpec> m3b_foreign_keys() noexcept;

/// Resolves a TableNames field name ("character", "inventory", ...) to the configured table name.
std::string_view resolve_table(const TableNames& names, std::string_view logical_table);

class Migrator {
public:
	Migrator(MigrationTarget& target, MigrationOptions options)
	    : target_(target), options_(std::move(options)) {}

	MigrationRun run();

	/// The statements a schema step issues for `schema` (pure; used by run() and dry runs).
	static std::vector<Statement> statements_for(StepId id, const MigrationOptions& options,
	                                             const SchemaSnapshot& schema);

private:
	MigrationTarget& target_;
	MigrationOptions options_;
};

} // namespace rathena::persistence::migrate
