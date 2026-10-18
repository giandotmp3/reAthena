#include "rathena/persist/codec.hpp"

#include <array>

namespace rathena::persist {
namespace {

enum Tag : std::uint8_t { kInt = 1, kDouble = 2, kString = 3, kBool = 4, kTable = 5 };

void put(ByteWriter& w, const Value& v, int depth) {
	if (depth > kMaxValueDepth) throw PersistError("value nested deeper than 16 levels");
	struct V {
		ByteWriter& w;
		int depth;
		void operator()(std::int64_t x) const { w.u8(kInt); w.i64(x); }
		void operator()(double x) const { w.u8(kDouble); w.f64(x); }
		void operator()(const std::string& x) const { w.u8(kString); w.str(x); }
		void operator()(bool x) const { w.u8(kBool); w.u8(x ? 1 : 0); }
		void operator()(const std::shared_ptr<const ValueTable>& t) const {
			w.u8(kTable);
			const auto n = t ? t->entries.size() : 0;
			w.u32(static_cast<std::uint32_t>(n));
			if (t) for (const auto& [k, val] : t->entries) { w.str(k); put(w, val, depth + 1); }
		}
	};
	std::visit(V{w, depth}, v.data);
}

Value get(ByteReader& r, int depth) {
	if (depth > kMaxValueDepth) throw DecodeError("value nested too deep");
	switch (r.u8()) {
	case kInt: return Value{r.i64()};
	case kDouble: return Value{r.f64()};
	case kString: return Value{r.str()};
	case kBool: { const auto b = r.u8(); if (b > 1) throw DecodeError("bad bool"); return Value{b == 1}; }
	case kTable: {
		auto t = std::make_shared<ValueTable>();
		const auto n = r.count(6);  // key length (4) + tag (1) + at least 1 payload byte
		t->entries.reserve(n);
		for (std::uint32_t i = 0; i < n; ++i) {
			auto k = r.str();
			t->entries.emplace_back(std::move(k), get(r, depth + 1));
		}
		return Value{std::shared_ptr<const ValueTable>(std::move(t))};
	}
	default: throw DecodeError("unknown value tag");
	}
}

const std::array<std::uint32_t, 256>& crc_table() {
	static const auto table = [] {
		std::array<std::uint32_t, 256> t{};
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			t[i] = c;
		}
		return t;
	}();
	return table;
}

} // namespace

std::string encode_value(const Value& v) {
	ByteWriter w;
	w.u8(static_cast<std::uint8_t>(kCodecPersistValue));
	put(w, v, 0);
	return std::move(w.bytes());
}

Value decode_value(std::string_view bytes) {
	if (bytes.empty() || bytes.front() != kCodecPersistValue) throw DecodeError("bad codec byte");
	ByteReader r(bytes.substr(1));
	Value v = get(r, 0);
	if (!r.done()) throw DecodeError("trailing bytes");
	return v;
}

std::uint32_t crc32(std::string_view data) noexcept {
	const auto& t = crc_table();
	std::uint32_t c = 0xFFFFFFFFu;
	for (char ch : data) c = t[(c ^ static_cast<std::uint8_t>(ch)) & 0xff] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

// ---- batch --------------------------------------------------------------------------------
std::string encode_batch(const DirtyBatch& b) {
	ByteWriter w;
	w.u64(b.epoch);
	w.u32(static_cast<std::uint32_t>(b.records.size()));
	for (const auto& r : b.records) {
		w.u8(static_cast<std::uint8_t>(r.key.owner.scope));
		w.u64(r.key.owner.id);
		w.str(r.key.kind);
		w.u32(r.schema_version);
		w.u64(r.revision);
		w.str(r.payload.bytes);
	}
	w.u32(static_cast<std::uint32_t>(b.kv.size()));
	for (const auto& e : b.kv) {
		w.u8(static_cast<std::uint8_t>(e.key.owner.scope));
		w.u64(e.key.owner.id);
		w.str(e.key.owner_text);
		w.str(e.key.mod_id);
		w.str(e.key.key);
		w.u32(e.row_version);
		w.u64(e.revision);
		w.u8(e.erased ? 1 : 0);
		if (e.erased) continue;
		w.str(e.encoded ? e.encoded->bytes : encode_value(e.value));
		w.u64(e.cost);
	}
	return std::move(w.bytes());
}

DirtyBatch decode_batch(std::string_view bytes) {
	ByteReader r(bytes);
	DirtyBatch b;
	b.epoch = r.u64();
	const auto nr = r.count(29);
	b.records.reserve(nr);
	auto scope = [&r]() {
		const auto s = r.u8();
		if (s > static_cast<std::uint8_t>(OwnerScope::World)) throw DecodeError("bad owner scope");
		return static_cast<OwnerScope>(s);
	};
	for (std::uint32_t i = 0; i < nr; ++i) {
		Record rec;
		rec.key.owner.scope = scope();
		rec.key.owner.id = r.u64();
		rec.key.kind = r.str();
		rec.schema_version = r.u32();
		rec.revision = r.u64();
		rec.payload.bytes = r.str();
		b.records.push_back(std::move(rec));
	}
	const auto nk = r.count(34);
	b.kv.reserve(nk);
	for (std::uint32_t i = 0; i < nk; ++i) {
		KvEntry e;
		e.key.owner.scope = scope();
		e.key.owner.id = r.u64();
		e.key.owner_text = r.str();
		e.key.mod_id = r.str();
		e.key.key = r.str();
		e.row_version = r.u32();
		e.revision = r.u64();
		const auto flags = r.u8();
		if (flags > 1) throw DecodeError("bad flags");
		e.erased = flags == 1;
		if (!e.erased) {
			e.encoded = Blob{r.str()};
			e.cost = r.u64();
		}
		b.kv.push_back(std::move(e));
	}
	if (!r.done()) throw DecodeError("trailing bytes");
	return b;
}

} // namespace rathena::persist
