// Quarantine, OnlineLeases, RecordStore, KvMigrationRegistry, KvStore, IdMap.
#include <algorithm>

#include "rathena/persist/codec.hpp"
#include "rathena/persist/service.hpp"
#include "store_sql.hpp"

namespace rathena::persist {
using namespace sql;

// ---- Quarantine ---------------------------------------------------------------------------
Quarantine::Quarantine(SqlConnection& db, bool ensure) : db_(db) {
	if (ensure) ensure_schema(db_);
}

std::uint64_t Quarantine::put(QuarantinedRow row) {
	std::uint64_t id = 0;
	db_.transaction([&] {
		db_.execute({"UPDATE persist_counters SET value = value + 1 WHERE name = 'quarantine'", {}});
		const auto r = db_.query({"SELECT value FROM persist_counters WHERE name = 'quarantine'", {}});
		if (r.empty()) throw RepositoryError("persist_counters row for quarantine is missing");
		id = static_cast<std::uint64_t>(as_int(r.front().at("value")));
		db_.execute({"INSERT INTO persist_quarantine (id, table_name, row_ref, owner_ref, reason, raw) VALUES (?, ?, ?, ?, ?, ?)",
		             {u(id), s(row.table), s(row.row_ref), s(row.owner), s(row.reason), b(row.raw.bytes)}});
	});
	return id;
}

namespace {
QuarantinedRow to_row(const Row& r) {
	QuarantinedRow q;
	q.quarantine_id = static_cast<std::uint64_t>(as_int(r.at("id")));
	q.table = as_text(r.at("table_name"));
	q.row_ref = as_text(r.at("row_ref"));
	q.owner = as_text(r.at("owner_ref"));
	q.reason = as_text(r.at("reason"));
	q.raw = Blob{as_text(r.at("raw"))};
	return q;
}
} // namespace

std::vector<QuarantinedRow> Quarantine::list() const {
	std::vector<QuarantinedRow> out;
	for (const auto& r : db_.query({"SELECT id, table_name, row_ref, owner_ref, reason, raw FROM persist_quarantine ORDER BY id", {}}))
		out.push_back(to_row(r));
	return out;
}

bool Quarantine::contains(const std::string& table, const std::string& row_ref) const {
	return !db_.query({"SELECT id FROM persist_quarantine WHERE table_name = ? AND row_ref = ?", {s(table), s(row_ref)}}).empty();
}

bool Quarantine::contains_for(const std::string& table, const std::string& row_ref, const std::string& owner) const {
	return !db_.query({"SELECT id FROM persist_quarantine WHERE table_name = ? AND row_ref = ? AND owner_ref = ?",
	                   {s(table), s(row_ref), s(owner)}}).empty();
}

std::optional<QuarantinedRow> Quarantine::release(std::uint64_t id) {
	std::optional<QuarantinedRow> out;
	db_.transaction([&] {
		const auto r = db_.query({"SELECT id, table_name, row_ref, owner_ref, reason, raw FROM persist_quarantine WHERE id = ?", {u(id)}});
		if (r.empty()) return;
		out = to_row(r.front());
		db_.execute({"DELETE FROM persist_quarantine WHERE id = ?", {u(id)}});
	});
	return out;
}

// ---- OnlineLeases -------------------------------------------------------------------------
void OnlineLeases::acquire(const OwnerRef& o) { std::scoped_lock l(mu_); held_.insert(o); }
void OnlineLeases::release(const OwnerRef& o) { std::scoped_lock l(mu_); held_.erase(o); }
bool OnlineLeases::held(const OwnerRef& o) const { std::scoped_lock l(mu_); return held_.count(o) != 0; }

// ---- RecordStore --------------------------------------------------------------------------
RecordStore::RecordStore(SqlConnection& db, const OnlineLeases& l, Quarantine& q) : db_(db), leases_(l), quarantine_(q) {
	ensure_schema(db_);
}

void RecordStore::set_collection_policy(const std::string& kind, CollectionPolicy p) { policies_[kind] = p; }

namespace {
std::string owner_text(const OwnerRef& o) { return std::to_string(static_cast<int>(o.scope)) + ":" + std::to_string(o.id); }
Statement select_record(const RecordKey& k) {
	return {"SELECT schema_version, revision, payload FROM persist_records WHERE scope = ? AND owner_id = ? AND kind = ?",
	        {i(static_cast<std::int64_t>(k.owner.scope)), u(k.owner.id), s(k.kind)}};
}
} // namespace

SaveOutcome RecordStore::save(const SaveRequest& req) {
	if (!req.final_save && !req.restore && !leases_.held(req.record.key.owner)) return SaveOutcome::RejectedNotOnline;
	if (req.declared_size != req.record.payload.bytes.size()) return SaveOutcome::RejectedSizeMismatch;
	if (quarantine_.contains_for("persist_records", "rec/" + req.record.key.kind, owner_text(req.record.key.owner))) {
		// a quarantined row is replaced only by an operator release; a plain save must not overwrite evidence
		throw QuarantinedRowError("record of kind '" + req.record.key.kind + "' is quarantined");
	}
	db_.execute(sql::upsert_record(req.record));
	return db_.affected_rows() == 0 ? SaveOutcome::RejectedStaleRevision : SaveOutcome::Accepted;
}

std::optional<Record> RecordStore::load(const RecordKey& key) {
	const std::string ref = "rec/" + key.kind;
	if (quarantine_.contains_for("persist_records", ref, owner_text(key.owner)))
		throw QuarantinedRowError("record of kind '" + key.kind + "' is quarantined");
	const auto rows = db_.query(select_record(key));
	if (rows.empty()) return std::nullopt;
	const auto& r = rows.front();
	if (!is_int(r.at("schema_version")) || !is_int(r.at("revision")) || !is_blob(r.at("payload"))) {
		QuarantinedRow q;
		q.table = "persist_records";
		q.row_ref = ref;
		q.owner = owner_text(key.owner);
		q.reason = "column types do not match the schema";
		q.raw = Blob{as_text(r.at("payload"))};
		quarantine_.put(std::move(q));
		db_.execute({"DELETE FROM persist_records WHERE scope = ? AND owner_id = ? AND kind = ?",
		             {i(static_cast<std::int64_t>(key.owner.scope)), u(key.owner.id), sql::s(key.kind)}});
		throw QuarantinedRowError("record of kind '" + key.kind + "' was corrupt and is now quarantined");
	}
	Record rec;
	rec.key = key;
	rec.schema_version = static_cast<std::uint32_t>(as_int(r.at("schema_version")));
	rec.revision = static_cast<std::uint64_t>(as_int(r.at("revision")));
	rec.payload = Blob{as_text(r.at("payload"))};
	return rec;
}

void RecordStore::save_collection(const RecordKey& key, const std::vector<Blob>& items) {
	if (const auto it = policies_.find(key.kind); it != policies_.end() && it->second.max_items != 0 &&
	                                                items.size() > it->second.max_items)
		throw CollectionCapExceededError("collection '" + key.kind + "' holds at most " +
		                                 std::to_string(it->second.max_items) + " items, got " + std::to_string(items.size()));
	ByteWriter w;
	w.u32(static_cast<std::uint32_t>(items.size()));
	for (const auto& it : items) w.str(it.bytes);
	Record r;
	r.key = key;
	// a new save of a collection replaces it: take the next revision so the revision guard accepts it
	std::uint64_t rev = 1;
	if (const auto rows = db_.query(select_record(key)); !rows.empty()) rev = static_cast<std::uint64_t>(as_int(rows.front().at("revision"))) + 1;
	r.revision = rev;
	r.payload = Blob{std::move(w.bytes())};
	db_.execute(sql::upsert_record(r));
}

std::vector<Blob> RecordStore::load_collection(const RecordKey& key) {
	std::vector<Blob> out;
	bool consume = false;
	if (const auto it = policies_.find(key.kind); it != policies_.end()) consume = it->second.restore_once;
	db_.transaction([&] {
		const auto rows = db_.query(select_record(key));
		if (rows.empty()) return;
		try {
			const std::string stored = as_text(rows.front().at("payload"));
			ByteReader r(stored);
			const auto n = r.count(4);
			for (std::uint32_t k = 0; k < n; ++k) out.push_back(Blob{r.str()});
			if (!r.done()) throw DecodeError("trailing bytes");
		} catch (const DecodeError&) {
			out.clear();
			throw QuarantinedRowError("collection '" + key.kind + "' is corrupt");
		}
		if (consume)
			db_.execute({"DELETE FROM persist_records WHERE scope = ? AND owner_id = ? AND kind = ?",
			             {i(static_cast<std::int64_t>(key.owner.scope)), u(key.owner.id), sql::s(key.kind)}});
	});
	return out;
}

// ---- KvMigrationRegistry --------------------------------------------------------------------
void KvMigrationRegistry::register_step(const std::string& mod, std::uint32_t from, Fn fn) {
	steps_[{mod, from}] = std::move(fn);
}
bool KvMigrationRegistry::has_path(const std::string& mod, std::uint32_t stored, std::uint32_t current) const {
	if (stored > current) return false;
	for (std::uint32_t v = stored; v < current; ++v)
		if (steps_.find({mod, v}) == steps_.end()) return false;
	return true;
}
Value KvMigrationRegistry::upgrade(const std::string& mod, std::uint32_t stored, std::uint32_t current, Value v) const {
	if (!has_path(mod, stored, current))
		throw MigrationError("mod " + mod + ": stored row version " + std::to_string(stored) +
		                     " has no migration path to " + std::to_string(current));
	for (std::uint32_t k = stored; k < current; ++k) v = steps_.at({mod, k})(v);
	return v;
}

// ---- KvStore ------------------------------------------------------------------------------
KvStore::KvStore(SqlConnection& db, Quarantine& q, KvPolicy p) : db_(db), quarantine_(q), policy_(p) { ensure_schema(db_); }

void KvStore::set_migrations(const KvMigrationRegistry* r) { migrations_ = r; }
void KvStore::set_mod_version(const std::string& mod, std::uint32_t v) { versions_[mod] = v; }
void KvStore::set_mod_quota(const std::string& mod, std::uint64_t bytes) {
	std::scoped_lock l(ledger_mu_);
	quotas_[mod] = bytes;
}
std::uint64_t KvStore::quota(const std::string& mod) const {
	std::scoped_lock l(ledger_mu_);
	const auto it = quotas_.find(mod);
	return it == quotas_.end() ? policy_.mod_quota_bytes : it->second;
}

std::uint64_t KvStore::used_locked(const std::string& mod) const {
	auto it = used_.find(mod);
	if (it != used_.end()) return it->second;
	if (ledger_loaded_) return 0;  // warm_up() saw every mod that has rows
	const auto rows = db_.query({"SELECT COALESCE(SUM(size), 0) AS total FROM kv_entries WHERE mod_id = ?", {sql::s(mod)}});
	const auto total = rows.empty() ? 0 : static_cast<std::uint64_t>(as_int(rows.front().at("total")));
	used_[mod] = total;
	return total;
}

void KvStore::warm_up() {
	std::map<std::string, std::uint64_t> loaded;
	for (const auto& r : db_.query({"SELECT mod_id, SUM(size) AS total FROM kv_entries GROUP BY mod_id", {}}))
		loaded[as_text(r.at("mod_id"))] = static_cast<std::uint64_t>(as_int(r.at("total")));
	std::scoped_lock l(ledger_mu_);
	used_ = std::move(loaded);
	ledger_loaded_ = true;
}

std::uint64_t KvStore::bytes_used(const std::string& mod) const {
	std::scoped_lock l(ledger_mu_);
	return used_locked(mod);
}

void KvStore::admit(const std::string& mod, std::uint64_t new_cost, std::uint64_t old_cost) {
	std::scoped_lock l(ledger_mu_);
	const auto used = used_locked(mod);
	const auto base = used >= old_cost ? used - old_cost : 0;
	const auto qit = quotas_.find(mod);
	const auto limit = qit == quotas_.end() ? policy_.mod_quota_bytes : qit->second;
	if (base + new_cost > limit)
		throw QuotaExceededError("mod " + mod + ": storage quota " + std::to_string(limit) + " bytes exceeded (used " +
		                         std::to_string(used) + ", write needs " + std::to_string(new_cost) + ")");
	used_[mod] = base + new_cost;
}

void KvStore::release(const std::string& mod, std::uint64_t cost) {
	std::scoped_lock l(ledger_mu_);
	const auto used = used_locked(mod);
	used_[mod] = used >= cost ? used - cost : 0;
}

bool KvStore::is_quarantined(const KvKey& key) const {
	return quarantine_.contains_for("kv_entries", kv_row_ref(key), owner_ref_text(key));
}

void KvStore::quarantine_row(const KvKey& key, const std::string& reason, Blob raw) {
	QuarantinedRow q;
	q.table = "kv_entries";
	q.row_ref = kv_row_ref(key);
	q.owner = owner_ref_text(key);
	q.reason = reason;
	q.raw = std::move(raw);
	std::uint64_t cost = 0;
	if (const auto m = meta(key)) cost = m->cost;
	quarantine_.put(std::move(q));
	db_.execute(delete_kv(key));
	release(key.mod_id, cost);
}

std::optional<RawKvMeta> KvStore::meta(const KvKey& key) {
	const auto rows = db_.query({"SELECT revision, size FROM kv_entries WHERE scope = ? AND owner_ref = ? AND mod_id = ? AND entry_key = ?",
	                             {i(static_cast<std::int64_t>(key.owner.scope)), sql::s(owner_ref_text(key)), sql::s(key.mod_id), sql::s(key.key)}});
	if (rows.empty()) return std::nullopt;
	return RawKvMeta{static_cast<std::uint64_t>(as_int(rows.front().at("revision"))),
	                 static_cast<std::uint64_t>(as_int(rows.front().at("size")))};
}

std::optional<RawKv> KvStore::get_raw(const KvKey& key) {
	if (is_quarantined(key))
		throw QuarantinedRowError("mod " + key.mod_id + ": row '" + key.key + "' is quarantined (corrupt)");
	const auto rows = db_.query(select_kv(key));
	if (rows.empty()) return std::nullopt;
	const auto& r = rows.front();
	if (!is_int(r.at("row_version")) || !is_int(r.at("revision")) || !is_int(r.at("size")) || !is_blob(r.at("value"))) {
		quarantine_row(key, "column types do not match the schema", Blob{as_text(r.at("value"))});
		throw QuarantinedRowError("mod " + key.mod_id + ": row '" + key.key + "' was corrupt and is now quarantined");
	}
	RawKv out;
	out.row_version = static_cast<std::uint32_t>(as_int(r.at("row_version")));
	out.revision = static_cast<std::uint64_t>(as_int(r.at("revision")));
	out.cost = static_cast<std::uint64_t>(as_int(r.at("size")));
	out.bytes = Blob{as_text(r.at("value"))};
	return out;
}

void KvStore::set_raw(const KvKey& key, RawKv row) {
	if (is_quarantined(key))
		throw QuarantinedRowError("mod " + key.mod_id + ": row '" + key.key + "' is quarantined (corrupt)");
	const auto old = meta(key);
	admit(key.mod_id, row.cost, old ? old->cost : 0);
	try {
		db_.execute(upsert_kv(key, row.row_version, old ? old->revision + 1 : 1, row.cost, row.bytes.bytes));
	} catch (...) {
		// put the ledger back to what the database holds
		{
			std::scoped_lock l(ledger_mu_);
			used_[key.mod_id] = used_locked(key.mod_id) - row.cost + (old ? old->cost : 0);
		}
		throw;
	}
}

std::optional<Value> KvStore::get(const KvKey& key) {
	auto raw = get_raw(key);
	if (!raw) return std::nullopt;
	const auto& bytes = raw->bytes.bytes;
	if (!bytes.empty() && bytes.front() == kCodecScriptHostValue)
		throw RepositoryError("mod " + key.mod_id + ": row '" + key.key + "' was written with the script-host codec");
	Value v;
	try {
		v = decode_value(bytes);
	} catch (const DecodeError& e) {
		quarantine_row(key, std::string("value decode failed: ") + e.what(), raw->bytes);
		throw QuarantinedRowError("mod " + key.mod_id + ": row '" + key.key + "' was corrupt and is now quarantined");
	}
	const auto vit = versions_.find(key.mod_id);
	const std::uint32_t current = vit == versions_.end() ? raw->row_version : vit->second;
	if (raw->row_version != current) {
		if (migrations_ == nullptr || !migrations_->has_path(key.mod_id, raw->row_version, current))
			throw MigrationError("mod " + key.mod_id + ": stored row version " + std::to_string(raw->row_version) +
			                     " has no migration path to " + std::to_string(current));
		v = migrations_->upgrade(key.mod_id, raw->row_version, current, std::move(v));
		const auto enc = encode_value(v);
		db_.execute(upsert_kv(key, current, raw->revision, raw->cost, enc));
	}
	return v;
}

void KvStore::set(const KvKey& key, Value value) {
	if (policy_.delete_on_zero_or_empty && is_zero_or_empty(value)) {
		erase(key);
		return;
	}
	const auto vit = versions_.find(key.mod_id);
	RawKv row;
	row.row_version = vit == versions_.end() ? 1 : vit->second;
	row.cost = key.key.size() + encoded_size(value);
	row.bytes = Blob{encode_value(value)};
	set_raw(key, std::move(row));
}

bool KvStore::erase(const KvKey& key) {
	const auto old = meta(key);
	db_.execute(delete_kv(key));
	const bool existed = db_.affected_rows() != 0;
	if (existed && old) release(key.mod_id, old->cost);
	return existed;
}

std::vector<std::string> KvStore::keys(OwnerScope scope, const std::string& owner_ref, const std::string& mod) const {
	std::vector<std::string> out;
	for (const auto& r : db_.query({"SELECT entry_key FROM kv_entries WHERE scope = ? AND owner_ref = ? AND mod_id = ? ORDER BY entry_key",
	                                {i(static_cast<std::int64_t>(scope)), sql::s(owner_ref), sql::s(mod)}}))
		out.push_back(as_text(r.at("entry_key")));
	return out;
}

// ---- IdMap --------------------------------------------------------------------------------
IdMap::IdMap(SqlConnection& db, std::map<std::string, IdRange> r) : db_(db), ranges_(std::move(r)) { ensure_schema(db_); }

namespace {
struct IdRow { std::string namespaced; std::uint32_t client_id; bool tombstoned; };
std::optional<IdRow> by_number(SqlConnection& db, const std::string& kind, std::uint32_t n) {
	const auto r = db.query({"SELECT namespaced, client_id, state FROM persist_id_map WHERE kind = ? AND client_id = ?", {sql::s(kind), u(n)}});
	if (r.empty()) return std::nullopt;
	return IdRow{as_text(r.front().at("namespaced")), n, as_int(r.front().at("state")) != 0};
}
std::optional<IdRow> by_name(SqlConnection& db, const std::string& kind, const std::string& name) {
	const auto r = db.query({"SELECT namespaced, client_id, state FROM persist_id_map WHERE kind = ? AND namespaced = ?", {sql::s(kind), sql::s(name)}});
	if (r.empty()) return std::nullopt;
	return IdRow{name, static_cast<std::uint32_t>(as_int(r.front().at("client_id"))), as_int(r.front().at("state")) != 0};
}
} // namespace

std::uint32_t IdMap::assign(const std::string& kind, const std::string& name, std::optional<std::uint32_t> explicit_id) {
	std::uint32_t result = 0;
	db_.transaction([&] {
		const auto existing = by_name(db_, kind, name);
		if (existing) {
			if (explicit_id && *explicit_id != existing->client_id)
				throw IdCollisionError("'" + name + "' is already mapped to " + std::to_string(existing->client_id) +
				                       ", cannot also claim " + std::to_string(*explicit_id));
			if (existing->tombstoned)  // the string id returns: it gets its own number back
				db_.execute({"UPDATE persist_id_map SET state = 0 WHERE kind = ? AND client_id = ?", {sql::s(kind), u(existing->client_id)}});
			result = existing->client_id;
			return;
		}
		std::uint32_t n = 0;
		if (explicit_id) {
			n = *explicit_id;
			if (const auto holder = by_number(db_, kind, n))
				throw IdCollisionError("client id " + std::to_string(n) + " of kind '" + kind + "' is claimed by both '" +
				                       holder->namespaced + "'" + (holder->tombstoned ? " (retired, tombstoned)" : "") +
				                       " and '" + name + "'");
		} else {
			const auto rit = ranges_.find(kind);
			if (rit == ranges_.end()) throw PersistError("no allocation range configured for kind '" + kind + "'");
			const auto [first, last] = rit->second;
			// fast path: one past the highest number in range; fall back to the first hole
			const auto top = db_.query({"SELECT MAX(client_id) AS top FROM persist_id_map WHERE kind = ? AND client_id >= ? AND client_id <= ?",
			                            {sql::s(kind), u(first), u(last)}});
			std::int64_t max_used = -1;
			if (!top.empty() && std::holds_alternative<std::int64_t>(top.front().at("top"))) max_used = as_int(top.front().at("top"));
			if (max_used < 0) {
				n = first;
			} else if (static_cast<std::uint64_t>(max_used) < last) {
				n = static_cast<std::uint32_t>(max_used + 1);
			} else {
				bool found = false;
				std::uint32_t expect = first;
				for (const auto& r : db_.query({"SELECT client_id FROM persist_id_map WHERE kind = ? AND client_id >= ? AND client_id <= ? ORDER BY client_id",
				                                {sql::s(kind), u(first), u(last)}})) {
					const auto got = static_cast<std::uint32_t>(as_int(r.at("client_id")));
					if (got != expect) { found = true; break; }
					++expect;
				}
				if (!found && expect > last) throw IdRangeExhaustedError("id range for kind '" + kind + "' (" + std::to_string(first) + ".." + std::to_string(last) + ") is exhausted; numbers of retired ids are never reused");
				n = expect;
			}
		}
		db_.execute({"INSERT INTO persist_id_map (kind, client_id, namespaced, state) VALUES (?, ?, ?, 0)", {sql::s(kind), u(n), sql::s(name)}});
		result = n;
	});
	return result;
}

std::optional<std::uint32_t> IdMap::client_id(const std::string& kind, const std::string& name) const {
	const auto row = by_name(db_, kind, name);
	if (!row || row->tombstoned) return std::nullopt;
	return row->client_id;
}

std::optional<std::string> IdMap::namespaced_id(const std::string& kind, std::uint32_t n) const {
	const auto row = by_number(db_, kind, n);
	if (!row || row->tombstoned) return std::nullopt;
	return row->namespaced;
}

void IdMap::retire(const std::string& kind, const std::string& name) {
	db_.execute({"UPDATE persist_id_map SET state = 1 WHERE kind = ? AND namespaced = ?", {sql::s(kind), sql::s(name)}});
}

bool IdMap::is_tombstoned(const std::string& kind, std::uint32_t n) const {
	const auto row = by_number(db_, kind, n);
	return row && row->tombstoned;
}

} // namespace rathena::persist
