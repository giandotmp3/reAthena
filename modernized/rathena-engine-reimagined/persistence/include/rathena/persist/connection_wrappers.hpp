#pragma once
// Decorators over SqlConnection.
//  * GuardedConnection: serializes every call (and whole transactions) behind one recursive
//    mutex so the write-behind thread and the sim thread can share one connection.
//  * FaultInjectingConnection: failure-injection seam for tests and drills (outage, N-th
//    statement fails, poison rows, statement log).
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "rathena/persist/sql_connection.hpp"

namespace rathena::persist {

class GuardedConnection final : public SqlConnection {
public:
	explicit GuardedConnection(SqlConnection& inner) : inner_(inner) {}
	[[nodiscard]] SqlDialect dialect() const noexcept override { return inner_.dialect(); }
	void connect() override;
	void execute(const Statement& st) override;
	std::vector<Row> query(const Statement& st) override;
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override;
	bool execute_in_transaction(const std::vector<Statement>& statements) override;
	void transaction(const std::function<void()>& body) override;
	void execute_script(std::string_view sql) override;
private:
	SqlConnection& inner_;
	mutable std::recursive_mutex mu_;
};

enum class Fault : std::uint8_t {
	Outage,      ///< RepositoryError: backend unreachable / IO error
	RowRejected, ///< RowRejectedError: this statement's data is refused (poison row)
};

class FaultInjectingConnection final : public SqlConnection {
public:
	explicit FaultInjectingConnection(SqlConnection& inner) : inner_(inner) {}
	[[nodiscard]] SqlDialect dialect() const noexcept override { return inner_.dialect(); }
	void connect() override;
	void execute(const Statement& st) override;
	std::vector<Row> query(const Statement& st) override;
	[[nodiscard]] std::uint64_t affected_rows() const noexcept override { return inner_.affected_rows(); }
	bool execute_in_transaction(const std::vector<Statement>& statements) override;
	void transaction(const std::function<void()>& body) override;
	void execute_script(std::string_view sql) override;

	/// While on, every call throws RepositoryError (database down).
	void set_outage(bool on) { outage_.store(on); }
	/// The next `count` statements whose SQL contains `needle` (empty = any) fail with `kind`.
	void fail_next(std::size_t count, Fault kind = Fault::Outage, std::string needle = {});
	/// Statements that contain `needle` and carry this text parameter fail permanently as a
	/// poison row until cleared.
	void poison_param(std::string needle_sql, std::string param_text);
	void clear_faults();
	[[nodiscard]] std::vector<std::string> log() const;
	void clear_log();
	[[nodiscard]] std::size_t injected() const { return injected_.load(); }
private:
	void check(const Statement& st);
	void check_sql(const std::string& sql, const std::vector<SqlValue>* params);
	SqlConnection& inner_;
	std::atomic<bool> outage_{false};
	std::atomic<std::size_t> injected_{0};
	mutable std::mutex mu_;
	std::size_t fail_count_{0};
	Fault fail_kind_{Fault::Outage};
	std::string fail_needle_;
	std::string poison_sql_, poison_param_;
	std::vector<std::string> log_;
};

} // namespace rathena::persist
