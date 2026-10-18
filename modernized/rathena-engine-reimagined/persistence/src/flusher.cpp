// WriteBehindFlusher: applies batches to the database with WAL fallback and exactly-once replay.
#include "rathena/persist/codec.hpp"
#include "rathena/persist/service.hpp"
#include "store_sql.hpp"

namespace rathena::persist {
using namespace sql;

WriteBehindFlusher::WriteBehindFlusher(SqlConnection& db, DirtyTracker& t, Wal& w)
    : db_(db), tracker_(t), wal_(w), quarantine_(db, false) {
	// Normally the schema is created here; if the backend is down at startup the first apply or
	// replay retries it (batches wait in the WAL meanwhile).
	try { ensure_schema_once(); } catch (const RepositoryError&) {}
}

namespace {
Statement kv_statement(const KvEntry& e) {
	if (e.erased) return delete_kv(e.key);
	const std::string bytes = e.encoded ? e.encoded->bytes : encode_value(e.value);
	const std::uint64_t cost = e.cost != 0 ? e.cost : e.key.key.size() + encoded_size(e.value);
	return upsert_kv(e.key, e.row_version, e.revision, cost, bytes);
}

Statement marker_statement(const std::string& wal_id, std::uint64_t seq) {
	return {"INSERT INTO persist_wal_progress (wal_id, last_seq) VALUES (?, ?) ON CONFLICT (wal_id) DO UPDATE SET "
	        "last_seq = excluded.last_seq WHERE excluded.last_seq > persist_wal_progress.last_seq",
	        {sql::s(wal_id), u(seq)}};
}
} // namespace

void WriteBehindFlusher::ensure_schema_once() {
	if (schema_ok_) return;
	try {
		ensure_schema(db_);
	} catch (const MigrationError& e) {
		throw RepositoryError(std::string("schema check failed: ") + e.what());  // outage-class: batches wait in the WAL
	}
	schema_ok_ = true;
}

void WriteBehindFlusher::quarantine_entry(const DirtyBatch& batch, std::size_t index, bool is_kv, const std::string& reason) {
	QuarantinedRow q;
	if (is_kv) {
		const auto& e = batch.kv[index];
		q.table = "kv_entries";
		q.row_ref = kv_row_ref(e.key);
		q.owner = owner_ref_text(e.key);
		q.raw = e.encoded ? *e.encoded : Blob{encode_value(e.value)};
	} else {
		const auto& r = batch.records[index];
		q.table = "persist_records";
		q.row_ref = "rec/" + r.key.kind;
		q.owner = std::to_string(static_cast<int>(r.key.owner.scope)) + ":" + std::to_string(r.key.owner.id);
		q.raw = r.payload;
	}
	q.reason = reason;  // backend message only; never row bytes
	quarantine_.put(std::move(q));
}

WriteBehindFlusher::Applied WriteBehindFlusher::apply(const DirtyBatch& batch,
                                                      std::optional<std::pair<std::string, std::uint64_t>> marker) {
	Applied out;
	ensure_schema_once();
	try {
		db_.transaction([&] {
			for (const auto& r : batch.records) db_.execute(upsert_record(r));
			for (const auto& e : batch.kv) db_.execute(kv_statement(e));
			if (marker) db_.execute(marker_statement(marker->first, marker->second));
		});
		out.records = batch.records.size();
		out.kv = batch.kv.size();
		return out;
	} catch (const RowRejectedError&) {
		// The database is healthy but refuses some row: isolate it, quarantine it, apply the rest.
	}
	for (std::size_t k = 0; k < batch.records.size(); ++k) {
		try {
			db_.transaction([&] { db_.execute(upsert_record(batch.records[k])); });
			++out.records;
		} catch (const RowRejectedError& e) {
			quarantine_entry(batch, k, false, std::string("backend rejected the row: ") + e.what());
			++out.quarantined;
		}
	}
	for (std::size_t k = 0; k < batch.kv.size(); ++k) {
		try {
			db_.transaction([&] { db_.execute(kv_statement(batch.kv[k])); });
			++out.kv;
		} catch (const RowRejectedError& e) {
			quarantine_entry(batch, k, true, std::string("backend rejected the row: ") + e.what());
			++out.quarantined;
		}
	}
	if (marker) db_.transaction([&] { db_.execute(marker_statement(marker->first, marker->second)); });
	return out;
}

std::size_t WriteBehindFlusher::drain_wal() {
	if (wal_.empty()) {
		backend_down_ = false;
		return 0;
	}
	last_drain_attempt_ = std::chrono::steady_clock::now();
	std::size_t consumed = 0;
	try {
		ensure_schema_once();
		std::uint64_t last_applied = 0;
		{
			const auto rows = db_.query({"SELECT last_seq FROM persist_wal_progress WHERE wal_id = ?", {sql::s(wal_.id())}});
			if (!rows.empty()) last_applied = static_cast<std::uint64_t>(as_int(rows.front().at("last_seq")));
		}
		wal_.replay_seq([&](std::uint64_t seq, const DirtyBatch& b) {
			if (hook_) hook_("replay:before_apply", seq);
			if (seq > last_applied) {
				// batch effects and the progress marker commit together: exactly once
				apply(b, std::make_pair(wal_.id(), seq));
				last_applied = seq;
				if (applied_cb_) applied_cb_(b);
			}  // else: applied by a run that crashed before trimming the segment
			if (hook_) hook_("replay:after_apply", seq);
			++consumed;
		});
		if (hook_) hook_("replay:after_trim", 0);
		backend_down_ = false;
		// segments of older WAL generations are gone: forget their markers
		db_.execute({"DELETE FROM persist_wal_progress WHERE wal_id <> ?", {sql::s(wal_.id())}});
	} catch (const RepositoryError&) {
		backend_down_ = true;  // outage: keep every remaining segment for the next attempt
	}
	return consumed;
}

FlushResult WriteBehindFlusher::persist(const DirtyBatch& batch) {
	FlushResult res;
	if (batch.empty()) return res;
	// Order: anything already in the WAL is older than this batch and goes first.
	if (!wal_.empty()) {
		const auto now = std::chrono::steady_clock::now();
		if (!backend_down_ || retry_interval_.count() == 0 || now - last_drain_attempt_ >= retry_interval_) drain_wal();
	}
	if (wal_.empty()) {
		try {
			const auto a = apply(batch, std::nullopt);
			res.records_written = a.records;
			res.kv_written = a.kv;
			res.quarantined = a.quarantined;
			backend_down_ = false;
			if (applied_cb_) applied_cb_(batch);
			return res;
		} catch (const RepositoryError&) {
			backend_down_ = true;
		}
	}
	wal_.append(batch);  // WalFullError propagates: the caller still owns the batch
	res.spilled_to_wal = true;
	return res;
}

FlushResult WriteBehindFlusher::flush_once() {
	const auto batch = tracker_.swap_out();
	try {
		return persist(*batch);
	} catch (...) {
		// The batch could not be persisted anywhere (WAL full): put it back so nothing is lost.
		// Entries marked since the swap are newer and win.
		tracker_.restore(*batch);
		throw;
	}
}

} // namespace rathena::persist
