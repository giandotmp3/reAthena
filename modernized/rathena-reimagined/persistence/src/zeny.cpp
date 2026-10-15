#include "rathena/persistence/zeny.hpp"

#include <string>

namespace rathena::persistence {

Zeny Zeny::of(std::int64_t v) {
	if (v < 0 || v > kMax) throw ZenyRangeError("zeny out of range [0, INT32_MAX]: " + std::to_string(v));
	return Zeny(static_cast<std::int32_t>(v));
}

std::optional<Zeny> Zeny::from_db(std::int64_t raw) noexcept {
	if (raw < 0 || raw > kMax) return std::nullopt;
	return Zeny(static_cast<std::int32_t>(raw));
}

ZenyCredit Zeny::credit(std::int64_t amount) const {
	if (amount < 0) throw std::invalid_argument("zeny credit must be non-negative; use debit()");
	const std::int64_t room = std::int64_t{kMax} - value_;
	if (amount > room) return {Zeny(kMax), amount - room};
	return {Zeny(static_cast<std::int32_t>(value_ + amount)), 0};
}

std::optional<Zeny> Zeny::debit(std::int64_t amount) const noexcept {
	if (amount < 0 || amount > value_) return std::nullopt;
	return Zeny(static_cast<std::int32_t>(value_ - amount));
}

} // namespace rathena::persistence
