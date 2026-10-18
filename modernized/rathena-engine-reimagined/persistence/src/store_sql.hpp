#pragma once
// Internal SQL shared by the stores and the flusher (portable subset: ? placeholders, ON CONFLICT
// upserts, quoted-free ASCII identifiers, integers for times and ids).
#include <string>
#include <variant>

#include "rathena/persist/domain.hpp"
#include "rathena/persist/sql_connection.hpp"

namespace rathena::persist::sql {

inline SqlValue i(std::int64_t v) { return SqlValue{v}; }
inline SqlValue u(std::uint64_t v) { return SqlValue{static_cast<std::int64_t>(v)}; }
inline SqlValue s(std::string v) { return SqlValue{std::move(v)}; }
inline SqlValue b(std::string v) { return SqlValue{Blob{std::move(v)}}; }

inline std::int64_t as_int(const SqlValue& v, std::int64_t dflt = 0) {
	if (const auto* x = std::get_if<std::int64_t>(&v)) return *x;
	if (const auto* y = std::get_if<std::uint64_t>(&v)) return static_cast<std::int64_t>(*y);
	return dflt;
}
inline std::string as_text(const SqlValue& v) {
	if (const auto* x = std::get_if<std::string>(&v)) return *x;
	if (const auto* y = std::get_if<Blob>(&v)) return y->bytes;
	return {};
}
inline bool is_int(const SqlValue& v) { return std::holds_alternative<std::int64_t>(v); }
inline bool is_blob(const SqlValue& v) { return std::holds_alternative<Blob>(v); }
inline bool is_text(const SqlValue& v) { return std::holds_alternative<std::string>(v); }

inline Statement upsert_record(const Record& r) {
	return {"INSERT INTO persist_records (scope, owner_id, kind, schema_version, revision, payload) "
	        "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT (scope, owner_id, kind) DO UPDATE SET "
	        "schema_version = excluded.schema_version, revision = excluded.revision, payload = excluded.payload "
	        "WHERE excluded.revision >= persist_records.revision",
	        {i(static_cast<std::int64_t>(r.key.owner.scope)), u(r.key.owner.id), s(r.key.kind),
	         i(r.schema_version), u(r.revision), b(r.payload.bytes)}};
}

inline Statement upsert_kv(const KvKey& k, std::uint32_t row_version, std::uint64_t revision, std::uint64_t cost,
                           const std::string& bytes) {
	return {"INSERT INTO kv_entries (scope, owner_ref, mod_id, entry_key, row_version, revision, size, value) "
	        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT (scope, owner_ref, mod_id, entry_key) DO UPDATE SET "
	        "row_version = excluded.row_version, revision = excluded.revision, size = excluded.size, "
	        "value = excluded.value WHERE excluded.revision >= kv_entries.revision",
	        {i(static_cast<std::int64_t>(k.owner.scope)), s(owner_ref_text(k)), s(k.mod_id), s(k.key),
	         i(row_version), u(revision), u(cost), b(bytes)}};
}

inline Statement delete_kv(const KvKey& k) {
	return {"DELETE FROM kv_entries WHERE scope = ? AND owner_ref = ? AND mod_id = ? AND entry_key = ?",
	        {i(static_cast<std::int64_t>(k.owner.scope)), s(owner_ref_text(k)), s(k.mod_id), s(k.key)}};
}

inline Statement select_kv(const KvKey& k) {
	return {"SELECT row_version, revision, size, value FROM kv_entries "
	        "WHERE scope = ? AND owner_ref = ? AND mod_id = ? AND entry_key = ?",
	        {i(static_cast<std::int64_t>(k.owner.scope)), s(owner_ref_text(k)), s(k.mod_id), s(k.key)}};
}

inline std::string kv_row_ref(const KvKey& k) { return "kv/" + k.mod_id + "/" + k.key; }

} // namespace rathena::persist::sql
