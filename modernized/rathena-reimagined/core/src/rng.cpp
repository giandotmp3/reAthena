#include "rathena/core/rng.hpp"

#include <limits>
#include <utility>

namespace rathena::core {

std::uint32_t Rng::below(std::uint32_t bound) {
	if (bound <= 1) {
		return 0;
	}
	// Reject the low partial bucket: threshold = 2^32 mod bound.
	const std::uint32_t threshold = (0u - bound) % bound; // 0u - bound: no unary minus on unsigned
	for (;;) {
		const std::uint32_t r = next_u32();
		if (r >= threshold) {
			return r % bound;
		}
	}
}

std::int32_t Rng::range(std::int32_t min, std::int32_t max) {
	if (min > max) {
		std::swap(min, max);
	}
	const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(max) - min) + 1u;
	if (span > std::numeric_limits<std::uint32_t>::max()) {
		// Full 32-bit span: every raw value is a valid offset.
		return static_cast<std::int32_t>(static_cast<std::int64_t>(min) + next_u32());
	}
	const auto off = below(static_cast<std::uint32_t>(span));
	return static_cast<std::int32_t>(static_cast<std::int64_t>(min) + off);
}

bool Rng::chance(std::uint32_t numerator, std::uint32_t denominator) {
	if (denominator == 0 || numerator == 0) {
		return false;
	}
	if (numerator >= denominator) {
		return true;
	}
	return below(denominator) < numerator;
}

std::uint32_t SequenceRng::next_u32() {
	if (values_.empty()) {
		++calls_;
		return 0;
	}
	return values_[calls_++ % values_.size()];
}

} // namespace rathena::core
