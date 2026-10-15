#include "rathena/persistence/migrate/migrator.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <stdexcept>
#include <string>

#include "rathena/persistence/item_mapping.hpp"

namespace rathena::persistence::migrate {
namespace {

using F = std::string TableNames::*;
constexpr std::array<std::pair<std::string_view, F>, 32> kFields = {{
    {"login", &TableNames::login},
    {"global_acc_reg_num", &TableNames::global_acc_reg_num},
    {"global_acc_reg_str", &TableNames::global_acc_reg_str},
    {"acc_reg_num", &TableNames::acc_reg_num},
    {"acc_reg_str", &TableNames::acc_reg_str},
    {"character", &TableNames::character},
    {"char_reg_num", &TableNames::char_reg_num},
    {"char_reg_str", &TableNames::char_reg_str},
    {"inventory", &TableNames::inventory},
    {"cart_inventory", &TableNames::cart_inventory},
    {"storage", &TableNames::storage},
    {"guild_storage", &TableNames::guild_storage},
    {"mail", &TableNames::mail},
    {"mail_attachments", &TableNames::mail_attachments},
    {"party", &TableNames::party},
    {"guild", &TableNames::guild},
    {"guild_member", &TableNames::guild_member},
    {"clan", &TableNames::clan},
    {"clan_alliance", &TableNames::clan_alliance},
    {"elemental", &TableNames::elemental},
    {"homunculus", &TableNames::homunculus},
    {"mercenary_owner", &TableNames::mercenary_owner},
    {"pet", &TableNames::pet},
    {"friends", &TableNames::friends},
    {"hotkey", &TableNames::hotkey},
    {"memo", &TableNames::memo},
    {"skill", &TableNames::skill},
    {"sc_data", &TableNames::sc_data},
    {"bonus_script", &TableNames::bonus_script},
    {"quest", &TableNames::quest},
    {"achievement", &TableNames::achievement},
    {"schema_migrations", &TableNames::schema_migrations},
}};

constexpr std::array<NullableRef, 11> kNullableRefs = {{
    {"character", "party_id"},  {"character", "guild_id"},     {"character", "pet_id"},
    {"character", "homun_id"},  {"character", "elemental_id"}, {"character", "clan_id"},
    {"character", "partner_id"},{"character", "father"},       {"character", "mother"},
    {"character", "child"},     {"mail", "send_id"},
}};

// Children of `char` that BR-CHAR-013 deletes -> CASCADE; references it clears -> SET NULL.
// Account deletion (BR-LOGIN-037) removes only the login row and `##` regs, so `char` and
// account storage RESTRICT on login rather than cascading a behavior change.
constexpr std::array<ForeignKeySpec, 31> kForeignKeys = {{
    {"global_acc_reg_num", "account_id", "login", "account_id", OnDelete::Cascade},
    {"global_acc_reg_str", "account_id", "login", "account_id", OnDelete::Cascade},
    {"character", "account_id", "login", "account_id", OnDelete::Restrict},
    {"storage", "account_id", "login", "account_id", OnDelete::Restrict},
    {"character", "party_id", "party", "party_id", OnDelete::SetNull},
    {"character", "guild_id", "guild", "guild_id", OnDelete::SetNull},
    {"character", "clan_id", "clan", "clan_id", OnDelete::SetNull},
    {"character", "partner_id", "character", "char_id", OnDelete::SetNull},
    {"character", "father", "character", "char_id", OnDelete::SetNull},
    {"character", "mother", "character", "char_id", OnDelete::SetNull},
    {"character", "child", "character", "char_id", OnDelete::SetNull},
    {"inventory", "char_id", "character", "char_id", OnDelete::Cascade},
    {"cart_inventory", "char_id", "character", "char_id", OnDelete::Cascade},
    {"skill", "char_id", "character", "char_id", OnDelete::Cascade},
    {"memo", "char_id", "character", "char_id", OnDelete::Cascade},
    {"hotkey", "char_id", "character", "char_id", OnDelete::Cascade},
    {"char_reg_num", "char_id", "character", "char_id", OnDelete::Cascade},
    {"char_reg_str", "char_id", "character", "char_id", OnDelete::Cascade},
    {"sc_data", "char_id", "character", "char_id", OnDelete::Cascade},
    {"bonus_script", "char_id", "character", "char_id", OnDelete::Cascade},
    {"quest", "char_id", "character", "char_id", OnDelete::Cascade},
    {"achievement", "char_id", "character", "char_id", OnDelete::Cascade},
    {"friends", "char_id", "character", "char_id", OnDelete::Cascade},
    {"friends", "friend_id", "character", "char_id", OnDelete::Cascade},
    {"homunculus", "char_id", "character", "char_id", OnDelete::Cascade},
    {"elemental", "char_id", "character", "char_id", OnDelete::Cascade},
    {"mercenary_owner", "char_id", "character", "char_id", OnDelete::Cascade},
    {"guild_member", "char_id", "character", "char_id", OnDelete::Cascade},
    // The guild master row RESTRICTs: the plan must break the guild before deleting the char.
    {"guild", "char_id", "character", "char_id", OnDelete::Restrict},
    {"mail", "dest_id", "character", "char_id", OnDelete::Cascade},
    {"mail", "send_id", "character", "char_id", OnDelete::SetNull},
}};

std::string q(std::string_view id) { return quote_identifier(id); }

std::string lower(std::string_view s) {
	std::string out(s);
	for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

std::string_view on_delete_sql(OnDelete o) {
	switch (o) {
	case OnDelete::Cascade: return "CASCADE";
	case OnDelete::SetNull: return "SET NULL";
	case OnDelete::Restrict: return "RESTRICT";
	}
	return "RESTRICT";
}

std::string_view pass_scheme_sql(PassScheme s) {
	switch (s) {
	case PassScheme::Plain: return "plain";
	case PassScheme::Md5: return "md5";
	case PassScheme::Argon2id: break;
	}
	throw std::invalid_argument("legacy user_pass values cannot already be argon2id");
}

} // namespace

bool OrphanReport::clean() const noexcept {
	return std::all_of(findings.begin(), findings.end(), [](const OrphanFinding& f) { return f.count == 0; });
}

std::string_view to_string(StepOutcome o) noexcept {
	switch (o) {
	case StepOutcome::Applied: return "applied";
	case StepOutcome::WouldApply: return "would apply";
	case StepOutcome::AlreadyApplied: return "already applied";
	case StepOutcome::ReportOnly: return "report";
	case StepOutcome::BlockedByGate: return "blocked by gate";
	case StepOutcome::Refused: return "refused";
	case StepOutcome::NotReached: return "not reached";
	}
	return "?";
}

const StepResult* MigrationRun::find(StepId id) const noexcept {
	for (const auto& s : steps)
		if (s.id == id) return &s;
	return nullptr;
}

std::span<const NullableRef> m3a_nullable_refs() noexcept { return kNullableRefs; }
std::span<const ForeignKeySpec> m3b_foreign_keys() noexcept { return kForeignKeys; }

std::string_view resolve_table(const TableNames& names, std::string_view logical_table) {
	for (const auto& [k, f] : kFields)
		if (k == logical_table) return names.*f;
	throw std::invalid_argument("unknown logical table: " + std::string(logical_table));
}

std::vector<Statement> Migrator::statements_for(StepId id, const MigrationOptions& o, const SchemaSnapshot& schema) {
	const TableNames& t = o.tables;
	std::vector<Statement> out;
	const auto has = [&](std::string_view logical) { return schema.find(resolve_table(t, logical)) != nullptr; };

	switch (id) {
	case StepId::M0:
		out.push_back({"CREATE TABLE IF NOT EXISTS " + q(t.schema_migrations) +
		                   " (`version` VARCHAR(64) NOT NULL PRIMARY KEY, `applied_at` DATETIME NOT NULL)",
		               {}});
		break;
	case StepId::M1:
		break; // read-only report
	case StepId::M6:
		out.push_back({"ALTER TABLE " + q(t.login) + " ADD COLUMN `pass_hash` VARCHAR(255) NULL," +
		                   " ADD COLUMN `pass_scheme` ENUM('plain','md5','argon2id') NOT NULL DEFAULT 'plain'",
		               {}});
		out.push_back({"UPDATE " + q(t.login) + " SET `pass_hash` = `user_pass`, `pass_scheme` = ? WHERE `pass_hash` IS NULL",
		               {std::string(pass_scheme_sql(o.legacy_pass_scheme))}});
		break;
	case StepId::M6b:
		if (o.md5_challenge_shim) {
			// Documented residual risk: a plaintext-equivalent challenge secret is retained.
			out.push_back({"ALTER TABLE " + q(t.login) + " ADD COLUMN `challenge_secret` VARCHAR(32) NULL", {}});
		} else {
			out.push_back({"ALTER TABLE " + q(t.login) + " ADD COLUMN `legacy_pass_deadline` INT UNSIGNED NOT NULL DEFAULT 0", {}});
			// Server ('S') accounts are excluded: their credential comes from ${INTER_SERVER_PASS}.
			out.push_back({"UPDATE " + q(t.login) + " SET `legacy_pass_deadline` = UNIX_TIMESTAMP() + ?" +
			                   " WHERE `pass_scheme` <> 'argon2id' AND `sex` <> 'S'",
			               {std::uint64_t{o.legacy_password_grace_days} * 86400u}});
		}
		break;
	case StepId::M7:
		if (schema.find("auction")) {
			out.push_back({"CREATE TABLE IF NOT EXISTS `_archive_auction` LIKE `auction`", {}});
			out.push_back({"INSERT INTO `_archive_auction` SELECT * FROM `auction`", {}});
			out.push_back({"DROP TABLE `auction`", {}});
		}
		break;
	case StepId::M2: {
		std::vector<std::string> names;
		for (const auto& tbl : schema.tables) {
			const auto lname = lower(tbl.name);
			const auto logs = log_tables();
			if (std::find(logs.begin(), logs.end(), lname) != logs.end()) continue;
			if (lower(tbl.engine) == "innodb") continue;
			names.push_back(tbl.name);
		}
		std::sort(names.begin(), names.end());
		for (const auto& n : names) out.push_back({"ALTER TABLE " + q(n) + " ENGINE=InnoDB", {}});
		break;
	}
	case StepId::M4:
		if (has("character"))
			out.push_back({"ALTER TABLE " + q(t.character) + " MODIFY `zeny` INT NOT NULL DEFAULT 0," +
			                   " ADD CONSTRAINT `chk_char_zeny` CHECK (`zeny` >= 0)",
			               {}});
		for (const auto it : all_item_tables()) {
			const auto& name = std::string(resolve_table(t, to_string(it)));
			if (schema.find(name))
				out.push_back({"ALTER TABLE " + q(name) + " MODIFY `amount` INT UNSIGNED NOT NULL DEFAULT 0", {}});
		}
		if (has("homunculus"))
			out.push_back({"ALTER TABLE " + q(t.homunculus) + " MODIFY `homun_id` INT UNSIGNED NOT NULL AUTO_INCREMENT", {}});
		break;
	case StepId::M3a:
		for (const auto& r : kNullableRefs) {
			if (!has(r.logical_table)) continue;
			const auto tbl = q(resolve_table(t, r.logical_table));
			out.push_back({"ALTER TABLE " + tbl + " MODIFY " + q(r.column) + " INT UNSIGNED NULL DEFAULT NULL", {}});
			out.push_back({"UPDATE " + tbl + " SET " + q(r.column) + " = NULL WHERE " + q(r.column) + " = 0", {}});
		}
		break;
	case StepId::M3b:
		for (const auto& fk : kForeignKeys) {
			if (!has(fk.child_table) || !has(fk.parent_table)) continue;
			const auto child = resolve_table(t, fk.child_table);
			out.push_back({"ALTER TABLE " + q(child) + " ADD CONSTRAINT " +
			                   q("fk_" + std::string(child) + "_" + std::string(fk.column)) + " FOREIGN KEY (" +
			                   q(fk.column) + ") REFERENCES " + q(resolve_table(t, fk.parent_table)) + " (" +
			                   q(fk.parent_column) + ") ON DELETE " + std::string(on_delete_sql(fk.on_delete)),
			               {}});
		}
		break;
	}
	return out;
}

MigrationRun Migrator::run() {
	MigrationRun run;
	const auto plan = migration_plan();

	// Forward-only history check: every recorded version is known, and the recorded schema steps
	// form a consistent prefix (a step is only recorded if every earlier schema step is).
	std::set<std::string> applied;
	for (auto& v : target_.applied_versions()) applied.insert(std::move(v));
	for (const auto& v : applied) {
		const bool known = std::any_of(plan.begin(), plan.end(), [&](const StepInfo& s) { return s.version == v; });
		if (!known) {
			run.refused = true;
			run.refusal_reason = "schema_migrations contains unknown version '" + v + "' (database is newer than this tool)";
		}
	}
	if (!run.refused) {
		bool gap = false;
		for (const auto& s : plan) {
			if (s.kind == StepKind::Report) continue;
			const bool is_applied = applied.contains(std::string(s.version));
			if (is_applied && gap) {
				run.refused = true;
				run.refusal_reason = "schema_migrations is out of order: " + std::string(to_string(s.id)) +
				                     " is recorded but an earlier step is not";
				break;
			}
			if (!is_applied) gap = true;
		}
	}
	if (run.refused) {
		for (const auto& s : plan) run.steps.push_back({s.id, StepOutcome::NotReached, {}, {}});
		return run;
	}

	const SchemaSnapshot schema = target_.inspect_schema();
	bool stop = false;
	bool m3a_done = applied.contains(std::string(step_info(StepId::M3a).version));

	for (const auto& s : plan) {
		StepResult r{s.id, StepOutcome::NotReached, {}, {}};
		if (stop) {
			run.steps.push_back(std::move(r));
			continue;
		}
		const std::string version(s.version);

		if (s.kind == StepKind::Report) {
			run.orphans = target_.orphan_report();
			r.outcome = StepOutcome::ReportOnly;
			r.detail = run.orphans.clean() ? "no orphans" : std::to_string(run.orphans.findings.size()) + " orphan finding(s)";
			run.steps.push_back(std::move(r));
			continue;
		}
		if (applied.contains(version)) {
			r.outcome = StepOutcome::AlreadyApplied;
			run.steps.push_back(std::move(r));
			continue;
		}
		if (s.kind == StepKind::Baseline) {
			const auto fp = schema_fingerprint(schema);
			if (!options_.known_baselines.contains(fp)) {
				r.outcome = StepOutcome::Refused;
				r.detail = "unknown schema drift: " + fp;
				run.refused = true;
				run.refusal_reason = r.detail;
				stop = true;
				run.steps.push_back(std::move(r));
				continue;
			}
			r.detail = "baseline " + fp;
		}

		bool open = true;
		switch (s.gate) {
		case Gate::None: break;
		case Gate::CharRefactorReachedMail: open = options_.gate_char_refactor_reached_mail; break;
		case Gate::NoLegacyWriters: open = options_.gate_no_legacy_writers; break;
		case Gate::M3aAppliedAndOrphansClean: open = m3a_done && run.orphans.clean(); break;
		}
		if (!open) {
			r.outcome = StepOutcome::BlockedByGate;
			r.detail = "gate closed: " + std::string(to_string(s.gate));
			stop = true;
			run.steps.push_back(std::move(r));
			continue;
		}

		try {
			r.statements = statements_for(s.id, options_, schema);
			if (options_.dry_run) {
				r.outcome = StepOutcome::WouldApply;
			} else {
				target_.apply(version, r.statements);
				r.outcome = StepOutcome::Applied;
			}
		} catch (const std::exception& e) {
			r.outcome = StepOutcome::Refused;
			r.detail = e.what();
			run.refused = true;
			run.refusal_reason = std::string(to_string(s.id)) + " failed: " + e.what();
			stop = true;
		}
		if (s.id == StepId::M3a && (r.outcome == StepOutcome::Applied || r.outcome == StepOutcome::WouldApply))
			m3a_done = true;
		run.steps.push_back(std::move(r));
	}
	return run;
}

} // namespace rathena::persistence::migrate
