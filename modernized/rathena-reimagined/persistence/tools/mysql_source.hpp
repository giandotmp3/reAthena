#pragma once
// LegacySource over a live MySQL/MariaDB database, for `rathena-migrate import`. This is the only
// MySQL client code left in rAthena; it is compiled into the rathena-migrate tool only, and only
// when CMake finds a client library (RATHENA_MIGRATE_WITH_MYSQL).
#include <memory>

#include "rathena/persistence/db_config.hpp"
#include "rathena/persistence/migrate/import.hpp"

namespace rathena::migrate_tool {

/// Reads over a `binary` connection character set, so text arrives exactly as stored (legacy
/// servers wrote raw EUC-KR/CP1252 bytes into latin1 columns). The whole import reads inside one
/// consistent-snapshot transaction (effective on InnoDB tables; stop the servers for MyISAM).
class MysqlSource final : public persistence::migrate::LegacySource {
public:
	explicit MysqlSource(persistence::DbEndpoint endpoint);
	~MysqlSource() override;
	MysqlSource(const MysqlSource&) = delete;
	MysqlSource& operator=(const MysqlSource&) = delete;

	void connect(); ///< throws persistence::RepositoryError; the message never contains the password
	[[nodiscard]] std::string describe() const override { return endpoint_.redacted(); }
	std::vector<persistence::migrate::SourceTable> tables() override;
	std::uint64_t row_count(const persistence::migrate::SourceTable& table) override;
	void read_rows(const persistence::migrate::SourceTable& table,
	               const std::vector<const persistence::migrate::SourceColumn*>& columns, bool utf8_as_latin1,
	               std::size_t batch_size,
	               const std::function<void(std::vector<persistence::migrate::RawRow>&)>& sink) override;

private:
	struct Impl;
	persistence::DbEndpoint endpoint_;
	std::unique_ptr<Impl> impl_;
};

} // namespace rathena::migrate_tool
