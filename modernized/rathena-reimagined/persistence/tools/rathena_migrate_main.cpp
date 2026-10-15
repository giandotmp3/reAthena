// rathena-migrate: database setup and the one-time move off MySQL (DATABASE_DECISION.md).
//
// Usage:
//   rathena-migrate --init-target-schema
//       Creates the clean target schema (target_schema.hpp) in ${DATABASE_URL}
//       (sqlite: or postgresql:// URL).
//   rathena-migrate import --from <mysql-url> [--to <url>] [--schema-dir <dir>] [--batch-size <n>]
//                          [--baseline=<fingerprint>]... [--utf8-text-as-latin1]
//       Copies a legacy MySQL/MariaDB database into PostgreSQL or SQLite (migrate/import.hpp).
//       --to defaults to ${DATABASE_URL}; --schema-dir defaults to `sql-files` (relative to the
//       working directory). A URL argument may be written env:NAME to read it from the
//       environment variable NAME, which keeps the password out of the process list.
//
// Passwords are never printed: every URL is shown redacted.
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/migrate/import.hpp"
#include "rathena/persistence/migrate/schema.hpp"
#include "rathena/persistence/target_schema.hpp"

#if defined(RATHENA_MIGRATE_HAS_MYSQL)
#include "mysql_source.hpp"
#endif

namespace mig = rathena::persistence::migrate;
using namespace rathena::persistence;

namespace {

constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;

void usage(std::ostream& out) {
	out << "usage:\n"
	       "  rathena-migrate --init-target-schema            (uses ${DATABASE_URL})\n"
	       "  rathena-migrate import --from mysql://user:pass@host:3306/db [--to postgresql://...|sqlite:path]\n"
	       "                  [--schema-dir sql-files] [--batch-size 1000] [--baseline=<fingerprint>]...\n"
	       "                  [--utf8-text-as-latin1]\n"
	       "  A URL may be given as env:NAME to read it from the environment variable NAME.\n"
	       "  --to defaults to ${DATABASE_URL}.\n";
}

/// `env:NAME` -> ${NAME}; anything else as given.
std::optional<std::string> resolve_url(std::string_view arg) {
	if (!arg.starts_with("env:")) return std::string(arg);
	const ProcessEnvironment env;
	auto v = env.get(arg.substr(4));
	if (!v || v->empty()) return std::nullopt;
	return v;
}

std::string redacted_url(const std::string& url) {
	if (is_mysql_url(url) || is_postgres_url(url)) {
		try {
			return parse_db_url(url).redacted();
		} catch (const DbConfigError&) {
			return "(malformed URL)";
		}
	}
	return url; // sqlite: carries no credentials
}

int init_target_schema() {
	const ProcessEnvironment env;
	const auto cfg = DbConnectionConfig::from_environment(env, ServerProcess::Migrate);
	auto conn = open_connection(cfg.main_url);
	if (!create_target_schema(*conn)) {
		std::cerr << "rathena-migrate: creating the target schema failed (nothing was changed)\n";
		return kExitFailed;
	}
	std::cout << "target schema " << kTargetSchemaVersion << " is in place\n";
	return EXIT_SUCCESS;
}

int import_command(int argc, char** argv) {
	std::optional<std::string> from;
	std::optional<std::string> to;
	mig::ImportOptions opts;
	opts.known_baselines = mig::known_legacy_baselines(); // --baseline= adds to these
	for (int i = 2; i < argc; ++i) {
		const std::string_view a = argv[i];
		auto next = [&]() -> std::optional<std::string_view> {
			if (i + 1 >= argc) return std::nullopt;
			return std::string_view(argv[++i]);
		};
		if (a == "--from" || a == "--to") {
			const auto v = next();
			if (!v) {
				std::cerr << a << " needs a value\n";
				return kExitUsage;
			}
			const auto url = resolve_url(*v);
			if (!url) {
				std::cerr << "rathena-migrate: " << *v << " is not set\n";
				return kExitUsage;
			}
			(a == "--from" ? from : to) = *url;
		} else if (a == "--schema-dir") {
			const auto v = next();
			if (!v) {
				std::cerr << a << " needs a value\n";
				return kExitUsage;
			}
			opts.schema_dir = std::string(*v);
		} else if (a == "--batch-size") {
			const auto v = next();
			if (!v) {
				std::cerr << a << " needs a value\n";
				return kExitUsage;
			}
			opts.batch_size = static_cast<std::size_t>(std::strtoul(std::string(*v).c_str(), nullptr, 10));
			if (opts.batch_size == 0) {
				std::cerr << "--batch-size must be a positive number\n";
				return kExitUsage;
			}
		} else if (a.starts_with("--baseline=")) {
			opts.known_baselines.insert(std::string(a.substr(11)));
		} else if (a == "--utf8-text-as-latin1") {
			opts.utf8_as_latin1 = true;
		} else {
			std::cerr << "unknown argument: " << a << "\n";
			usage(std::cerr);
			return kExitUsage;
		}
	}
	if (!from) {
		std::cerr << "rathena-migrate import: --from mysql://... is required\n";
		return kExitUsage;
	}
	if (!to) {
		const ProcessEnvironment env;
		to = env.get("DATABASE_URL");
		if (!to || to->empty()) {
			std::cerr << "rathena-migrate import: pass --to or set DATABASE_URL\n";
			return kExitUsage;
		}
	}
	if (!is_mysql_url(*from)) {
		std::cerr << "rathena-migrate import: --from must be a mysql:// URL\n";
		return kExitUsage;
	}
	if (is_mysql_url(*to)) {
		std::cerr << "rathena-migrate import: --to must be a postgresql:// or sqlite: URL; " << kMysqlNotSupported << "\n";
		return kExitUsage;
	}

#if defined(RATHENA_MIGRATE_HAS_MYSQL)
	std::cerr << "import " << redacted_url(*from) << " -> " << redacted_url(*to) << "\n";
	rathena::migrate_tool::MysqlSource source(parse_db_url(*from));
	source.connect();
	auto target = open_connection(*to);
	target->connect();
	const auto report = mig::run_import(source, *target, opts, std::cerr);
	mig::print_summary(report, std::cout);
	return report.ok() ? EXIT_SUCCESS : kExitFailed;
#else
	std::cerr << "rathena-migrate: this build has no MySQL support, so it cannot import "
	          << redacted_url(*from) << ".\nRebuild with a MySQL or MariaDB client library installed "
	          << "(libmariadb-dev or libmysqlclient-dev) and -DRATHENA_MIGRATE_WITH_MYSQL=ON.\n";
	return kExitUsage;
#endif
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		usage(std::cerr);
		return kExitUsage;
	}
	const std::string_view cmd = argv[1];
	try {
		if (cmd == "--help" || cmd == "-h") {
			usage(std::cout);
			return EXIT_SUCCESS;
		}
		if (cmd == "--init-target-schema" && argc == 2) return init_target_schema();
		if (cmd == "import") return import_command(argc, argv);
		std::cerr << "unknown command: " << cmd << "\n";
		usage(std::cerr);
		return kExitUsage;
	} catch (const std::exception& e) {
		std::cerr << "rathena-migrate: " << e.what() << "\n";
		return kExitFailed;
	}
}
