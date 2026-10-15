// rathena-migrate: forward-only, idempotent schema migration tool (architecture §5).
//
// Connection details come from the environment ONLY (${DATABASE_URL}); there are no credential
// flags and no config-file defaults. Dry run is the default; --apply executes.
//
// Usage:
//   rathena-migrate [--plan] [--apply] [--md5-challenge-shim] [--legacy-pass-scheme=plain|md5]
//                   [--grace-days=N] [--gate-char-reached-mail] [--gate-no-legacy-writers]
//                   [--baseline=<fingerprint>]...
//
// The unmodified legacy baselines (main.sql, main.sql + web.sql at d4b8e7b) are built in;
// --baseline accepts an additional, operator-verified fingerprint.
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/migrator.hpp"
#include "rathena/persistence/migrate/mysql_target.hpp"
#include "rathena/persistence/mysql.hpp"

namespace mig = rathena::persistence::migrate;
using namespace rathena::persistence;

namespace {

void print_plan() {
	for (const auto& s : mig::migration_plan())
		std::cout << mig::to_string(s.id) << "\t" << s.version << "\tgate: " << mig::to_string(s.gate) << "\n\t"
		          << s.description << "\n";
}

} // namespace

int main(int argc, char** argv) {
	mig::MigrationOptions opts;
	opts.known_baselines = mig::known_legacy_baselines(); // --baseline= adds to these
	for (int i = 1; i < argc; ++i) {
		const std::string_view a = argv[i];
		if (a == "--plan") {
			print_plan();
			return EXIT_SUCCESS;
		} else if (a == "--apply") {
			opts.dry_run = false;
		} else if (a == "--md5-challenge-shim") {
			opts.md5_challenge_shim = true;
		} else if (a == "--legacy-pass-scheme=md5") {
			opts.legacy_pass_scheme = PassScheme::Md5;
		} else if (a == "--legacy-pass-scheme=plain") {
			opts.legacy_pass_scheme = PassScheme::Plain;
		} else if (a.starts_with("--grace-days=")) {
			opts.legacy_password_grace_days = static_cast<std::uint32_t>(std::stoul(std::string(a.substr(13))));
		} else if (a == "--gate-char-reached-mail") {
			opts.gate_char_refactor_reached_mail = true;
		} else if (a == "--gate-no-legacy-writers") {
			opts.gate_no_legacy_writers = true;
		} else if (a.starts_with("--baseline=")) {
			opts.known_baselines.insert(std::string(a.substr(11)));
		} else {
			std::cerr << "unknown argument: " << a << "\n";
			return 2;
		}
	}

	try {
		const ProcessEnvironment env;
		const auto cfg = DbConnectionConfig::from_environment(env, ServerProcess::Migrate);
		std::cerr << "target: " << cfg.main.redacted() << (opts.dry_run ? " (dry run)" : "") << "\n";
		MysqlConnection conn(cfg.main);
		conn.connect();
		mig::MysqlMigrationTarget target(conn, opts.tables);
		const auto run = mig::Migrator(target, opts).run();
		for (const auto& s : run.steps)
			std::cout << mig::to_string(s.id) << ": " << mig::to_string(s.outcome)
			          << (s.detail.empty() ? "" : " -- " + s.detail) << "\n";
		if (run.refused) {
			std::cerr << "refused: " << run.refusal_reason << "\n";
			return 1;
		}
		return EXIT_SUCCESS;
	} catch (const std::exception& e) {
		std::cerr << "rathena-migrate: " << e.what() << "\n";
		return 1;
	}
}
