#include "rathena/persist/script_host_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "rathena/persist/codec.hpp"

namespace rathena::persist {
namespace sh = script_host;
namespace {

enum Tag : std::uint8_t { kNil = 0, kInt = 1, kDouble = 2, kBool = 3, kString = 4, kTable = 5 };

void put(ByteWriter& w, const sh::Value& v) {
	struct V {
		ByteWriter& w;
		void operator()(std::monostate) const { w.u8(kNil); }
		void operator()(std::int64_t x) const { w.u8(kInt); w.i64(x); }
		void operator()(double x) const { w.u8(kDouble); w.f64(x); }
		void operator()(bool x) const { w.u8(kBool); w.u8(x ? 1 : 0); }
		void operator()(const std::string& x) const { w.u8(kString); w.str(x); }
		void operator()(const std::shared_ptr<sh::ValueTable>& t) const {
			w.u8(kTable);
			w.u32(t ? static_cast<std::uint32_t>(t->entries.size()) : 0);
			if (t) for (const auto& [k, e] : t->entries) { w.str(k); put(w, e); }
			w.u32(t ? static_cast<std::uint32_t>(t->array.size()) : 0);
			if (t) for (const auto& [k, e] : t->array) { w.i64(k); put(w, e); }
		}
	};
	std::visit(V{w}, v);
}

sh::Value get(ByteReader& r, int depth) {
	if (depth > sh::kMaxValueDepth) throw DecodeError("value nested too deep");
	switch (r.u8()) {
	case kNil: return sh::Value{};
	case kInt: return sh::Value{r.i64()};
	case kDouble: return sh::Value{r.f64()};
	case kBool: { const auto b = r.u8(); if (b > 1) throw DecodeError("bad bool"); return sh::Value{b == 1}; }
	case kString: return sh::Value{r.str()};
	case kTable: {
		auto t = std::make_shared<sh::ValueTable>();
		const auto ne = r.count(5);
		for (std::uint32_t i = 0; i < ne; ++i) { auto k = r.str(); t->entries.emplace(std::move(k), get(r, depth + 1)); }
		const auto na = r.count(9);
		for (std::uint32_t i = 0; i < na; ++i) { const auto k = r.i64(); t->array.emplace(k, get(r, depth + 1)); }
		return sh::Value{t};
	}
	default: throw DecodeError("unknown value tag");
	}
}

std::string encode_row(const sh::Value& v) {
	ByteWriter w;
	w.u8(static_cast<std::uint8_t>(kCodecScriptHostValue));
	put(w, v);
	return std::move(w.bytes());
}

sh::Value decode_row(const std::string& bytes) {
	if (bytes.empty() || bytes.front() != kCodecScriptHostValue) throw DecodeError("bad codec byte");
	ByteReader r(std::string_view(bytes).substr(1));
	auto v = get(r, 0);
	if (!r.done()) throw DecodeError("trailing bytes");
	return v;
}

OwnerScope map_scope(sh::Scope s) {
	switch (s) {
	case sh::Scope::Character: return OwnerScope::Character;
	case sh::Scope::Account: return OwnerScope::Account;
	case sh::Scope::World: return OwnerScope::World;
	}
	return OwnerScope::World;
}

KvKey map_key(const sh::StorageKey& k) {
	KvKey out;
	out.owner.scope = map_scope(k.scope);
	std::uint64_t id = 0;
	bool numeric = !k.owner.empty() && k.owner.size() <= 18;
	for (char c : k.owner) {
		if (c < '0' || c > '9') { numeric = false; break; }
		id = id * 10 + static_cast<std::uint64_t>(c - '0');
	}
	out.owner.id = numeric ? id : 0;
	out.owner_text = k.owner;
	out.mod_id = k.mod;
	out.key = k.key;
	return out;
}

sh::StorageError corrupt(const KvKey& k, const std::string& why) {
	return sh::StorageError(sh::StorageError::Code::Corrupt, "mod " + k.mod_id + ": row '" + k.key + "' " + why);
}

} // namespace

std::uint64_t KvStorageBackend::next_revision(std::uint64_t old_rev) {
	// Strictly increasing and independent of whether the old row could be read: wall-clock
	// microseconds, bumped past anything already issued or stored.
	const auto now = static_cast<std::uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	std::uint64_t want = std::max({now, old_rev + 1, last_revision_ + 1});
	last_revision_ = want;
	return want;
}

std::uint64_t script_host_row_cost(const sh::StorageKey& k, const sh::Row& r) {
	return k.key.size() + k.owner.size() + 4 + sh::value_size(r.value);
}

KvStorageBackend::KvStorageBackend(KvStore& kv, WriteBehindService* wb, std::size_t max_cached)
    : kv_(kv), wb_(wb), max_cached_(max_cached) {
	if (wb_ != nullptr) kv_.warm_up();  // quota checks must not need the database while it is down
	if (wb_ != nullptr) wb_->set_applied_callback([this](const DirtyBatch& b) { on_applied(b); });
}

std::size_t KvStorageBackend::overlay_size() const {
	std::scoped_lock l(mu_);
	return static_cast<std::size_t>(std::count_if(overlay_.begin(), overlay_.end(), [](const auto& kv) { return kv.second.dirty; }));
}

void KvStorageBackend::trim_cache() const {
	if (overlay_.size() <= max_cached_) return;
	const auto target = max_cached_ - max_cached_ / 4;
	for (auto it = overlay_.begin(); it != overlay_.end() && overlay_.size() > target;)
		it = it->second.dirty ? std::next(it) : overlay_.erase(it);  // dirty rows are never evicted
}

void KvStorageBackend::on_applied(const DirtyBatch& batch) {
	std::scoped_lock l(mu_);
	for (const auto& e : batch.kv) {
		const auto it = overlay_.find(e.key);
		if (it == overlay_.end() || it->second.revision > e.revision) continue;  // a newer write is pending
		if (it->second.erased) overlay_.erase(it);      // absent in the database now: nothing to cache
		else it->second.dirty = false;                  // in the database: stays as a clean cache entry
	}
}

std::optional<sh::Row> KvStorageBackend::load(const sh::StorageKey& k) const {
	const KvKey key = map_key(k);
	std::scoped_lock l(mu_);
	if (const auto it = overlay_.find(key); it != overlay_.end()) {
		if (it->second.erased) return std::nullopt;
		return sh::Row{it->second.row.version, sh::deep_copy(it->second.row.value)};
	}
	std::optional<RawKv> raw;
	try {
		raw = kv_.get_raw(key);
	} catch (const QuarantinedRowError& e) {
		throw sh::StorageError(sh::StorageError::Code::Corrupt, e.what());
	}
	if (!raw) {
		if (wb_ != nullptr) {  // negative cache
			overlay_[key] = Pending{true, false, sh::Row{}, 0, 0};
			trim_cache();
		}
		return std::nullopt;
	}
	try {
		auto row = sh::Row{raw->row_version, decode_row(raw->bytes.bytes)};
		if (wb_ != nullptr) {
			overlay_[key] = Pending{false, false, sh::Row{row.version, sh::deep_copy(row.value)}, raw->revision, raw->cost};
			trim_cache();
		}
		return row;
	} catch (const DecodeError& e) {
		kv_.quarantine_row(key, std::string("value decode failed: ") + e.what(), raw->bytes);
		throw corrupt(key, "was corrupt and is now quarantined");
	}
}

void KvStorageBackend::store(const sh::StorageKey& k, const sh::Row& r) {
	const KvKey key = map_key(k);
	const std::uint64_t cost = script_host_row_cost(k, r);
	std::string bytes = encode_row(r.value);
	std::scoped_lock l(mu_);
	std::uint64_t old_cost = 0, old_rev = 0;
	if (const auto it = overlay_.find(key); it != overlay_.end()) {
		old_rev = it->second.revision;
		old_cost = it->second.erased ? 0 : it->second.cost;
	} else {
		try {
			if (kv_.is_quarantined(key)) throw corrupt(key, "is quarantined");
			if (const auto m = kv_.meta(key)) { old_rev = m->revision; old_cost = m->cost; }
		} catch (const RepositoryError&) {
			// Backend down. Write-behind keeps accepting: the replaced row's cost is unknown, so the
			// ledger may over-count until restart (conservative); the revision comes from the clock.
			if (wb_ == nullptr) throw;
		}
	}
	try {
		if (wb_ == nullptr) {
			RawKv raw;
			raw.row_version = r.version;
			raw.cost = cost;
			raw.bytes = Blob{std::move(bytes)};
			kv_.set_raw(key, std::move(raw));
			return;
		}
		kv_.admit(key.mod_id, cost, old_cost);
		KvEntry e;
		e.key = key;
		e.row_version = r.version;
		const std::uint64_t rev = next_revision(old_rev);
		e.revision = rev;
		e.encoded = Blob{std::move(bytes)};
		e.cost = cost;
		try {
			wb_->mark(std::move(e));
		} catch (...) {
			kv_.admit(key.mod_id, old_cost, cost);  // undo the reservation
			throw;
		}
		overlay_[key] = Pending{false, true, sh::Row{r.version, sh::deep_copy(r.value)}, rev, cost};
		trim_cache();
	} catch (const QuotaExceededError& e) {
		throw sh::StorageError(sh::StorageError::Code::QuotaExceeded, e.what());
	} catch (const QuarantinedRowError& e) {
		throw sh::StorageError(sh::StorageError::Code::Corrupt, e.what());
	}
}

bool KvStorageBackend::erase(const sh::StorageKey& k) {
	const KvKey key = map_key(k);
	std::scoped_lock l(mu_);
	if (wb_ == nullptr) return kv_.erase(key);
	std::uint64_t old_cost = 0, old_rev = 0;
	bool existed = false;
	if (const auto it = overlay_.find(key); it != overlay_.end()) {
		existed = !it->second.erased;
		old_cost = it->second.cost;
		old_rev = it->second.revision;
	} else {
		try {
			if (const auto m = kv_.meta(key)) {
				existed = true;
				old_cost = m->cost;
				old_rev = m->revision;
			}
		} catch (const RepositoryError&) {
			existed = true;  // backend down: the erase is queued regardless (it is idempotent)
		}
	}
	if (!existed) return false;
	const std::uint64_t rev = next_revision(old_rev);
	KvEntry e;
	e.key = key;
	e.erased = true;
	e.revision = rev;
	wb_->mark(std::move(e));
	kv_.release(key.mod_id, old_cost);
	overlay_[key] = Pending{true, true, sh::Row{}, rev, 0};
	return true;
}

std::uint64_t KvStorageBackend::usage(const std::string& mod) const { return kv_.bytes_used(mod); }

std::vector<std::string> KvStorageBackend::keys(sh::Scope s, const std::string& owner, const std::string& mod) const {
	std::set<std::string> out;
	const auto scope = map_scope(s);
	std::scoped_lock l(mu_);
	for (auto& key : kv_.keys(scope, owner.empty() ? std::string("0") : owner, mod)) out.insert(std::move(key));
	for (const auto& [key, p] : overlay_) {
		if (key.owner.scope != scope || key.owner_text != owner || key.mod_id != mod) continue;
		if (p.erased) out.erase(key.key); else out.insert(key.key);
	}
	return {out.begin(), out.end()};
}

} // namespace rathena::persist
