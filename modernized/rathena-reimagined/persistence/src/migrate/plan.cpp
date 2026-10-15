#include "rathena/persistence/migrate/plan.hpp"

#include <array>

namespace rathena::persistence::migrate {
namespace {

constexpr std::array<StepInfo, 9> kPlan = {{
    {StepId::M0, "0000_m0_baseline_fingerprint", StepKind::Baseline, Gate::None,
     "Baseline by schema fingerprint; unknown drift refuses the run"},
    {StepId::M1, "0001_m1_orphan_report", StepKind::Report, Gate::None,
     "Orphan report (child rows without a parent); recurring until M3b"},
    {StepId::M6, "0006_m6_login_pass_hash", StepKind::Schema, Gate::None,
     "login.pass_hash + pass_scheme in {plain, md5, argon2id}; lazy rehash on login"},
    {StepId::M6b, "0006b_m6b_md5_shim_or_grace", StepKind::Schema, Gate::None,
     "MD5 challenge shim (opt-in, default off) or legacy-password grace deadline"},
    {StepId::M7, "0007_m7_archive_auction", StepKind::Schema, Gate::CharRefactorReachedMail,
     "Archive auction to _archive_auction, then drop it"},
    {StepId::M2, "0002_m2_innodb", StepKind::Schema, Gate::None,
     "Main-DB tables to InnoDB; logs.sql tables excluded"},
    {StepId::M4, "0004_m4_type_fixes", StepKind::Schema, Gate::None,
     "zeny INT + CHECK (zeny >= 0); item amount INT UNSIGNED; homun_id signedness"},
    {StepId::M3a, "0003a_m3a_zero_to_null", StepKind::Schema, Gate::NoLegacyWriters,
     "Convert 0-as-none references to NULL"},
    {StepId::M3b, "0003b_m3b_foreign_keys", StepKind::Schema, Gate::M3aAppliedAndOrphansClean,
     "Add foreign keys (cascade order verified against BR-CHAR-013)"},
}};

constexpr std::array<std::string_view, 10> kLogTables = {
    "atcommandlog", "branchlog", "cashlog", "chatlog", "feedinglog",
    "loginlog", "mvplog", "npclog", "picklog", "zenylog",
};

} // namespace

std::span<const StepInfo> migration_plan() noexcept { return kPlan; }

const StepInfo& step_info(StepId id) noexcept {
	for (const auto& s : kPlan)
		if (s.id == id) return s;
	return kPlan[0];
}

std::string_view to_string(StepId id) noexcept {
	switch (id) {
	case StepId::M0: return "M0";
	case StepId::M1: return "M1";
	case StepId::M6: return "M6";
	case StepId::M6b: return "M6b";
	case StepId::M7: return "M7";
	case StepId::M2: return "M2";
	case StepId::M4: return "M4";
	case StepId::M3a: return "M3a";
	case StepId::M3b: return "M3b";
	}
	return "?";
}

std::string_view to_string(Gate g) noexcept {
	switch (g) {
	case Gate::None: return "none";
	case Gate::CharRefactorReachedMail: return "char refactor reached mail";
	case Gate::NoLegacyWriters: return "no legacy binary still writes the affected tables";
	case Gate::M3aAppliedAndOrphansClean: return "M3a applied and M1 orphan report clean";
	}
	return "?";
}

std::span<const std::string_view> log_tables() noexcept { return kLogTables; }

} // namespace rathena::persistence::migrate
