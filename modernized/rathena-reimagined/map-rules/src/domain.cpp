#include "rathena/map_rules/domain.hpp"

#include <algorithm>
#include <cstdlib>

namespace rathena::map_rules {

Zeny Zeny::clamped(std::int64_t v) {
	return Zeny(static_cast<std::int32_t>(std::clamp<std::int64_t>(v, 0, kMaxZeny)));
}

std::optional<Zeny> Zeny::checked(std::int64_t v) {
	if (v < 0 || v > kMaxZeny) {
		return std::nullopt;
	}
	return Zeny(static_cast<std::int32_t>(v));
}

Zeny Zeny::credited(std::int64_t amount) const {
	if (amount <= 0) {
		return *this;
	}
	return clamped(static_cast<std::int64_t>(value_) + std::min<std::int64_t>(amount, kMaxZeny));
}

std::optional<Zeny> Zeny::debited(std::int64_t amount) const {
	if (amount < 0 || amount > value_) {
		return std::nullopt;
	}
	return Zeny(static_cast<std::int32_t>(value_ - amount));
}

bool within_cells(const Position& a, const Position& b, std::int32_t range) {
	if (a.map_id != b.map_id) {
		return false;
	}
	const std::int64_t dx = std::llabs(static_cast<std::int64_t>(a.x) - b.x);
	const std::int64_t dy = std::llabs(static_cast<std::int64_t>(a.y) - b.y);
	return std::max(dx, dy) <= range;
}

} // namespace rathena::map_rules
