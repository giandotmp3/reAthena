#pragma once
// Binary encoding helpers (little-endian, bounds-checked) and the built-in Value codec.
// Every stored KV value starts with a codec byte so that rows written by different models never
// get decoded by the wrong one: 'P' = persistence Value, 'S' = script-host Value.
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "rathena/persist/domain.hpp"
#include "rathena/persist/errors.hpp"

namespace rathena::persist {

inline constexpr char kCodecPersistValue = 'P';
inline constexpr char kCodecScriptHostValue = 'S';
inline constexpr int kMaxValueDepth = 16;

class ByteWriter {
public:
	void u8(std::uint8_t v) { buf_.push_back(static_cast<char>(v)); }
	void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); }
	void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); }
	void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
	void f64(double v) { std::uint64_t b; std::memcpy(&b, &v, 8); u64(b); }
	void str(std::string_view s) { u32(static_cast<std::uint32_t>(s.size())); buf_.append(s); }
	[[nodiscard]] std::string& bytes() { return buf_; }
	[[nodiscard]] const std::string& bytes() const { return buf_; }
private:
	std::string buf_;
};

class ByteReader {
public:
	explicit ByteReader(std::string_view data) : d_(data) {}
	std::uint8_t u8() { need(1); return static_cast<std::uint8_t>(d_[pos_++]); }
	std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(d_[pos_ + static_cast<std::size_t>(i)])) << (8 * i); pos_ += 4; return v; }
	std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(d_[pos_ + static_cast<std::size_t>(i)])) << (8 * i); pos_ += 8; return v; }
	std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
	double f64() { std::uint64_t b = u64(); double v; std::memcpy(&v, &b, 8); return v; }
	std::string str() { std::uint32_t n = u32(); need(n); std::string s(d_.substr(pos_, n)); pos_ += n; return s; }
	/// A count that must be satisfiable by the remaining bytes (each item needs >= min_item bytes).
	std::uint32_t count(std::size_t min_item) { std::uint32_t n = u32(); if (min_item != 0 && static_cast<std::uint64_t>(n) * min_item > remaining()) throw DecodeError("count exceeds data"); return n; }
	[[nodiscard]] std::size_t remaining() const { return d_.size() - pos_; }
	[[nodiscard]] bool done() const { return pos_ == d_.size(); }
private:
	void need(std::size_t n) const { if (d_.size() - pos_ < n) throw DecodeError("truncated data"); }
	std::string_view d_;
	std::size_t pos_{0};
};

/// 'P' + body. Throws DecodeError-free; values nested deeper than kMaxValueDepth throw PersistError.
std::string encode_value(const Value& v);
/// Throws DecodeError for any malformed input (unknown tag, truncation, trailing bytes, depth).
Value decode_value(std::string_view bytes);
/// CRC-32 (IEEE) used by WAL segments.
std::uint32_t crc32(std::string_view data) noexcept;

/// Serialized form of a dirty batch (used by the WAL). decode_batch throws DecodeError.
std::string encode_batch(const DirtyBatch& b);
DirtyBatch decode_batch(std::string_view bytes);

} // namespace rathena::persist
