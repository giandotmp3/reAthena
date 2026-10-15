// PostgresConnection when the build has no libpq: every operation throws DbConfigError without
// touching the network, and open_connection() refuses postgres:// URLs up front. See
// postgres_connection.cpp for the real one.
#include "rathena/persistence/postgres.hpp"

namespace rathena::persistence {
namespace {

[[noreturn]] void unavailable() {
	throw DbConfigError("this build has no PostgreSQL support (libpq was not found); install libpq "
	                    "(libpq-dev / postgresql-libs / Homebrew libpq) and rebuild with "
	                    "-DRATHENA_PERSISTENCE_WITH_POSTGRESQL=ON");
}

} // namespace

bool postgres_client_available() noexcept { return false; }

struct PostgresConnection::Impl {};

PostgresConnection::PostgresConnection(DbEndpoint endpoint) : endpoint_(std::move(endpoint)), impl_(std::make_unique<Impl>()) {}
PostgresConnection::~PostgresConnection() = default;

void PostgresConnection::connect() { unavailable(); }
std::vector<Row> PostgresConnection::run(const Statement&, bool) { unavailable(); }
void PostgresConnection::execute(const Statement&) { unavailable(); }
std::vector<Row> PostgresConnection::query(const Statement&) { unavailable(); }
bool PostgresConnection::execute_in_transaction(const std::vector<Statement>&) { unavailable(); }
void PostgresConnection::execute_script(std::string_view) { unavailable(); }

} // namespace rathena::persistence
