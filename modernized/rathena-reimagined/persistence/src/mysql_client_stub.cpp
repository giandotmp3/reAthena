// MysqlConnection when the build has no MySQL/MariaDB client library: every operation throws
// NotImplemented, without touching the network. See mysql_client.cpp for the real one.
#include "rathena/persistence/errors.hpp"
#include "rathena/persistence/mysql.hpp"

namespace rathena::persistence {

bool mysql_client_available() noexcept { return false; }

struct MysqlConnection::Impl {};

MysqlConnection::MysqlConnection(DbEndpoint endpoint) : endpoint_(std::move(endpoint)), impl_(std::make_unique<Impl>()) {}
MysqlConnection::~MysqlConnection() = default;

void MysqlConnection::connect() {
	throw NotImplemented("MysqlConnection::connect (" + endpoint_.redacted() + "): built without a MySQL client");
}
void MysqlConnection::execute(const Statement&) { throw NotImplemented("MysqlConnection::execute"); }
std::vector<Row> MysqlConnection::query(const Statement&) { throw NotImplemented("MysqlConnection::query"); }
bool MysqlConnection::execute_in_transaction(const std::vector<Statement>&) {
	throw NotImplemented("MysqlConnection::execute_in_transaction");
}

} // namespace rathena::persistence
