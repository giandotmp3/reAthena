#include "rathena/persist/connection_wrappers.hpp"

#include <variant>

#include "rathena/persist/errors.hpp"

namespace rathena::persist {

// ---- GuardedConnection --------------------------------------------------------------------
void GuardedConnection::connect() { std::scoped_lock l(mu_); inner_.connect(); }
void GuardedConnection::execute(const Statement& st) { std::scoped_lock l(mu_); inner_.execute(st); }
std::vector<Row> GuardedConnection::query(const Statement& st) { std::scoped_lock l(mu_); return inner_.query(st); }
std::uint64_t GuardedConnection::affected_rows() const noexcept { std::scoped_lock l(mu_); return inner_.affected_rows(); }
bool GuardedConnection::execute_in_transaction(const std::vector<Statement>& s) { std::scoped_lock l(mu_); return inner_.execute_in_transaction(s); }
void GuardedConnection::transaction(const std::function<void()>& body) { std::scoped_lock l(mu_); inner_.transaction(body); }
void GuardedConnection::execute_script(std::string_view sql) { std::scoped_lock l(mu_); inner_.execute_script(sql); }

// ---- FaultInjectingConnection -------------------------------------------------------------
void FaultInjectingConnection::check_sql(const std::string& sql, const std::vector<SqlValue>* params) {
	if (outage_.load()) {
		injected_.fetch_add(1);
		throw RepositoryError("injected fault: database unreachable");
	}
	std::scoped_lock l(mu_);
	if (fail_count_ > 0 && (fail_needle_.empty() || sql.find(fail_needle_) != std::string::npos)) {
		--fail_count_;
		injected_.fetch_add(1);
		if (fail_kind_ == Fault::RowRejected) throw RowRejectedError("injected fault: row rejected");
		throw RepositoryError("injected fault: statement failed");
	}
	if (!poison_param_.empty() && params != nullptr && sql.find(poison_sql_) != std::string::npos) {
		for (const auto& p : *params) {
			if (const auto* s = std::get_if<std::string>(&p); s != nullptr && *s == poison_param_) {
				injected_.fetch_add(1);
				throw RowRejectedError("injected fault: poison row");
			}
		}
	}
}

void FaultInjectingConnection::check(const Statement& st) {
	{
		std::scoped_lock l(mu_);
		log_.push_back(st.sql);
	}
	check_sql(st.sql, &st.params);
}

void FaultInjectingConnection::connect() {
	if (outage_.load()) throw RepositoryError("injected fault: database unreachable");
	inner_.connect();
}
void FaultInjectingConnection::execute(const Statement& st) { check(st); inner_.execute(st); }
std::vector<Row> FaultInjectingConnection::query(const Statement& st) { check(st); return inner_.query(st); }
bool FaultInjectingConnection::execute_in_transaction(const std::vector<Statement>& statements) {
	try {
		transaction([&] { for (const auto& st : statements) execute(st); });
	} catch (const RepositoryError&) {
		return false;
	}
	return true;
}
void FaultInjectingConnection::transaction(const std::function<void()>& body) {
	{
		std::scoped_lock l(mu_);
		log_.push_back("BEGIN");
	}
	check_sql("BEGIN", nullptr);
	inner_.transaction(body);  // body calls back into this object, so each statement is checked
}
void FaultInjectingConnection::execute_script(std::string_view sql) {
	check_sql(std::string(sql), nullptr);
	inner_.execute_script(sql);
}

void FaultInjectingConnection::fail_next(std::size_t count, Fault kind, std::string needle) {
	std::scoped_lock l(mu_);
	fail_count_ = count;
	fail_kind_ = kind;
	fail_needle_ = std::move(needle);
}
void FaultInjectingConnection::poison_param(std::string needle_sql, std::string param_text) {
	std::scoped_lock l(mu_);
	poison_sql_ = std::move(needle_sql);
	poison_param_ = std::move(param_text);
}
void FaultInjectingConnection::clear_faults() {
	outage_.store(false);
	std::scoped_lock l(mu_);
	fail_count_ = 0;
	poison_param_.clear();
	poison_sql_.clear();
}
std::vector<std::string> FaultInjectingConnection::log() const { std::scoped_lock l(mu_); return log_; }
void FaultInjectingConnection::clear_log() { std::scoped_lock l(mu_); log_.clear(); }

} // namespace rathena::persist
