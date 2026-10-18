#pragma once
// Shared fixtures for the service tests: a thread-safe SQLite stack with fault injection, scratch
// directories and small builders. Fake same-shape values only.
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "rathena/persist/connection_wrappers.hpp"
#include "rathena/persist/sqlite_connection.hpp"
#include "rathena/persist/writer.hpp"
#include "support.hpp"

namespace rathena::persist::testing_support {

inline std::filesystem::path scratch_dir(const std::string& name) {
	auto p = std::filesystem::temp_directory_path() / ("rathena_persist_" + std::to_string(::getpid())) / name;
	std::filesystem::remove_all(p);
	std::filesystem::create_directories(p);
	return p;
}

/// connection stack: SqliteConnection -> GuardedConnection (mutex) -> FaultInjectingConnection.
/// Stores and services are given `db`, the outermost layer; `raw_view` bypasses fault injection.
struct Stack {
	explicit Stack(const std::string& path = ":memory:") : sqlite(path), guarded(sqlite), db(guarded) { db.connect(); }
	SqliteConnection sqlite;
	GuardedConnection guarded;
	FaultInjectingConnection db;
	SqlConnection& raw_view() { return guarded; }
};

inline Record make_record(std::uint64_t owner, std::uint64_t revision, std::string payload, const char* kind = "mod:profile") {
	Record r;
	r.key = {character(owner), kind};
	r.revision = revision;
	r.payload = blob(std::move(payload));
	return r;
}

inline KvEntry make_kv(std::uint64_t owner, const char* mod, const char* key, std::int64_t v, std::uint64_t revision = 1) {
	KvEntry e;
	e.key = {character(owner), mod, key};
	e.value = Value{v};
	e.revision = revision;
	return e;
}

inline std::int64_t scalar(SqlConnection& db, const char* sql) {
	const auto rows = db.query({sql, {}});
	if (rows.empty()) return -1;
	const auto& v = rows.front().begin()->second;
	if (const auto* i = std::get_if<std::int64_t>(&v)) return *i;
	return -1;
}

inline std::string record_payload(SqlConnection& db, std::uint64_t owner, const char* kind = "mod:profile") {
	const auto rows = db.query({"SELECT payload FROM persist_records WHERE owner_id = ? AND kind = ?",
	                            {SqlValue{static_cast<std::int64_t>(owner)}, SqlValue{std::string(kind)}}});
	if (rows.empty()) return "<none>";
	return std::get<Blob>(rows.front().at("payload")).bytes;
}

template <class Pred>
bool eventually(Pred p, std::chrono::milliseconds limit = std::chrono::milliseconds(5000)) {
	const auto end = std::chrono::steady_clock::now() + limit;
	while (std::chrono::steady_clock::now() < end) {
		if (p()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return p();
}

} // namespace rathena::persist::testing_support
